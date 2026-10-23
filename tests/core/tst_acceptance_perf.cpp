#include "EngineFixture.h"
#include "api/Functions.h"
#include "core/Check.h"
#include "core/Engine.h"

#include <QElapsedTimer>
#include <QtTest>
#include <string>
#include <vector>

namespace {

ubahn::api::SimParams longDay() {
    ubahn::api::SimParams params = ubahn::core::twentyStationParams();
    // SPEC 14.5: 20 stations, fleet 60, one service day, no deltas.
    params.line.fleetSize = 60;
    params.period.start = ubahn::api::kOpenTod;
    params.period.end = ubahn::api::kCloseTod;
    return params;
}

} // namespace

class AcceptancePerfTest : public QObject {
    Q_OBJECT

private slots:
    void longDayUnderFiveSeconds();
};

void AcceptancePerfTest::longDayUnderFiveSeconds() {
    if (ubahn::core::kCheckInvariants) {
        QSKIP("SPEC 14.5 is measured in Release; Debug skips it");
    }

    const ubahn::api::SimParams params = longDay();
    QCOMPARE(static_cast<int>(params.line.stationNames.size()), 20);
    QVERIFY(ubahn::api::validate(params).empty());

    QElapsedTimer timer;
    timer.start();
    ubahn::core::Engine engine(params, 1);
    engine.runUntil(params.period.end, false);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY(!engine.internalError().has_value());
    QVERIFY2(elapsedMs < 5000, qPrintable(QString("runUntil took %1 ms").arg(elapsedMs)));
}

QTEST_APPLESS_MAIN(AcceptancePerfTest)
#include "tst_acceptance_perf.moc"
