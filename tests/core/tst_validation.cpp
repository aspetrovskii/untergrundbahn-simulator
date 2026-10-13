#include "api/Functions.h"
#include "core/Line.h"
#include "core/Schedule.h"

#include <QtTest>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;

ubahn::api::SpanInput spanOf(double lengthM, std::optional<int32_t> tStd) {
    ubahn::api::SpanInput span;
    span.lengthM = lengthM;
    span.tStd = tStd;
    return span;
}

ubahn::api::LineConfig lineOf(int stationCount) {
    ubahn::api::LineConfig line;
    line.stationNames.reserve(static_cast<size_t>(stationCount));
    for (int i = 0; i < stationCount; ++i) {
        line.stationNames.push_back("Станция " + std::to_string(i + 1));
    }
    const int32_t tStd = ubahn::api::physMinRunTime(1000.0);
    for (int i = 0; i < stationCount - 1; ++i) {
        line.spans.push_back(spanOf(1000.0, tStd));
    }
    line.fleetSize = 10;
    return line;
}

void fillDays(ubahn::api::ScheduleConfig& schedule, const ubahn::api::Window& window) {
    for (ubahn::api::DayProfile& day : schedule.days) {
        day.windows = {window};
    }
}

// One window, headway well above C6/C11/C12, so a single out-of-range line field stays alone.
ubahn::api::SimParams looseParams() {
    ubahn::api::SimParams params;
    params.line = lineOf(8);
    ubahn::api::Window window;
    window.fromTod = 6 * kHour;
    window.toTod = 24 * kHour;
    window.headway = 2000;
    window.dwell = 200;
    window.delayProb = 0.0;
    fillDays(params.schedule, window);
    params.period.start = 6 * kHour;
    params.period.end = 7 * 86400 + 12 * kHour;
    params.stepSec = 60;
    params.rate = 1.0;
    return params;
}

ubahn::api::LineConfig trainingLine() {
    ubahn::api::LineConfig line;
    line.stationNames = {"Северная",    "Полевая", "Заводская", "Университет",
                         "Центральная", "Рынок",   "Парк",      "Южная"};
    const double lengths[] = {1100, 780, 1700, 950, 1500, 860, 1400};
    const int32_t tStds[] = {120, 90, 130, 100, 110, 95, 125};
    for (int i = 0; i < 7; ++i) {
        line.spans.push_back(spanOf(lengths[i], tStds[i]));
    }
    line.fleetSize = 26;
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

// SPEC 6.3 and 12, built here. Presets are not used.
ubahn::api::SimParams trainingParams() {
    ubahn::api::SimParams params;
    params.line = trainingLine();
    for (int day = 0; day < 5; ++day) {
        params.schedule.days[static_cast<size_t>(day)].windows = weekdayWindows();
    }
    for (int day = 5; day < 7; ++day) {
        params.schedule.days[static_cast<size_t>(day)].windows = weekendWindows();
    }
    params.period.start = 6 * kHour;
    params.period.end = 7 * 86400 + 12 * kHour;
    params.stepSec = 60;
    params.rate = 1.0;
    return params;
}

void replaceMonday(ubahn::api::SimParams& params, std::vector<ubahn::api::Window> windows) {
    params.schedule.days[0].windows = std::move(windows);
}

ubahn::api::Window windowAt(int32_t from, int32_t to, int32_t headway, int32_t dwell, double prob) {
    ubahn::api::Window window;
    window.fromTod = from;
    window.toTod = to;
    window.headway = headway;
    window.dwell = dwell;
    window.delayProb = prob;
    return window;
}

QString issueText(const std::vector<ubahn::api::ValidationIssue>& issues) {
    QString text;
    for (const ubahn::api::ValidationIssue& issue : issues) {
        text += QString::fromStdString(issue.field);
        text += QStringLiteral(" | ");
        text += QString::number(static_cast<int>(issue.code));
        text += QStringLiteral(" | ");
        text += QString::fromStdString(issue.message);
        text += QLatin1Char('\n');
    }
    return text;
}

} // namespace

class ValidationTest : public QObject {
    Q_OBJECT

private slots:
    void trainingParamsAreValid();
    void shortFleetIsNotAnError();
    void tripLongerThanSixHoursIsAllowed();
    void acceptedBounds_data();
    void acceptedBounds();
    void rejectedRanges_data();
    void rejectedRanges();
    void stationCount();
    void emptyLineReportsCountAndSpans();
    void spanCountMismatch();
    void apiExampleMessages();
    void signalOffsetTooLong();
    void windowOutOfHours();
    void windowsOverlap();
    void windowsOutOfOrder();
    void windowsGap();
    void emptyProfile();
    void degenerateWindow();
    void headwayBelowDwellPlusClear();
    void headwayBelowTurnaround();
    void headwayBelowMin();
    void dwellBelowMin();
    void probabilityOutOfRange();
    void delayRangeInvalid();
    void periodEnd();
    void periodStart();
    void stepOutOfRange();
    void severalIssuesKeepOrder();
    void validateScheduleOmitsLinePeriodAndStep();
    void nonFiniteValues();
};

void ValidationTest::trainingParamsAreValid() {
    const ubahn::api::SimParams params = trainingParams();
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QVERIFY2(issues.empty(), qPrintable(issueText(issues)));
    QVERIFY(ubahn::api::validateSchedule(params.line, params.schedule).empty());
}

void ValidationTest::shortFleetIsNotAnError() {
    // SPEC 9.4: k < n_req stretches the interval, it does not fail validation.
    ubahn::api::SimParams params = trainingParams();
    params.line.fleetSize = 2;
    QVERIFY(ubahn::api::validate(params).empty());
}

void ValidationTest::tripLongerThanSixHoursIsAllowed() {
    // Dispatcher decision: generation stops; this is not a validation error.
    ubahn::api::SimParams params = looseParams();
    params.line = lineOf(20);
    for (ubahn::api::SpanInput& span : params.line.spans) {
        span.lengthM = 400.0;
        span.tStd = 2000;
    }
    params.line.fleetSize = 2;
    const ubahn::core::Line line = ubahn::core::buildLine(params.line);
    QVERIFY(ubahn::core::tToTerm(line, 200) > 6 * kHour);
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QVERIFY2(issues.empty(), qPrintable(issueText(issues)));
}

void ValidationTest::acceptedBounds_data() {
    QTest::addColumn<int>("kind");

    QTest::newRow("length 400") << 1;
    QTest::newRow("length 5000") << 2;
    QTest::newRow("fleet 2") << 3;
    QTest::newRow("fleet 200") << 4;
    QTest::newRow("dwellMin 30") << 5;
    QTest::newRow("dwellMin 180") << 6;
    QTest::newRow("headwayMin 30") << 7;
    QTest::newRow("headwayMin 300") << 8;
    QTest::newRow("tClear 10") << 9;
    QTest::newRow("tClear 120") << 10;
    QTest::newRow("kSlow 1") << 11;
    QTest::newRow("kSlow 2") << 12;
    QTest::newRow("toDeadEnd 10") << 13;
    QTest::newRow("toDeadEnd 600") << 14;
    QTest::newRow("cabChange 10") << 15;
    QTest::newRow("cabChange 600") << 16;
    QTest::newRow("fromDeadEnd 10") << 17;
    QTest::newRow("fromDeadEnd 600") << 18;
    QTest::newRow("depotOut 10") << 19;
    QTest::newRow("depotOut 600") << 20;
    QTest::newRow("depotIn 10") << 21;
    QTest::newRow("depotIn 600") << 22;
    QTest::newRow("turn 600+600+600") << 23;
    QTest::newRow("signal 50") << 24;
    QTest::newRow("signal 300") << 25;
    QTest::newRow("approach 10") << 26;
    QTest::newRow("approach 60") << 27;
    QTest::newRow("delay 0") << 28;
    QTest::newRow("delay 600") << 29;
    QTest::newRow("delay 0 to 600") << 30;
    QTest::newRow("step 1") << 31;
    QTest::newRow("step 240") << 32;
    QTest::newRow("rate 0") << 33;
    QTest::newRow("rate 1000") << 34;
    QTest::newRow("N 7") << 35;
    QTest::newRow("N 20") << 36;
    QTest::newRow("omitted tStd") << 37;
    QTest::newRow("probability 0 and 1") << 38;
    QTest::newRow("period start 0") << 39;
    QTest::newRow("abutting windows") << 40;
    QTest::newRow("C11 and C12 equal") << 41;
}

void ValidationTest::acceptedBounds() {
    QFETCH(int, kind);
    ubahn::api::SimParams params = looseParams();
    switch (kind) {
    case 1:
        for (ubahn::api::SpanInput& span : params.line.spans) {
            span.lengthM = 400.0;
            span.tStd = ubahn::api::physMinRunTime(400.0);
        }
        break;
    case 2:
        for (ubahn::api::SpanInput& span : params.line.spans) {
            span.lengthM = 5000.0;
            span.tStd = ubahn::api::physMinRunTime(5000.0);
        }
        break;
    case 3:
        params.line.fleetSize = 2;
        break;
    case 4:
        params.line.fleetSize = 200;
        break;
    case 5:
        params.line.dwellMin = 30;
        break;
    case 6:
        params.line.dwellMin = 180;
        break;
    case 7:
        params.line.headwayMin = 30;
        break;
    case 8:
        params.line.headwayMin = 300;
        break;
    case 9:
        params.line.tClear = 10;
        break;
    case 10:
        params.line.tClear = 120;
        break;
    case 11:
        params.line.kSlow = 1.0;
        break;
    case 12:
        params.line.kSlow = 2.0;
        break;
    case 13:
        params.line.toDeadEnd = 10;
        break;
    case 14:
        params.line.toDeadEnd = 600;
        break;
    case 15:
        params.line.cabChange = 10;
        break;
    case 16:
        params.line.cabChange = 600;
        break;
    case 17:
        params.line.fromDeadEnd = 10;
        break;
    case 18:
        params.line.fromDeadEnd = 600;
        break;
    case 19:
        params.line.depotOut = 10;
        break;
    case 20:
        params.line.depotOut = 600;
        break;
    case 21:
        params.line.depotIn = 10;
        break;
    case 22:
        params.line.depotIn = 600;
        break;
    case 23:
        params.line.toDeadEnd = 600;
        params.line.cabChange = 600;
        params.line.fromDeadEnd = 600;
        break;
    case 24:
        params.line.signalStopOffsetM = 50.0;
        break;
    case 25:
        params.line.signalStopOffsetM = 300.0;
        break;
    case 26:
        params.line.signalApproach = 10;
        break;
    case 27:
        params.line.signalApproach = 60;
        break;
    case 28:
        params.schedule.delayMin = 0;
        params.schedule.delayMax = 0;
        break;
    case 29:
        params.schedule.delayMin = 600;
        params.schedule.delayMax = 600;
        break;
    case 30:
        params.schedule.delayMin = 0;
        params.schedule.delayMax = 600;
        break;
    case 31:
        params.stepSec = 1;
        break;
    case 32:
        params.stepSec = 240;
        break;
    case 33:
        params.rate = 0.0;
        break;
    case 34:
        params.rate = 1000.0;
        break;
    case 35:
        params.line = lineOf(7);
        break;
    case 36:
        params.line = lineOf(20);
        break;
    case 37:
        params.line.spans[0].tStd = std::nullopt;
        break;
    case 38:
        params.schedule.days[0].windows[0].delayProb = 0.0;
        params.schedule.days[1].windows[0].delayProb = 1.0;
        break;
    case 39:
        params.period.start = 0;
        params.period.end = 12 * kHour;
        break;
    case 40:
        fillDays(params.schedule, windowAt(6 * kHour, 12 * kHour, 2000, 200, 0.0));
        for (ubahn::api::DayProfile& day : params.schedule.days) {
            day.windows.push_back(windowAt(12 * kHour, 24 * kHour, 2000, 200, 0.0));
        }
        break;
    case 41:
        // H equals dwell + t_clear and equals T_turn. Both constraints are >=, not >.
        fillDays(params.schedule, windowAt(6 * kHour, 24 * kHour, 120, 95, 0.0));
        break;
    default:
        QFAIL("unknown bound");
    }

    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QVERIFY2(issues.empty(), qPrintable(issueText(issues)));
}

void ValidationTest::rejectedRanges_data() {
    QTest::addColumn<int>("kind");

    QTest::newRow("length low") << 1;
    QTest::newRow("length high") << 2;
    QTest::newRow("fleet low") << 3;
    QTest::newRow("fleet high") << 4;
    QTest::newRow("dwellMin low") << 5;
    QTest::newRow("dwellMin high") << 6;
    QTest::newRow("headwayMin low") << 7;
    QTest::newRow("headwayMin high") << 8;
    QTest::newRow("tClear low") << 9;
    QTest::newRow("tClear high") << 10;
    QTest::newRow("kSlow low") << 11;
    QTest::newRow("kSlow high") << 12;
    QTest::newRow("toDeadEnd low") << 13;
    QTest::newRow("toDeadEnd high") << 14;
    QTest::newRow("cabChange low") << 15;
    QTest::newRow("cabChange high") << 16;
    QTest::newRow("fromDeadEnd low") << 17;
    QTest::newRow("fromDeadEnd high") << 18;
    QTest::newRow("depotOut low") << 19;
    QTest::newRow("depotOut high") << 20;
    QTest::newRow("depotIn low") << 21;
    QTest::newRow("depotIn high") << 22;
    QTest::newRow("signal low") << 23;
    QTest::newRow("signal high") << 24;
    QTest::newRow("approach low") << 25;
    QTest::newRow("approach high") << 26;
    QTest::newRow("delayMin") << 27;
    QTest::newRow("delayMax") << 28;
    QTest::newRow("rate") << 29;
}

void ValidationTest::rejectedRanges() {
    QFETCH(int, kind);

    struct Expect {
        const char* field;
        const char* message;
    };
    static const Expect kExpect[] = {
        {nullptr, nullptr},
        {"line.spans[2].lengthM", "Длина перегона 300 м вне диапазона 400…5000 м"},
        {"line.spans[2].lengthM", "Длина перегона 5001 м вне диапазона 400…5000 м"},
        {"line.fleetSize", "Размер парка 1 вне диапазона 2…200"},
        {"line.fleetSize", "Размер парка 201 вне диапазона 2…200"},
        {"line.dwellMin", "Минимальная стоянка 29 с вне диапазона 30…180 с"},
        {"line.dwellMin", "Минимальная стоянка 181 с вне диапазона 30…180 с"},
        {"line.headwayMin", "Минимальный интервал прибытия 29 с вне диапазона 30…300 с"},
        {"line.headwayMin", "Минимальный интервал прибытия 301 с вне диапазона 30…300 с"},
        {"line.tClear", "t_clear 9 с вне диапазона 10…120 с"},
        {"line.tClear", "t_clear 121 с вне диапазона 10…120 с"},
        {"line.kSlow", "k_slow 0.5 вне диапазона 1…2"},
        {"line.kSlow", "k_slow 2.5 вне диапазона 1…2"},
        {"line.toDeadEnd", "Время в тупик 9 с вне диапазона 10…600 с"},
        {"line.toDeadEnd", "Время в тупик 601 с вне диапазона 10…600 с"},
        {"line.cabChange", "Смена кабины 9 с вне диапазона 10…600 с"},
        {"line.cabChange", "Смена кабины 601 с вне диапазона 10…600 с"},
        {"line.fromDeadEnd", "Время из тупика 9 с вне диапазона 10…600 с"},
        {"line.fromDeadEnd", "Время из тупика 601 с вне диапазона 10…600 с"},
        {"line.depotOut", "Выдача из депо 9 с вне диапазона 10…600 с"},
        {"line.depotOut", "Выдача из депо 601 с вне диапазона 10…600 с"},
        {"line.depotIn", "Уход в депо 9 с вне диапазона 10…600 с"},
        {"line.depotIn", "Уход в депо 601 с вне диапазона 10…600 с"},
        {"line.signalStopOffsetM", "Точка остановки у сигнала 40 м вне диапазона 50…300 м"},
        {"line.signalStopOffsetM", "Точка остановки у сигнала 350 м вне диапазона 50…300 м"},
        {"line.signalApproach", "Подход от сигнала 5 с вне диапазона 10…60 с"},
        {"line.signalApproach", "Подход от сигнала 70 с вне диапазона 10…60 с"},
        {"schedule.delayMin", "Минимальная задержка -1 с вне диапазона 0…600 с"},
        {"schedule.delayMax", "Максимальная задержка 601 с вне диапазона 0…600 с"},
        {"rate", "Темп -3 меньше 0"},
    };

    ubahn::api::SimParams params = looseParams();
    switch (kind) {
    case 1:
        params.line.spans[2].lengthM = 300.0;
        break;
    case 2:
        params.line.spans[2].lengthM = 5001.0;
        params.line.spans[2].tStd = 100000;
        break;
    case 3:
        params.line.fleetSize = 1;
        break;
    case 4:
        params.line.fleetSize = 201;
        break;
    case 5:
        params.line.dwellMin = 29;
        break;
    case 6:
        params.line.dwellMin = 181;
        break;
    case 7:
        params.line.headwayMin = 29;
        break;
    case 8:
        params.line.headwayMin = 301;
        break;
    case 9:
        params.line.tClear = 9;
        break;
    case 10:
        params.line.tClear = 121;
        break;
    case 11:
        params.line.kSlow = 0.5;
        break;
    case 12:
        params.line.kSlow = 2.5;
        break;
    case 13:
        params.line.toDeadEnd = 9;
        break;
    case 14:
        params.line.toDeadEnd = 601;
        break;
    case 15:
        params.line.cabChange = 9;
        break;
    case 16:
        params.line.cabChange = 601;
        break;
    case 17:
        params.line.fromDeadEnd = 9;
        break;
    case 18:
        params.line.fromDeadEnd = 601;
        break;
    case 19:
        params.line.depotOut = 9;
        break;
    case 20:
        params.line.depotOut = 601;
        break;
    case 21:
        params.line.depotIn = 9;
        break;
    case 22:
        params.line.depotIn = 601;
        break;
    case 23:
        params.line.signalStopOffsetM = 40.0;
        break;
    case 24:
        params.line.signalStopOffsetM = 350.0;
        break;
    case 25:
        params.line.signalApproach = 5;
        break;
    case 26:
        params.line.signalApproach = 70;
        break;
    case 27:
        params.schedule.delayMin = -1;
        break;
    case 28:
        params.schedule.delayMax = 601;
        break;
    case 29:
        params.rate = -3.0;
        break;
    default:
        QFAIL("unknown range");
    }

    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QVERIFY2(issues.size() == 1, qPrintable(issueText(issues)));
    QCOMPARE(QString::fromStdString(issues[0].field), QString::fromUtf8(kExpect[kind].field));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ValueOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].message), QString::fromUtf8(kExpect[kind].message));
}

void ValidationTest::stationCount() {
    ubahn::api::SimParams low = looseParams();
    low.line = lineOf(6);
    const std::vector<ubahn::api::ValidationIssue> lowIssues = ubahn::api::validate(low);
    QCOMPARE(lowIssues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(lowIssues[0].field), QString("line.stationNames"));
    QCOMPARE(lowIssues[0].code, ubahn::api::IssueCode::StationCountOutOfRange);
    QCOMPARE(QString::fromStdString(lowIssues[0].message),
             QString("Число станций 6 вне диапазона 7…20"));

    ubahn::api::SimParams high = looseParams();
    high.line = lineOf(21);
    const std::vector<ubahn::api::ValidationIssue> highIssues = ubahn::api::validate(high);
    QCOMPARE(highIssues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(highIssues[0].field), QString("line.stationNames"));
    QCOMPARE(highIssues[0].code, ubahn::api::IssueCode::StationCountOutOfRange);
    QCOMPARE(QString::fromStdString(highIssues[0].message),
             QString("Число станций 21 вне диапазона 7…20"));
}

void ValidationTest::emptyLineReportsCountAndSpans() {
    ubahn::api::SimParams params = looseParams();
    params.line.stationNames.clear();
    params.line.spans.clear();
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{2});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("line.stationNames"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::StationCountOutOfRange);
    QCOMPARE(QString::fromStdString(issues[1].field), QString("line.spans"));
    QCOMPARE(issues[1].code, ubahn::api::IssueCode::SpanCountMismatch);
    QCOMPARE(QString::fromStdString(issues[1].message), QString("Число перегонов 0 не равно -1"));
}

void ValidationTest::spanCountMismatch() {
    ubahn::api::SimParams params = trainingParams();
    params.line.spans.pop_back();
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("line.spans"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::SpanCountMismatch);
    QCOMPARE(QString::fromStdString(issues[0].message), QString("Число перегонов 6 не равно 7"));
}

void ValidationTest::apiExampleMessages() {
    // API.md 8.3. Turnaround is lowered so headway 100 does not also fail C12;
    // the example lists only these two issues. Emission order is line, then schedule.
    ubahn::api::SimParams params = trainingParams();
    params.line.toDeadEnd = 10;
    params.line.cabChange = 10;
    params.line.fromDeadEnd = 10;
    params.line.spans[1].tStd = 50;
    params.schedule.days[0].windows[1].headway = 100;

    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{2});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("line.spans[1].tStd"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::TStdBelowPhysMin);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Время хода 50 с меньше физического минимума 58 с"));
    QCOMPARE(QString::fromStdString(issues[1].field),
             QString("schedule.days[0].windows[1].headway"));
    QCOMPARE(issues[1].code, ubahn::api::IssueCode::HeadwayBelowDwellPlusClear);
    QCOMPARE(QString::fromStdString(issues[1].message),
             QString("Интервал 100 с меньше стоянки 80 с плюс t_clear 25 с (C11)"));

    const std::vector<ubahn::api::ValidationIssue> scheduleOnly =
        ubahn::api::validateSchedule(params.line, params.schedule);
    QCOMPARE(scheduleOnly.size(), size_t{1});
    QCOMPARE(scheduleOnly[0].code, ubahn::api::IssueCode::HeadwayBelowDwellPlusClear);
}

void ValidationTest::signalOffsetTooLong() {
    ubahn::api::SimParams params = trainingParams();
    params.line.spans[1].lengthM = 200.0;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{2});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("line.spans[1].lengthM"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ValueOutOfRange);
    QCOMPARE(QString::fromStdString(issues[1].field), QString("line.signalStopOffsetM"));
    QCOMPARE(issues[1].code, ubahn::api::IssueCode::SignalOffsetTooLong);
    QCOMPARE(QString::fromStdString(issues[1].message),
             QString("Точка остановки у сигнала 200 м не короче самого короткого перегона 200 м"));
}

void ValidationTest::windowOutOfHours() {
    ubahn::api::SimParams params = trainingParams();
    replaceMonday(params, {windowAt(5 * kHour, 10 * kHour, 180, 60, 0.1),
                           windowAt(10 * kHour, 24 * kHour, 180, 60, 0.1)});
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[0].windows[0]"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::WindowOutOfHours);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Окно 5:00-10:00 выходит за часы работы 6:00-24:00"));
}

void ValidationTest::windowsOverlap() {
    ubahn::api::SimParams params = trainingParams();
    replaceMonday(params, {windowAt(6 * kHour, 12 * kHour, 180, 60, 0.1),
                           windowAt(10 * kHour, 24 * kHour, 180, 60, 0.1)});
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[0].windows[1]"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::WindowsOverlap);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Окно 10:00-24:00 перекрывается с окном 6:00-12:00"));
}

void ValidationTest::windowsOutOfOrder() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.days[1].windows = {windowAt(10 * kHour, 24 * kHour, 180, 60, 0.1),
                                       windowAt(6 * kHour, 10 * kHour, 180, 60, 0.1)};
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[1].windows[1]"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::WindowsOverlap);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Окно 6:00-10:00 начинается раньше предыдущего окна 10:00-24:00"));
}

void ValidationTest::windowsGap() {
    ubahn::api::SimParams params = trainingParams();
    replaceMonday(params, {windowAt(6 * kHour, 12 * kHour, 180, 60, 0.1),
                           windowAt(14 * kHour, 24 * kHour, 180, 60, 0.1)});
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[0].windows[1]"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::WindowsGap);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Разрыв 12:00-14:00 внутри часов работы 6:00-24:00"));
}

void ValidationTest::emptyProfile() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.days[6].windows.clear();
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[6].windows"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::WindowsGap);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Профиль дня пуст: нет окон на 6:00-24:00"));
}

void ValidationTest::degenerateWindow() {
    ubahn::api::SimParams params = trainingParams();
    replaceMonday(params, {windowAt(10 * kHour, 10 * kHour, 180, 60, 0.1)});
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{2});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[0].windows[0].toTod"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ValueOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Конец окна 10:00 не позже начала 10:00"));
    QCOMPARE(QString::fromStdString(issues[1].field), QString("schedule.days[0].windows"));
    QCOMPARE(issues[1].code, ubahn::api::IssueCode::WindowsGap);
    QCOMPARE(QString::fromStdString(issues[1].message),
             QString("Часы работы 6:00-24:00 не покрыты окнами"));
}

void ValidationTest::headwayBelowDwellPlusClear() {
    ubahn::api::SimParams params = trainingParams();
    params.line.toDeadEnd = 10;
    params.line.cabChange = 10;
    params.line.fromDeadEnd = 10;
    params.schedule.days[0].windows[1].headway = 100;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::HeadwayBelowDwellPlusClear);
    QCOMPARE(QString::fromStdString(issues[0].field),
             QString("schedule.days[0].windows[1].headway"));
}

void ValidationTest::headwayBelowTurnaround() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.days[0].windows[4].headway = 100;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field),
             QString("schedule.days[0].windows[4].headway"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::HeadwayBelowTurnaround);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Интервал 100 с меньше времени оборота 120 с (C12)"));
}

void ValidationTest::headwayBelowMin() {
    ubahn::api::SimParams params = trainingParams();
    params.line.toDeadEnd = 10;
    params.line.cabChange = 10;
    params.line.fromDeadEnd = 10;
    params.line.tClear = 10;
    params.line.headwayMin = 100;
    params.schedule.days[0].windows[4].headway = 90;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field),
             QString("schedule.days[0].windows[4].headway"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::HeadwayBelowMin);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Интервал 90 с меньше минимального интервала прибытия 100 с (C6)"));
}

void ValidationTest::dwellBelowMin() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.days[0].windows[4].dwell = 30;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.days[0].windows[4].dwell"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::DwellBelowMin);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Стоянка 30 с меньше минимальной стоянки 40 с (C5)"));
}

void ValidationTest::probabilityOutOfRange() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.days[0].windows[0].delayProb = 1.5;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field),
             QString("schedule.days[0].windows[0].delayProb"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ProbabilityOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Вероятность задержки 1.5 вне диапазона [0, 1]"));

    params = trainingParams();
    params.schedule.days[2].windows[0].delayProb = -0.1;
    const std::vector<ubahn::api::ValidationIssue> negative = ubahn::api::validate(params);
    QCOMPARE(negative.size(), size_t{1});
    QCOMPARE(negative[0].code, ubahn::api::IssueCode::ProbabilityOutOfRange);
    QCOMPARE(QString::fromStdString(negative[0].field),
             QString("schedule.days[2].windows[0].delayProb"));
}

void ValidationTest::delayRangeInvalid() {
    ubahn::api::SimParams params = trainingParams();
    params.schedule.delayMin = 40;
    params.schedule.delayMax = 20;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("schedule.delayMin"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::DelayRangeInvalid);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Минимальная задержка 40 с больше максимальной 20 с"));
}

void ValidationTest::periodEnd() {
    ubahn::api::SimParams params = trainingParams();
    params.period.end = params.period.start;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("period.end"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::PeriodInvalid);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Конец периода 21600 с не позже начала 21600 с"));
}

void ValidationTest::periodStart() {
    ubahn::api::SimParams params = trainingParams();
    params.period.start = -1;
    params.period.end = 100;
    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("period.start"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::PeriodInvalid);
    QCOMPARE(QString::fromStdString(issues[0].message), QString("Начало периода -1 с меньше 0"));
}

void ValidationTest::stepOutOfRange() {
    ubahn::api::SimParams params = trainingParams();
    params.stepSec = 0;
    std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("stepSec"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::StepOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Шаг проигрывания 0 с вне диапазона 1…240 с"));

    params.stepSec = 241;
    issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::StepOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Шаг проигрывания 241 с вне диапазона 1…240 с"));
}

void ValidationTest::severalIssuesKeepOrder() {
    ubahn::api::SimParams params = trainingParams();
    params.line.spans[0].lengthM = 300.0;
    params.line.spans[1].tStd = 50;
    params.line.fleetSize = 1;
    params.schedule.days[0].windows[1].headway = 100;
    params.schedule.days[6].windows.clear();
    params.schedule.delayMin = 80;
    params.schedule.delayMax = 10;
    params.period.start = -5;
    params.period.end = -5;
    params.stepSec = 300;
    params.rate = -2.0;

    const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    const ubahn::api::IssueCode codes[] = {
        ubahn::api::IssueCode::ValueOutOfRange,
        ubahn::api::IssueCode::TStdBelowPhysMin,
        ubahn::api::IssueCode::ValueOutOfRange,
        ubahn::api::IssueCode::HeadwayBelowDwellPlusClear,
        ubahn::api::IssueCode::HeadwayBelowTurnaround,
        ubahn::api::IssueCode::WindowsGap,
        ubahn::api::IssueCode::DelayRangeInvalid,
        ubahn::api::IssueCode::PeriodInvalid,
        ubahn::api::IssueCode::PeriodInvalid,
        ubahn::api::IssueCode::StepOutOfRange,
        ubahn::api::IssueCode::ValueOutOfRange,
    };
    const char* fields[] = {
        "line.spans[0].lengthM",
        "line.spans[1].tStd",
        "line.fleetSize",
        "schedule.days[0].windows[1].headway",
        "schedule.days[0].windows[1].headway",
        "schedule.days[6].windows",
        "schedule.delayMin",
        "period.end",
        "period.start",
        "stepSec",
        "rate",
    };
    QCOMPARE(issues.size(), size_t{11});
    for (size_t i = 0; i < issues.size(); ++i) {
        QCOMPARE(issues[i].code, codes[i]);
        QCOMPARE(QString::fromStdString(issues[i].field), QString::fromUtf8(fields[i]));
    }
    QCOMPARE(QString::fromStdString(issues[1].message),
             QString("Время хода 50 с меньше физического минимума 58 с"));
    QCOMPARE(QString::fromStdString(issues[3].message),
             QString("Интервал 100 с меньше стоянки 80 с плюс t_clear 25 с (C11)"));

    const std::vector<ubahn::api::ValidationIssue> scheduleOnly =
        ubahn::api::validateSchedule(params.line, params.schedule);
    QCOMPARE(scheduleOnly.size(), size_t{4});
    QCOMPARE(scheduleOnly[0].code, ubahn::api::IssueCode::HeadwayBelowDwellPlusClear);
    QCOMPARE(scheduleOnly[1].code, ubahn::api::IssueCode::HeadwayBelowTurnaround);
    QCOMPARE(scheduleOnly[2].code, ubahn::api::IssueCode::WindowsGap);
    QCOMPARE(scheduleOnly[3].code, ubahn::api::IssueCode::DelayRangeInvalid);
}

void ValidationTest::validateScheduleOmitsLinePeriodAndStep() {
    ubahn::api::SimParams params = trainingParams();
    params.line.fleetSize = 0;
    params.line.spans[1].tStd = 50;
    params.period.end = params.period.start;
    params.stepSec = 0;
    params.rate = -1.0;

    const std::vector<ubahn::api::ValidationIssue> scheduleOnly =
        ubahn::api::validateSchedule(params.line, params.schedule);
    QVERIFY(scheduleOnly.empty());

    const std::vector<ubahn::api::ValidationIssue> all = ubahn::api::validate(params);
    QVERIFY(all.size() >= 4);
}

void ValidationTest::nonFiniteValues() {
    ubahn::api::SimParams params = looseParams();
    params.line.spans[0].lengthM = std::numeric_limits<double>::quiet_NaN();
    std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ValueOutOfRange);
    QCOMPARE(QString::fromStdString(issues[0].field), QString("line.spans[0].lengthM"));
    QCOMPARE(QString::fromStdString(issues[0].message),
             QString("Длина перегона nan м вне диапазона 400…5000 м"));

    params = looseParams();
    params.line.kSlow = std::numeric_limits<double>::quiet_NaN();
    issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].message), QString("k_slow nan вне диапазона 1…2"));

    params = looseParams();
    params.schedule.days[0].windows[0].delayProb = std::numeric_limits<double>::quiet_NaN();
    issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ProbabilityOutOfRange);

    params = looseParams();
    params.rate = std::numeric_limits<double>::quiet_NaN();
    issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), size_t{1});
    QCOMPARE(QString::fromStdString(issues[0].field), QString("rate"));
    QCOMPARE(issues[0].code, ubahn::api::IssueCode::ValueOutOfRange);
}

QTEST_APPLESS_MAIN(ValidationTest)

#include "tst_validation.moc"
