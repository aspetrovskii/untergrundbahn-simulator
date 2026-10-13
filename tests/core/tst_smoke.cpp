#include "core/Check.h"
#include "core/Time.h"

#include <QtTest>
#include <cstdint>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void ceilDiv_data();
    void ceilDiv();
    void floorDiv_data();
    void floorDiv();
    void floorModNegative();
    void roundUpTo();
    void warmupFrom();
    void todAndDowNegative();
    void ceilEpsTolerance();
    void invariantChecksFollowConfig();
};

void SmokeTest::ceilDiv_data() {
    QTest::addColumn<qint64>("value");
    QTest::addColumn<qint64>("divisor");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("zero") << qint64{0} << qint64{5} << qint64{0};
    QTest::newRow("exact") << qint64{10} << qint64{5} << qint64{2};
    QTest::newRow("round up") << qint64{11} << qint64{5} << qint64{3};
    QTest::newRow("153/5") << qint64{153} << qint64{5} << qint64{31};
    QTest::newRow("negative exact") << qint64{-5} << qint64{5} << qint64{-1};
    QTest::newRow("negative toward +inf") << qint64{-6} << qint64{5} << qint64{-1};
    QTest::newRow("negative fraction") << qint64{-1} << qint64{5} << qint64{0};
}

void SmokeTest::ceilDiv() {
    QFETCH(qint64, value);
    QFETCH(qint64, divisor);
    QFETCH(qint64, expected);
    QCOMPARE(ubahn::core::ceilDiv(value, divisor), expected);
}

void SmokeTest::floorDiv_data() {
    QTest::addColumn<qint64>("value");
    QTest::addColumn<qint64>("divisor");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("positive") << qint64{11} << qint64{5} << qint64{2};
    QTest::newRow("exact day") << qint64{-86400} << qint64{86400} << qint64{-1};
    QTest::newRow("just below zero") << qint64{-1} << qint64{86400} << qint64{-1};
    QTest::newRow("previous day") << qint64{-86401} << qint64{86400} << qint64{-2};
    QTest::newRow("negative not exact") << qint64{-6} << qint64{5} << qint64{-2};
}

void SmokeTest::floorDiv() {
    QFETCH(qint64, value);
    QFETCH(qint64, divisor);
    QFETCH(qint64, expected);
    QCOMPARE(ubahn::core::floorDiv(value, divisor), expected);
}

void SmokeTest::floorModNegative() {
    QCOMPARE(ubahn::core::floorMod(qint64{-1}, qint64{86400}), qint64{86399});
    QCOMPARE(ubahn::core::floorMod(qint64{-86400}, qint64{86400}), qint64{0});
    QCOMPARE(ubahn::core::floorMod(qint64{-1}, qint64{7}), qint64{6});
}

void SmokeTest::roundUpTo() {
    QCOMPARE(ubahn::core::roundUpTo(153, 5), qint64{155});
    QCOMPARE(ubahn::core::roundUpTo(150, 5), qint64{150});
    QCOMPARE(ubahn::core::roundUpTo(151, 5), qint64{155});
}

void SmokeTest::warmupFrom() {
    constexpr ubahn::api::SimTime monday0700 = 7 * 3600;
    constexpr ubahn::api::SimTime monday0300 = 3 * 3600;
    constexpr ubahn::api::SimTime monday0200 = 2 * 3600;
    constexpr ubahn::api::SimTime tuesday0200 = 86400 + 2 * 3600;
    constexpr ubahn::api::SimTime sunday0300 = monday0300 - 86400;

    QCOMPARE(ubahn::core::kWarmupTod, 3 * 3600);
    QCOMPARE(ubahn::core::warmupFrom(monday0700), monday0300);
    QCOMPARE(ubahn::core::warmupFrom(tuesday0200), monday0300);
    QCOMPARE(ubahn::core::warmupFrom(monday0300), monday0300);
    QCOMPARE(ubahn::core::warmupFrom(monday0200), sunday0300);
    QCOMPARE(ubahn::core::todOf(sunday0300), 3 * 3600);
    QCOMPARE(ubahn::core::dowOf(sunday0300), 6);
}

void SmokeTest::todAndDowNegative() {
    QCOMPARE(ubahn::core::dayIndex(-1), qint64{-1});
    QCOMPARE(ubahn::core::todOf(-1), 86399);
    QCOMPARE(ubahn::core::dowOf(-1), 6);

    QCOMPARE(ubahn::core::dayIndex(-86400), qint64{-1});
    QCOMPARE(ubahn::core::todOf(-86400), 0);
    QCOMPARE(ubahn::core::dowOf(-86400), 6);

    QCOMPARE(ubahn::core::dayIndex(-86401), qint64{-2});
    QCOMPARE(ubahn::core::todOf(-86401), 86399);
    QCOMPARE(ubahn::core::dowOf(-86401), 5);

    QCOMPARE(ubahn::core::todOf(0), 0);
    QCOMPARE(ubahn::core::dowOf(0), 0);
}

void SmokeTest::ceilEpsTolerance() {
    QCOMPARE(ubahn::core::ceilEps(0.0), qint64{0});
    QCOMPARE(ubahn::core::ceilEps(2.0), qint64{2});
    QCOMPARE(ubahn::core::ceilEps(2.1), qint64{3});
    QCOMPARE(ubahn::core::ceilEps(100.0 * 1.3), qint64{130});
    QCOMPARE(ubahn::core::ceilEps(1.0 + 1e-12), qint64{1});
    QCOMPARE(ubahn::core::ceilEps(1.0 + 1e-7), qint64{2});
}

void SmokeTest::invariantChecksFollowConfig() {
#if defined(UBAHN_CHECK_INVARIANTS)
    QVERIFY(ubahn::core::kCheckInvariants);
#else
    QVERIFY(!ubahn::core::kCheckInvariants);
#endif
    UBAHN_ASSERT(1 + 1 == 2, "arithmetic");
}

QTEST_APPLESS_MAIN(SmokeTest)

#include "tst_smoke.moc"
