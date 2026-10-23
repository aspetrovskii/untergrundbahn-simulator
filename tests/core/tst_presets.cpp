#include "api/Functions.h"

#include <QtTest>
#include <cmath>
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
    QCOMPARE(line.dwellMin, 30);
    QCOMPARE(line.headwayMin, 90);
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

void expectLine(const ubahn::api::LineConfig& line, const std::vector<const char*>& names,
                const std::vector<double>& lengths, const std::vector<int32_t>& tStd) {
    QCOMPARE(line.stationNames.size(), names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        QCOMPARE(QString::fromStdString(line.stationNames[i]), QString(names[i]));
    }
    QCOMPARE(line.spans.size(), lengths.size());
    for (size_t i = 0; i < lengths.size(); ++i) {
        QCOMPARE(line.spans[i].lengthM, lengths[i]);
        QVERIFY(line.spans[i].tStd.has_value());
        QCOMPARE(*line.spans[i].tStd, tStd[i]);
    }
}

double speedKmh(const ubahn::api::SpanInput& span) {
    return span.lengthM * 3.6 / static_cast<double>(*span.tStd);
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
    void shortLineMatches();
    void cityLineMatches();
    void moscowLinesMatch();
    void moscowPeakHeadway();
    void spansRunAtMetroSpeed();
};

void PresetsTest::orderIdsAndSizes() {
    const std::vector<ubahn::api::Preset> presets = ubahn::api::builtinPresets();
    QCOMPARE(presets.size(), size_t{5});

    QCOMPARE(QString::fromStdString(presets[0].id), QString("metro-8"));
    QCOMPARE(QString::fromStdString(presets[1].id), QString("short-4"));
    QCOMPARE(QString::fromStdString(presets[2].id), QString("metro-12"));
    QCOMPARE(QString::fromStdString(presets[3].id), QString("moscow-blue"));
    QCOMPARE(QString::fromStdString(presets[4].id), QString("moscow-green"));
    QCOMPARE(QString::fromStdString(presets[0].name), QString("Учебная, 8 станций"));
    QCOMPARE(QString::fromStdString(presets[1].name), QString("Короткая, 4 станции"));
    QCOMPARE(QString::fromStdString(presets[2].name), QString("Городская, 12 станций"));
    QCOMPARE(QString::fromStdString(presets[3].name), QString("Синяя, 22 станции"));
    QCOMPARE(QString::fromStdString(presets[4].name), QString("Зелёная, 24 станции"));

    const int stationCount[] = {8, 4, 12, 22, 24};
    const int fleet[] = {25, 13, 41, 60, 61};
    for (int i = 0; i < 5; ++i) {
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
    expectLine(ubahn::api::builtinPresets().at(0).params.line,
               {"Сосновая", "Липовая", "Мельничная", "Колокольная", "Торговая", "Ярмарочная",
                "Овражная", "Тополиная"},
               {1450, 1820, 1300, 2150, 1600, 1040, 2380}, {90, 108, 83, 124, 99, 73, 135});
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
    QCOMPARE(params.line.fleetSize, 25);
    expectPlaybackDefaults(params);
}

void PresetsTest::trainingAnalysisMatchesBe04() {
    const ubahn::api::SimParams params = ubahn::api::builtinPresets().at(0).params;
    const ubahn::api::ScheduleAnalysis analysis = ubahn::api::analyze(params.line, params.schedule);
    const int32_t weekdayNeed[] = {15, 25, 17, 25, 8};
    const int32_t weekendNeed[] = {7, 11, 7};

    QCOMPARE(analysis.minFleetForAllWindows, 25);
    QCOMPARE(analysis.tCycle, 2944);
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

void PresetsTest::shortLineMatches() {
    expectLine(ubahn::api::builtinPresets().at(1).params.line,
               {"Маячная", "Рыбачья", "Гончарная", "Кирпичная"}, {1700, 2260, 1380},
               {104, 130, 89});
}

void PresetsTest::moscowLinesMatch() {
    expectLine(ubahn::api::builtinPresets().at(3).params.line, {"Пятницкое шоссе",
                                                                "Митино",
                                                                "Волоколамская",
                                                                "Мякинино",
                                                                "Строгино",
                                                                "Крылатское",
                                                                "Молодёжная",
                                                                "Кунцевская",
                                                                "Славянский бульвар",
                                                                "Парк Победы",
                                                                "Киевская",
                                                                "Смоленская",
                                                                "Арбатская",
                                                                "Площадь Революции",
                                                                "Курская",
                                                                "Бауманская",
                                                                "Электрозаводская",
                                                                "Семёновская",
                                                                "Партизанская",
                                                                "Измайловская",
                                                                "Первомайская",
                                                                "Щёлковская"},
               {1440, 1720, 1180, 3010, 6470, 1940, 2340, 1920, 3290, 3200, 1100,
                1640, 1150, 2330, 2210, 1760, 1110, 1970, 1950, 1740, 1640},
               {90, 102, 78,  160, 333, 112, 130, 111, 173, 169, 74,
                99, 76,  130, 124, 104, 75,  113, 112, 103, 99});
    expectLine(ubahn::api::builtinPresets().at(4).params.line,
               {"Ховрино",       "Беломорская",   "Речной вокзал",     "Водный стадион",
                "Войковская",    "Сокол",         "Аэропорт",          "Динамо",
                "Белорусская",   "Маяковская",    "Тверская",          "Театральная",
                "Новокузнецкая", "Павелецкая",    "Автозаводская",     "Технопарк",
                "Коломенская",   "Каширская",     "Кантемировская",    "Царицыно",
                "Орехово",       "Домодедовская", "Красногвардейская", "Алма-Атинская"},
               {1690, 1260, 1820, 2470, 1940, 1260, 1980, 2160, 1020, 920,  1150, 1910,
                1410, 2920, 1650, 1710, 2820, 2590, 1790, 2020, 1750, 1650, 3090},
               {101, 81,  107, 136, 112, 81,  114, 122, 71,  66, 76, 111,
                88,  156, 99,  102, 152, 141, 105, 116, 103, 99, 164});
}

void PresetsTest::moscowPeakHeadway() {
    const ubahn::api::ScheduleConfig base = ubahn::api::defaultSchedule();
    for (const char* id : {"moscow-blue", "moscow-green"}) {
        const ubahn::api::Preset* preset = nullptr;
        for (const ubahn::api::Preset& item : ubahn::api::builtinPresets()) {
            if (item.id == id) {
                preset = &item;
            }
        }
        QVERIFY(preset != nullptr);
        const ubahn::api::ScheduleConfig& schedule = preset->params.schedule;
        QCOMPARE(schedule.delayMin, base.delayMin);
        QCOMPARE(schedule.delayMax, base.delayMax);
        for (int day = 0; day < 7; ++day) {
            const std::vector<ubahn::api::Window>& windows =
                schedule.days[static_cast<size_t>(day)].windows;
            const std::vector<ubahn::api::Window>& original =
                base.days[static_cast<size_t>(day)].windows;
            QCOMPARE(windows.size(), original.size());
            for (size_t i = 0; i < windows.size(); ++i) {
                const int32_t headway = day < 5 && (i == 1 || i == 3) ? 150 : original[i].headway;
                QCOMPARE(windows[i].headway, headway);
                QCOMPARE(windows[i].fromTod, original[i].fromTod);
                QCOMPARE(windows[i].toTod, original[i].toTod);
                QCOMPARE(windows[i].dwell, original[i].dwell);
                QCOMPARE(windows[i].delayProb, original[i].delayProb);
            }
        }
    }
}

void PresetsTest::cityLineMatches() {
    expectLine(ubahn::api::builtinPresets().at(2).params.line,
               {"Кедровая", "Янтарная", "Сенокосная", "Пасечная", "Ветряная", "Часовая", "Ратушная",
                "Типографская", "Фонтанная", "Каретная", "Лодочная", "Затонная"},
               {3850, 1560, 2040, 1350, 1780, 1180, 2290, 1630, 1920, 1410, 4200},
               {198, 98, 121, 87, 108, 80, 132, 101, 114, 90, 216});
}

// Scheduled speed 50..70 km/h, 70 only from 3.5 km; tMin is the physical minimum, so
// catch-up is bounded by 80 km/h and not by the 1.5 coefficient.
void PresetsTest::spansRunAtMetroSpeed() {
    for (const ubahn::api::Preset& preset : ubahn::api::builtinPresets()) {
        const ubahn::api::LineConfig& line = preset.params.line;
        int fastSpans = 0;
        for (size_t i = 0; i < line.spans.size(); ++i) {
            const ubahn::api::SpanInput& span = line.spans[i];
            const QByteArray where = QByteArray::fromStdString(preset.id) + " span " +
                                     QByteArray::number(static_cast<int>(i));
            QVERIFY2(span.tStd.has_value(), where.constData());
            QCOMPARE(std::fmod(span.lengthM, 10.0), 0.0);
            const double speed = speedKmh(span);
            const int32_t phys = ubahn::api::physMinRunTime(span.lengthM);
            QVERIFY2(*span.tStd >= phys, where.constData());
            // A span that cannot average 50 km/h keeps the physical minimum. Every other span
            // stays in 50..70, and 70 km/h is only scheduled from about 3.5 km.
            if (speed < 50.0) {
                QCOMPARE(*span.tStd, phys);
            } else {
                QVERIFY2(speed <= 70.0 + 1e-9, where.constData());
            }
            if (speed > 69.0) {
                ++fastSpans;
                QVERIFY2(span.lengthM >= 3500.0, where.constData());
            }
            QCOMPARE(ubahn::api::minRunTime(line, static_cast<int>(i)), phys);
        }
        const int expectedFast = preset.id == "metro-12" ? 2 : preset.id == "moscow-blue" ? 1 : 0;
        QCOMPARE(fastSpans, expectedFast);
    }
}

QTEST_APPLESS_MAIN(PresetsTest)

#include "tst_presets.moc"
