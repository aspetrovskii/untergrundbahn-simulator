#include "core/Schedule.h"
#include "core/Time.h"

#include <QtTest>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;

ubahn::core::Line trainingLine(int fleet) {
    ubahn::core::Line line;
    line.n = 8;
    line.cfg.fleetSize = fleet;
    // SPEC 4.3 defaults: 45 + 30 + 45. SPEC 12 running times.
    const double lengths[] = {1100, 780, 1700, 950, 1500, 860, 1400};
    const int32_t tStd[] = {120, 90, 130, 100, 110, 95, 125};
    for (int i = 0; i < 7; ++i) {
        ubahn::core::Span span;
        span.lengthM = lengths[i];
        span.tStd = tStd[i];
        line.spans.push_back(span);
    }
    return line;
}

std::vector<ubahn::api::Window> weekdayWindows() {
    return {
        {6 * kHour, 7 * kHour, 180, 60, 0.15},   {7 * kHour, 10 * kHour, 120, 80, 0.55},
        {10 * kHour, 17 * kHour, 160, 60, 0.25}, {17 * kHour, 20 * kHour, 120, 80, 0.50},
        {20 * kHour, 24 * kHour, 300, 40, 0.12},
    };
}

std::vector<ubahn::api::Window> weekendWindows() {
    return {
        {6 * kHour, 10 * kHour, 360, 40, 0.12},
        {10 * kHour, 20 * kHour, 240, 60, 0.20},
        {20 * kHour, 24 * kHour, 360, 40, 0.10},
    };
}

// SPEC 6.3: Monday..Friday share one profile, Saturday and Sunday the other.
ubahn::api::ScheduleConfig defaultSchedule() {
    ubahn::api::ScheduleConfig schedule;
    for (int day = 0; day < 5; ++day) {
        schedule.days[static_cast<size_t>(day)].windows = weekdayWindows();
    }
    for (int day = 5; day < 7; ++day) {
        schedule.days[static_cast<size_t>(day)].windows = weekendWindows();
    }
    return schedule;
}

int32_t sumTStd(const ubahn::core::Line& line) {
    int32_t sum = 0;
    for (const ubahn::core::Span& span : line.spans) {
        sum += span.tStd;
    }
    return sum;
}

} // namespace

class ScheduleTest : public QObject {
    Q_OBJECT

private slots:
    void cycleTimes();
    void demandAndFleet();
    void windowIndexAt_data();
    void windowIndexAt();
};

void ScheduleTest::cycleTimes() {
    const ubahn::core::Line line = trainingLine(26);
    QCOMPARE(line.n, 8);
    QCOMPARE(sumTStd(line), 770);

    QCOMPARE(ubahn::core::tTurn(line.cfg), 120);
    QCOMPARE(ubahn::core::tToTerm(line, 60), 1130);
    QCOMPARE(ubahn::core::tOneway(line, 60), 1190);
    QCOMPARE(ubahn::core::tCycle(line, 60), 2740);
    QCOMPARE(ubahn::core::tToTerm(line, 80), 1250);
    QCOMPARE(ubahn::core::tCycle(line, 80), 3060);
    QCOMPARE(ubahn::core::tToTerm(line, 40), 1010);
    QCOMPARE(ubahn::core::tCycle(line, 40), 2420);

    const ubahn::api::ScheduleAnalysis analysis = ubahn::core::analyzeLine(line, defaultSchedule());
    // Decision R5: the top-level field is the maximum over windows, not one dwell.
    QCOMPARE(analysis.tCycle, 3060);
    QCOMPARE(analysis.tTurn, 120);
    QVERIFY(ubahn::core::tCycle(line, 60) < analysis.tCycle);
    QVERIFY(ubahn::core::tCycle(line, 40) < analysis.tCycle);
}

void ScheduleTest::demandAndFleet() {
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const int32_t weekdayNeed[] = {16, 26, 18, 26, 9};
    const int32_t weekendNeed[] = {7, 12, 7};

    const ubahn::api::ScheduleAnalysis covered =
        ubahn::core::analyzeLine(trainingLine(26), schedule);
    QCOMPARE(covered.minFleetForAllWindows, 26);
    for (int day = 0; day < 5; ++day) {
        const std::vector<ubahn::api::WindowAnalysis>& rows =
            covered.days[static_cast<size_t>(day)];
        QCOMPARE(rows.size(), size_t{5});
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(rows[static_cast<size_t>(i)].nReq, weekdayNeed[i]);
            QCOMPARE(rows[static_cast<size_t>(i)].hEff,
                     schedule.days[0].windows[static_cast<size_t>(i)].headway);
            QVERIFY(!rows[static_cast<size_t>(i)].stretched);
        }
    }
    for (int day = 5; day < 7; ++day) {
        const std::vector<ubahn::api::WindowAnalysis>& rows =
            covered.days[static_cast<size_t>(day)];
        QCOMPARE(rows.size(), size_t{3});
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(rows[static_cast<size_t>(i)].nReq, weekendNeed[i]);
            QVERIFY(!rows[static_cast<size_t>(i)].stretched);
        }
    }
    QVERIFY(ubahn::core::stretchWarnings(covered, schedule).empty());
    QVERIFY(ubahn::core::stretchedWindows(covered, schedule).empty());
    QCOMPARE(ubahn::core::hEffAt(covered, schedule, 8 * kHour), 120);
    QCOMPARE(ubahn::core::hEffAt(covered, schedule, 5 * kHour + 59 * 60 + 59), 0);

    const ubahn::api::ScheduleAnalysis shortFleet =
        ubahn::core::analyzeLine(trainingLine(20), schedule);
    QCOMPARE(shortFleet.minFleetForAllWindows, 26);
    QCOMPARE(shortFleet.tCycle, 3060);
    for (int day = 0; day < 5; ++day) {
        const std::vector<ubahn::api::WindowAnalysis>& rows =
            shortFleet.days[static_cast<size_t>(day)];
        QCOMPARE(rows[1].hEff, 155);
        QVERIFY(rows[1].stretched);
        QCOMPARE(rows[1].nReq, 26);
        QCOMPARE(rows[3].hEff, 155);
        QVERIFY(rows[3].stretched);
        QVERIFY(!rows[0].stretched);
        QCOMPARE(rows[0].hEff, 180);
        QVERIFY(!rows[2].stretched);
        QVERIFY(!rows[4].stretched);
    }
    for (int day = 5; day < 7; ++day) {
        for (const ubahn::api::WindowAnalysis& row : shortFleet.days[static_cast<size_t>(day)]) {
            QVERIFY(!row.stretched);
        }
    }

    const std::vector<ubahn::api::Warning> warnings =
        ubahn::core::stretchWarnings(shortFleet, schedule);
    const std::vector<ubahn::api::StretchInfo> stretched =
        ubahn::core::stretchedWindows(shortFleet, schedule);
    QCOMPARE(warnings.size(), size_t{10});
    QCOMPARE(stretched.size(), size_t{10});

    const char* dayNames[] = {"Понедельник", "Вторник", "Среда", "Четверг", "Пятница"};
    for (int i = 0; i < 10; ++i) {
        const int day = i / 2;
        const int window = (i % 2 == 0) ? 1 : 3;
        const ubahn::api::Warning& warning = warnings[static_cast<size_t>(i)];
        QCOMPARE(warning.code, ubahn::api::WarningCode::IntervalStretched);
        QCOMPARE(warning.dayOfWeek, day);
        QCOMPARE(warning.windowIndex, window);
        QCOMPARE(warning.plannedHeadway, 120);
        QCOMPARE(warning.effectiveHeadway, 155);
        QCOMPARE(warning.requiredTrains, 26);
        const QString clock = (window == 1) ? QString("7:00-10:00") : QString("17:00-20:00");
        const QString expected =
            QString("%1, %2: плановый интервал 120 с растянут до 155 с, требуется 26 поездов")
                .arg(dayNames[day], clock);
        QCOMPARE(QString::fromStdString(warning.message), expected);

        const ubahn::api::StretchInfo& info = stretched[static_cast<size_t>(i)];
        QCOMPARE(info.dayOfWeek, day);
        QCOMPARE(info.windowIndex, window);
        QCOMPARE(info.planned, 120);
        QCOMPARE(info.effective, 155);
    }

    QCOMPARE(ubahn::core::hEffAt(shortFleet, schedule, 8 * kHour), 155);
    QCOMPARE(ubahn::core::hEffAt(shortFleet, schedule, 6 * kHour + 30 * 60), 180);
    QCOMPARE(ubahn::core::hEffAt(shortFleet, schedule, 5 * 86400 + 8 * kHour), 360);
    QCOMPARE(ubahn::core::hEffAt(shortFleet, schedule, 5 * kHour + 59 * 60 + 59), 0);
}

void ScheduleTest::windowIndexAt_data() {
    QTest::addColumn<qint64>("t");
    QTest::addColumn<int>("index");

    QTest::newRow("05:59:59 closed") << qint64{5 * kHour + 59 * 60 + 59} << -1;
    QTest::newRow("06:00") << qint64{6 * kHour} << 0;
    QTest::newRow("06:59:59") << qint64{6 * kHour + 59 * 60 + 59} << 0;
    QTest::newRow("07:00") << qint64{7 * kHour} << 1;
    QTest::newRow("23:59:59") << qint64{23 * kHour + 59 * 60 + 59} << 4;
    QTest::newRow("saturday profile") << qint64{5 * 86400 + 8 * kHour} << 0;
    QTest::newRow("next monday") << qint64{7 * 86400 + 8 * kHour} << 1;
    QTest::newRow("midnight closed") << qint64{86400} << -1;
}

void ScheduleTest::windowIndexAt() {
    QFETCH(qint64, t);
    QFETCH(int, index);
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const std::optional<int> found = ubahn::core::windowIndexAt(schedule, t);
    const ubahn::api::Window* window = ubahn::core::windowAt(schedule, t);
    if (index < 0) {
        QVERIFY(!found.has_value());
        QCOMPARE(window, nullptr);
        return;
    }
    QCOMPARE(found, std::optional<int>(index));
    QVERIFY(window != nullptr);
    const int day = ubahn::core::dowOf(t);
    const ubahn::api::Window& expected =
        schedule.days[static_cast<size_t>(day)].windows[static_cast<size_t>(index)];
    QCOMPARE(window->headway, expected.headway);
    QCOMPARE(window->fromTod, expected.fromTod);
    if (t == 5 * 86400 + 8 * kHour) {
        QCOMPARE(day, 5);
        QCOMPARE(window->headway, 360);
    }
    if (t == 7 * 86400 + 8 * kHour) {
        QCOMPARE(day, 0);
        QCOMPARE(window->headway, 120);
    }
}

QTEST_APPLESS_MAIN(ScheduleTest)

#include "tst_schedule.moc"
