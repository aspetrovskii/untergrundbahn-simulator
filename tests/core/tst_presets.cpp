#include "api/Functions.h"

#include <QtTest>
#include <optional>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;

void expectWindow(const ubahn::api::Window& window, int32_t fromTod, int32_t toTod, int32_t headway,
                  int32_t dwell, double delayProb) {
    QCOMPARE(window.fromTod, fromTod);
    QCOMPARE(window.toTod, toTod);
    QCOMPARE(window.headway, headway);
    QCOMPARE(window.dwell, dwell);
    QCOMPARE(window.delayProb, delayProb);
}

void expectPlaybackDefaults(const ubahn::api::SimParams& params) {
    QCOMPARE(params.period.start, ubahn::api::kOpenTod);
    QCOMPARE(params.period.end, ubahn::api::kOpenTod + 6 * kHour);
    QCOMPARE(params.stepSec, 60);
    QCOMPARE(params.rate, 1.0);
    QVERIFY(!params.seed.has_value());
}

void expectLineDefaults(const ubahn::api::LineConfig& line) {
    QCOMPARE(line.dwellMin, 40);
    QCOMPARE(line.headwayMin, 65);
    QCOMPARE(line.tClear, 25);
    QCOMPARE(line.kSlow, 1.3);
    QCOMPARE(line.toDeadEnd, 45);
    QCOMPARE(line.cabChange, 30);
    QCOMPARE(line.fromDeadEnd, 45);
    QCOMPARE(line.depotOut, 90);
    QCOMPARE(line.depotIn, 90);
    QCOMPARE(line.signalStopOffsetM, 200.0);
    QCOMPARE(line.signalApproach, 20);
}

void expectSameWindow(const ubahn::api::Window& left, const ubahn::api::Window& right) {
    QCOMPARE(left.fromTod, right.fromTod);
    QCOMPARE(left.toTod, right.toTod);
    QCOMPARE(left.headway, right.headway);
    QCOMPARE(left.dwell, right.dwell);
    QCOMPARE(left.delayProb, right.delayProb);
}

void expectSameSchedule(const ubahn::api::ScheduleConfig& left,
                        const ubahn::api::ScheduleConfig& right) {
    QCOMPARE(left.delayMin, right.delayMin);
    QCOMPARE(left.delayMax, right.delayMax);
    for (size_t day = 0; day < left.days.size(); ++day) {
        const std::vector<ubahn::api::Window>& a = left.days[day].windows;
        const std::vector<ubahn::api::Window>& b = right.days[day].windows;
        QCOMPARE(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            expectSameWindow(a[i], b[i]);
        }
    }
}

void expectSameLine(const ubahn::api::LineConfig& left, const ubahn::api::LineConfig& right) {
    QCOMPARE(left.stationNames.size(), right.stationNames.size());
    for (size_t i = 0; i < left.stationNames.size(); ++i) {
        QVERIFY(left.stationNames[i] == right.stationNames[i]);
    }
    QCOMPARE(left.spans.size(), right.spans.size());
    for (size_t i = 0; i < left.spans.size(); ++i) {
        QCOMPARE(left.spans[i].lengthM, right.spans[i].lengthM);
        QCOMPARE(left.spans[i].tStd, right.spans[i].tStd);
    }
    QCOMPARE(left.dwellMin, right.dwellMin);
    QCOMPARE(left.headwayMin, right.headwayMin);
    QCOMPARE(left.tClear, right.tClear);
    QCOMPARE(left.kSlow, right.kSlow);
    QCOMPARE(left.toDeadEnd, right.toDeadEnd);
    QCOMPARE(left.cabChange, right.cabChange);
    QCOMPARE(left.fromDeadEnd, right.fromDeadEnd);
    QCOMPARE(left.depotOut, right.depotOut);
    QCOMPARE(left.depotIn, right.depotIn);
    QCOMPARE(left.signalStopOffsetM, right.signalStopOffsetM);
    QCOMPARE(left.signalApproach, right.signalApproach);
    QCOMPARE(left.fleetSize, right.fleetSize);
}

void expectSameParams(const ubahn::api::SimParams& left, const ubahn::api::SimParams& right) {
    expectSameLine(left.line, right.line);
    expectSameSchedule(left.schedule, right.schedule);
    QCOMPARE(left.period.start, right.period.start);
    QCOMPARE(left.period.end, right.period.end);
    QCOMPARE(left.stepSec, right.stepSec);
    QCOMPARE(left.rate, right.rate);
    QCOMPARE(left.seed, right.seed);
}

// Empty tStd. `tie` is true when C3's coefficient and the physical minimum meet.
void expectOpenSpan(const ubahn::api::LineConfig& line, int index, double lengthM, bool tie) {
    const ubahn::api::SpanInput& span = line.spans[static_cast<size_t>(index)];
    QCOMPARE(span.lengthM, lengthM);
    QVERIFY(!span.tStd.has_value());
    const int32_t phys = ubahn::api::physMinRunTime(lengthM);
    const int32_t tMin = ubahn::api::minRunTime(line, index);
    if (tie) {
        QCOMPARE(tMin, phys);
    } else {
        QVERIFY(tMin > phys);
    }
}

} // namespace

class PresetsTest : public QObject {
    Q_OBJECT

private slots:
    void orderIdsAndSizes();
    void trainingLineMatchesSpec();
    void defaultScheduleMatchesSpec();
    void defaultParamsMatchesTraining();
    void trainingAnalysisMatchesBe04();
    void minimalLineLeavesTStdEmpty();
    void longLineLeavesTStdEmpty();
};

void PresetsTest::orderIdsAndSizes() {
    const std::vector<ubahn::api::Preset> presets = ubahn::api::builtinPresets();
    QCOMPARE(presets.size(), size_t{3});

    QCOMPARE(QString::fromStdString(presets[0].id), QString("training-8"));
    QCOMPARE(QString::fromStdString(presets[1].id), QString("minimal-7"));
    QCOMPARE(QString::fromStdString(presets[2].id), QString("long-20"));
    QCOMPARE(QString::fromStdString(presets[0].name), QString("Учебная, 8 станций"));
    QCOMPARE(QString::fromStdString(presets[1].name), QString("Минимальная, 7 станций"));
    QCOMPARE(QString::fromStdString(presets[2].name), QString("Длинная, 20 станций"));

    const int stationCount[] = {8, 7, 20};
    const int fleet[] = {26, 23, 87};
    for (int i = 0; i < 3; ++i) {
        const ubahn::api::SimParams& params = presets[static_cast<size_t>(i)].params;
        QCOMPARE(static_cast<int>(params.line.stationNames.size()), stationCount[i]);
        QCOMPARE(static_cast<int>(params.line.spans.size()), stationCount[i] - 1);
        QCOMPARE(params.line.fleetSize, fleet[i]);
        const ubahn::api::ScheduleAnalysis analysis =
            ubahn::api::analyze(params.line, params.schedule);
        QCOMPARE(analysis.minFleetForAllWindows, fleet[i]);
        expectPlaybackDefaults(params);
        expectLineDefaults(params.line);
    }
}

void PresetsTest::trainingLineMatchesSpec() {
    const ubahn::api::LineConfig line = ubahn::api::builtinPresets().at(0).params.line;
    const char* names[] = {"Северная",    "Полевая", "Заводская", "Университет",
                           "Центральная", "Рынок",   "Парк",      "Южная"};
    const double lengths[] = {1100, 780, 1700, 950, 1500, 860, 1400};
    const int32_t tStd[] = {120, 90, 130, 100, 110, 95, 125};
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(QString::fromStdString(line.stationNames[static_cast<size_t>(i)]),
                 QString(names[i]));
    }
    for (int i = 0; i < 7; ++i) {
        const ubahn::api::SpanInput& span = line.spans[static_cast<size_t>(i)];
        QCOMPARE(span.lengthM, lengths[i]);
        QVERIFY(span.tStd.has_value());
        QCOMPARE(*span.tStd, tStd[i]);
    }
}

void PresetsTest::defaultScheduleMatchesSpec() {
    const ubahn::api::ScheduleConfig schedule = ubahn::api::defaultSchedule();
    QCOMPARE(schedule.delayMin, 20);
    QCOMPARE(schedule.delayMax, 60);

    for (int day = 0; day < 5; ++day) {
        const std::vector<ubahn::api::Window>& windows =
            schedule.days[static_cast<size_t>(day)].windows;
        QCOMPARE(windows.size(), size_t{5});
        expectWindow(windows[0], 6 * kHour, 7 * kHour, 180, 60, 0.15);
        expectWindow(windows[1], 7 * kHour, 10 * kHour, 120, 80, 0.55);
        expectWindow(windows[2], 10 * kHour, 17 * kHour, 160, 60, 0.25);
        expectWindow(windows[3], 17 * kHour, 20 * kHour, 120, 80, 0.50);
        expectWindow(windows[4], 20 * kHour, ubahn::api::kCloseTod, 300, 40, 0.12);
    }
    for (int day = 5; day < 7; ++day) {
        const std::vector<ubahn::api::Window>& windows =
            schedule.days[static_cast<size_t>(day)].windows;
        QCOMPARE(windows.size(), size_t{3});
        expectWindow(windows[0], 6 * kHour, 10 * kHour, 360, 40, 0.12);
        expectWindow(windows[1], 10 * kHour, 20 * kHour, 240, 60, 0.20);
        expectWindow(windows[2], 20 * kHour, ubahn::api::kCloseTod, 360, 40, 0.10);
    }
}

void PresetsTest::defaultParamsMatchesTraining() {
    const ubahn::api::SimParams params = ubahn::api::defaultParams();
    const std::vector<ubahn::api::Preset> presets = ubahn::api::builtinPresets();
    const ubahn::api::Preset& training = presets.at(0);
    expectSameParams(params, training.params);
    QCOMPARE(params.line.fleetSize, 26);
    expectPlaybackDefaults(params);
}

void PresetsTest::trainingAnalysisMatchesBe04() {
    const ubahn::api::SimParams params = ubahn::api::builtinPresets().at(0).params;
    const ubahn::api::ScheduleAnalysis analysis = ubahn::api::analyze(params.line, params.schedule);
    const int32_t weekdayNeed[] = {16, 26, 18, 26, 9};
    const int32_t weekendNeed[] = {7, 12, 7};

    QCOMPARE(analysis.minFleetForAllWindows, 26);
    QCOMPARE(analysis.tCycle, 3060);
    QCOMPARE(analysis.tTurn, 120);
    for (int day = 0; day < 5; ++day) {
        const std::vector<ubahn::api::WindowAnalysis>& rows =
            analysis.days[static_cast<size_t>(day)];
        QCOMPARE(rows.size(), size_t{5});
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(rows[static_cast<size_t>(i)].nReq, weekdayNeed[i]);
            QVERIFY(!rows[static_cast<size_t>(i)].stretched);
        }
    }
    for (int day = 5; day < 7; ++day) {
        const std::vector<ubahn::api::WindowAnalysis>& rows =
            analysis.days[static_cast<size_t>(day)];
        QCOMPARE(rows.size(), size_t{3});
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(rows[static_cast<size_t>(i)].nReq, weekendNeed[i]);
            QVERIFY(!rows[static_cast<size_t>(i)].stretched);
        }
    }
}

void PresetsTest::minimalLineLeavesTStdEmpty() {
    const ubahn::api::LineConfig line = ubahn::api::builtinPresets().at(1).params.line;
    const char* names[] = {"Пристань", "Берёзовая", "Слобода", "Ратуша",
                           "Ярмарка",  "Роща",      "Застава"};
    const double lengths[] = {400, 1200, 650, 2100, 800, 1500};
    const bool tie[] = {true, true, false, false, false, true};
    for (int i = 0; i < 7; ++i) {
        QCOMPARE(QString::fromStdString(line.stationNames[static_cast<size_t>(i)]),
                 QString(names[i]));
    }
    for (int i = 0; i < 6; ++i) {
        expectOpenSpan(line, i, lengths[i], tie[i]);
    }
}

void PresetsTest::longLineLeavesTStdEmpty() {
    const ubahn::api::LineConfig line = ubahn::api::builtinPresets().at(2).params.line;
    const char* names[] = {"Озёрная",    "Каменная",      "Луговая",   "Прядильная", "Набережная",
                           "Крепостная", "Вишнёвая",      "Мостовая",  "Губернская", "Ткацкая",
                           "Садовая",    "Привокзальная", "Кузнечная", "Звёздная",   "Ремесленная",
                           "Соборная",   "Холмистая",     "Граничная", "Портовая",   "Дальняя"};
    const double lengths[] = {4600, 4100, 3200, 2800, 1950, 1700, 1200, 900,  600, 500,
                              400,  800,  1050, 1400, 1650, 2400, 3050, 4500, 5000};
    const bool tie[] = {true, false, true, false, true, false, true, false, true, false,
                        true, false, true, false, true, false, true, false, false};
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(QString::fromStdString(line.stationNames[static_cast<size_t>(i)]),
                 QString(names[i]));
    }
    for (int i = 0; i < 19; ++i) {
        expectOpenSpan(line, i, lengths[i], tie[i]);
    }
}

QTEST_APPLESS_MAIN(PresetsTest)

#include "tst_presets.moc"
