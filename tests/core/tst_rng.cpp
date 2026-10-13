#include "core/Rng.h"

#include <QtTest>
#include <cstdint>

class RngTest : public QObject {
    Q_OBJECT

private slots:
    void sameSeedSameSequence();
    void mt19937_64_10000th();
    void uniformIntInclusive();
    void uniformIntRoughlyUniform();
    void uniform01InHalfOpenUnitInterval();
    void goldenDrawsForSeed42();
    void randomSeedDiffersAcrossCalls();
};

void RngTest::sameSeedSameSequence() {
    ubahn::core::Rng left(42);
    ubahn::core::Rng right(42);
    for (int i = 0; i < 1000; ++i) {
        QCOMPARE(left.next(), right.next());
    }

    ubahn::core::Rng leftInt(42);
    ubahn::core::Rng rightInt(42);
    for (int i = 0; i < 1000; ++i) {
        QCOMPARE(leftInt.uniformInt(0, 600), rightInt.uniformInt(0, 600));
    }

    ubahn::core::Rng leftUnit(42);
    ubahn::core::Rng rightUnit(42);
    for (int i = 0; i < 1000; ++i) {
        QCOMPARE(leftUnit.uniform01(), rightUnit.uniform01());
    }
}

void RngTest::mt19937_64_10000th() {
    ubahn::core::Rng rng(5489);
    uint64_t value = 0;
    for (int i = 0; i < 10000; ++i) {
        value = rng.next();
    }
    QCOMPARE(value, static_cast<uint64_t>(9981545732273789042ULL));
}

void RngTest::uniformIntInclusive() {
    ubahn::core::Rng rng(42);
    for (int i = 0; i < 100; ++i) {
        QCOMPARE(rng.uniformInt(7, 7), 7);
        QCOMPARE(rng.uniformInt(0, 0), 0);
        QCOMPARE(rng.uniformInt(600, 600), 600);
    }

    bool sawZero = false;
    bool saw600 = false;
    bool sawLow = false;
    bool sawHigh = false;
    for (int i = 0; i < 20000; ++i) {
        const int32_t wide = rng.uniformInt(0, 600);
        QVERIFY(wide >= 0);
        QVERIFY(wide <= 600);
        sawZero = sawZero || wide == 0;
        saw600 = saw600 || wide == 600;

        const int32_t narrow = rng.uniformInt(20, 60);
        QVERIFY(narrow >= 20);
        QVERIFY(narrow <= 60);
        sawLow = sawLow || narrow == 20;
        sawHigh = sawHigh || narrow == 60;
    }
    QVERIFY(sawZero);
    QVERIFY(saw600);
    QVERIFY(sawLow);
    QVERIFY(sawHigh);
}

void RngTest::uniformIntRoughlyUniform() {
    constexpr int kBins = 10;
    constexpr int kSamples = 20000;
    ubahn::core::Rng rng(42);
    int counts[kBins] = {};
    for (int i = 0; i < kSamples; ++i) {
        const int32_t value = rng.uniformInt(0, kBins - 1);
        QVERIFY(value >= 0);
        QVERIFY(value < kBins);
        ++counts[value];
    }

    const double expected = static_cast<double>(kSamples) / static_cast<double>(kBins);
    double chi2 = 0.0;
    for (int count : counts) {
        const double delta = static_cast<double>(count) - expected;
        chi2 += delta * delta / expected;
    }
    // df = 9. The 0.999 critical value is about 27.9; the allowance is deliberately wide.
    QVERIFY(chi2 < 50.0);
}

void RngTest::uniform01InHalfOpenUnitInterval() {
    ubahn::core::Rng rng(42);
    for (int i = 0; i < 10000; ++i) {
        const double value = rng.uniform01();
        QVERIFY(value >= 0.0);
        QVERIFY(value < 1.0);
    }
}

void RngTest::goldenDrawsForSeed42() {
    const int32_t expectedInt[] = {42, 55, 48, 33, 23};
    ubahn::core::Rng ints(42);
    for (int32_t expected : expectedInt) {
        QCOMPARE(ints.uniformInt(20, 60), expected);
    }

    // First five uniform01 draws from Rng(42), independent of the integer stream above.
    const double expectedUnit[] = {0x1.82a3befaddcbcp-1, 0x1.472f1f73724ap-1, 0x1.81192cfe1cbcfp-1,
                                   0x1.171621fc50d68p-3, 0x1.ce79451c9a6c3p-1};
    ubahn::core::Rng unit(42);
    for (double expected : expectedUnit) {
        const double value = unit.uniform01();
        QVERIFY(value == expected);
    }
}

void RngTest::randomSeedDiffersAcrossCalls() {
    const uint64_t first = ubahn::core::randomSeed();
    const uint64_t second = ubahn::core::randomSeed();
    QVERIFY(first != second);
}

QTEST_APPLESS_MAIN(RngTest)

#include "tst_rng.moc"
