#include "api/Functions.h"
#include "core/Line.h"

#include <QtTest>
#include <cmath>

namespace {

ubahn::api::LineConfig trainingConfig() {
    // SPEC 12. Path II is the same spans in reverse; names do not affect the times.
    ubahn::api::LineConfig cfg;
    cfg.stationNames = {"Северная",    "Полевая", "Заводская", "Университет",
                        "Центральная", "Рынок",   "Парк",      "Южная"};
    const double lengths[] = {1100, 780, 1700, 950, 1500, 860, 1400};
    const int32_t tStds[] = {120, 90, 130, 100, 110, 95, 125};
    for (int i = 0; i < 7; ++i) {
        ubahn::api::SpanInput span;
        span.lengthM = lengths[i];
        span.tStd = tStds[i];
        cfg.spans.push_back(span);
    }
    cfg.kSlow = 1.3;
    return cfg;
}

} // namespace

class LineTest : public QObject {
    Q_OBJECT

private slots:
    void tPhysOfShortSpan();
    void tPhysContinuousAtCoastThreshold();
    void physMinRunTime_data();
    void physMinRunTime();
    void defaultStdRunTime();
    void trainingLineBounds();
    void emptyTStdUsesDefault();
    void runTimeIndexOutOfRange();
    void spanOfReversesPathII();
    void stationAndPosAreInverse();
};

void LineTest::tPhysOfShortSpan() {
    QCOMPARE(ubahn::core::tPhys(400.0), 40.0);
}

void LineTest::tPhysContinuousAtCoastThreshold() {
    constexpr double kVMaxMps = 80.0 / 3.6;
    constexpr double kAccelMps2 = 1.0;
    const double sAcc = (kVMaxMps * kVMaxMps) / (2.0 * kAccelMps2);
    const double length = 2.0 * sAcc;
    QVERIFY(length > 493.8);
    QVERIFY(length < 493.9);

    const double shortForm = 2.0 * std::sqrt(length / kAccelMps2);
    const double longForm = (2.0 * kVMaxMps) / kAccelMps2;
    QCOMPARE(shortForm, longForm);
    QCOMPARE(ubahn::core::tPhys(length), longForm);

    const double below = ubahn::core::tPhys(length - 1.0);
    const double above = ubahn::core::tPhys(length + 1.0);
    QVERIFY(below < ubahn::core::tPhys(length));
    QVERIFY(ubahn::core::tPhys(length) < above);
    QVERIFY(above - below < 0.2);
}

void LineTest::physMinRunTime_data() {
    QTest::addColumn<double>("lengthM");
    QTest::addColumn<int>("expected");

    QTest::newRow("780") << 780.0 << 58;
    QTest::newRow("1100") << 1100.0 << 72;
    QTest::newRow("1700") << 1700.0 << 99;
    QTest::newRow("950") << 950.0 << 65;
    QTest::newRow("1500") << 1500.0 << 90;
    QTest::newRow("860") << 860.0 << 61;
    QTest::newRow("1400") << 1400.0 << 86;
    QTest::newRow("400") << 400.0 << 40;
    // Card lists 5000 without a number. ALGORITHM 3.1 gives ⌈247.22⌉ = 248.
    QTest::newRow("5000") << 5000.0 << 248;
}

void LineTest::physMinRunTime() {
    QFETCH(double, lengthM);
    QFETCH(int, expected);
    QCOMPARE(static_cast<int>(ubahn::api::physMinRunTime(lengthM)), expected);
}

void LineTest::defaultStdRunTime() {
    QCOMPARE(ubahn::api::defaultStdRunTime(780.0), 90);
    QCOMPARE(ubahn::api::defaultStdRunTime(400.0), 60);
}

void LineTest::trainingLineBounds() {
    const int32_t tMinExpected[] = {80, 60, 99, 67, 90, 64, 86};
    // 100 × 1.3 is 130, not 131 (ceilEps). 95 × 1.3 and 125 × 1.3 round up.
    const int32_t tMaxSlow[] = {156, 117, 169, 130, 143, 124, 163};
    const int32_t tMaxUnity[] = {120, 90, 130, 100, 110, 95, 125};
    const int32_t tMaxDouble[] = {240, 180, 260, 200, 220, 190, 250};
    const double kSlows[] = {1.3, 1.0, 2.0};
    const int32_t* tMaxBySlow[] = {tMaxSlow, tMaxUnity, tMaxDouble};

    for (int k = 0; k < 3; ++k) {
        ubahn::api::LineConfig cfg = trainingConfig();
        cfg.kSlow = kSlows[k];
        const ubahn::core::Line line = ubahn::core::buildLine(cfg);
        QCOMPARE(line.n, 8);
        QCOMPARE(static_cast<int>(line.spans.size()), 7);
        for (int i = 0; i < 7; ++i) {
            const ubahn::core::Span& span = line.spans[static_cast<size_t>(i)];
            QCOMPARE(span.lengthM, cfg.spans[static_cast<size_t>(i)].lengthM);
            QCOMPARE(span.tStd, *cfg.spans[static_cast<size_t>(i)].tStd);
            QCOMPARE(span.tMin, tMinExpected[i]);
            QCOMPARE(span.tMax, tMaxBySlow[k][i]);
            QCOMPARE(ubahn::api::minRunTime(cfg, i), tMinExpected[i]);
            QCOMPARE(ubahn::api::maxRunTime(cfg, i), tMaxBySlow[k][i]);
        }
    }
}

void LineTest::emptyTStdUsesDefault() {
    ubahn::api::LineConfig cfg;
    cfg.stationNames = {"A", "B", "C"};
    ubahn::api::SpanInput first;
    first.lengthM = 780.0;
    ubahn::api::SpanInput second;
    second.lengthM = 400.0;
    cfg.spans = {first, second};

    const ubahn::core::Line line = ubahn::core::buildLine(cfg);
    QCOMPARE(line.spans.at(0).tStd, 90);
    QCOMPARE(line.spans.at(0).tMin, 60);
    QCOMPARE(line.spans.at(0).tMax, 117);
    QCOMPARE(line.spans.at(1).tStd, 60);
    QCOMPARE(line.spans.at(1).tMin, 40);
    QCOMPARE(line.spans.at(1).tMax, 78);
    QVERIFY(!line.cfg.spans.at(0).tStd.has_value());
}

void LineTest::runTimeIndexOutOfRange() {
    const ubahn::api::LineConfig cfg = trainingConfig();
    QCOMPARE(ubahn::api::minRunTime(cfg, -1), 0);
    QCOMPARE(ubahn::api::maxRunTime(cfg, -1), 0);
    QCOMPARE(ubahn::api::minRunTime(cfg, 7), 0);
    QCOMPARE(ubahn::api::maxRunTime(cfg, 7), 0);
}

void LineTest::spanOfReversesPathII() {
    const ubahn::core::Line line = ubahn::core::buildLine(trainingConfig());
    for (int pos = 0; pos < line.n - 1; ++pos) {
        const ubahn::core::Span& forward = ubahn::core::spanOf(line, ubahn::api::Dir::I, pos);
        QCOMPARE(&forward, &line.spans[static_cast<size_t>(pos)]);
        const ubahn::core::Span& back = ubahn::core::spanOf(line, ubahn::api::Dir::II, pos);
        QCOMPARE(&back, &line.spans[static_cast<size_t>(line.n - 2 - pos)]);
    }
}

void LineTest::stationAndPosAreInverse() {
    const ubahn::core::Line line = ubahn::core::buildLine(trainingConfig());
    const ubahn::api::Dir dirs[] = {ubahn::api::Dir::I, ubahn::api::Dir::II};
    for (ubahn::api::Dir dir : dirs) {
        for (int pos = 0; pos < line.n; ++pos) {
            const int station = ubahn::core::stationOf(line, dir, pos);
            QCOMPARE(ubahn::core::posOf(line, dir, station), pos);
            if (dir == ubahn::api::Dir::I) {
                QCOMPARE(station, pos);
            } else {
                QCOMPARE(station, line.n - 1 - pos);
            }
        }
        for (int station = 0; station < line.n; ++station) {
            const int pos = ubahn::core::posOf(line, dir, station);
            QCOMPARE(ubahn::core::stationOf(line, dir, pos), station);
            if (dir == ubahn::api::Dir::I) {
                QCOMPARE(pos, station);
            } else {
                QCOMPARE(pos, line.n - 1 - station);
            }
        }
    }
}

QTEST_APPLESS_MAIN(LineTest)

#include "tst_line.moc"
