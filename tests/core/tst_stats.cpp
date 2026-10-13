#include "core/Stats.h"
#include "core/Time.h"

#include <QtTest>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;

ubahn::core::Stats makeStats(ubahn::api::SimTime start, ubahn::api::SimTime end, int nStations) {
    return ubahn::core::Stats(ubahn::api::SimPeriod{start, end}, nStations);
}

void expectDouble(double actual, double expected) {
    QVERIFY(std::abs(actual - expected) < 1e-9);
}

void expectHeadway(const ubahn::api::HeadwayStat& stat, qint64 count, double mean, qint32 maxSec) {
    QCOMPARE(stat.count, static_cast<qint64>(count));
    QCOMPARE(stat.maxSec, maxSec);
    expectDouble(stat.meanSec, mean);
}

void expectEpisode(const ubahn::api::EpisodeStat& stat, qint64 count, double mean, qint32 maxSec) {
    QCOMPARE(stat.count, static_cast<qint64>(count));
    QCOMPARE(stat.maxSec, maxSec);
    expectDouble(stat.meanSec, mean);
}

void expectAdded(const ubahn::api::AddedTimeStat& stat, qint64 count, qint64 totalSec) {
    QCOMPARE(stat.count, static_cast<qint64>(count));
    QCOMPARE(stat.totalSec, static_cast<qint64>(totalSec));
}

} // namespace

class StatsTest : public QObject {
    Q_OBJECT

private slots:
    void outsideWindowIgnored();
    void delayShareAndMeanSkipZeros();
    void headwayByStationDirAndTotal();
    void eveningToMorningExcluded();
    void latenessEpisode();
    void regulationCounters();
    void foldCopiesStopAndStretches();
    void headwayRowsMatchPhysicalStations();
};

void StatsTest::outsideWindowIgnored() {
    // Period [08:00, 10:00] on Monday. Hours of service are [06:00, 24:00).
    ubahn::core::Stats stats = makeStats(8 * kHour, 10 * kHour, 2);
    const ubahn::api::SimTime before = 8 * kHour - 1;
    const ubahn::api::SimTime opening = 8 * kHour;
    const ubahn::api::SimTime closing = 10 * kHour;
    const ubahn::api::SimTime after = 10 * kHour + 1;

    stats.onStop(ubahn::api::Dir::I, 0, before, 40);
    stats.onStop(ubahn::api::Dir::I, 0, after, 40);
    stats.onArrival(ubahn::api::Dir::I, 0, before);
    stats.onArrival(ubahn::api::Dir::I, 0, after);
    stats.onLateness(0, 500, before);
    stats.onHold(80, after);
    stats.onSlowdown(80, before);
    stats.onSignalStop(80, after);

    stats.onStop(ubahn::api::Dir::I, 0, opening, 10);
    stats.onStop(ubahn::api::Dir::II, 1, closing, 0);
    stats.onArrival(ubahn::api::Dir::I, 0, opening);
    stats.onArrival(ubahn::api::Dir::I, 0, opening + 100);

    const ubahn::api::SimSummary summary = stats.fold(closing, true);
    QCOMPARE(summary.delayCount[0], static_cast<qint64>(1));
    QCOMPARE(summary.delayCount[1], static_cast<qint64>(0));
    QCOMPARE(summary.stopCount[0], static_cast<qint64>(1));
    QCOMPARE(summary.stopCount[1], static_cast<qint64>(1));
    expectHeadway(summary.headwayByStation[0][0], 1, 100.0, 100);
    QCOMPARE(summary.maxLatenessSec, 0);
    expectAdded(summary.holds, 0, 0);
    expectAdded(summary.slowdowns, 0, 0);
    expectAdded(summary.signalStops, 0, 0);

    // Inside the period but before opening: not passenger traffic (SPEC 6.2).
    ubahn::core::Stats early = makeStats(3 * kHour, 12 * kHour, 1);
    early.onStop(ubahn::api::Dir::I, 0, 5 * kHour, 30);
    early.onStop(ubahn::api::Dir::I, 0, 6 * kHour - 1, 30);
    early.onArrival(ubahn::api::Dir::I, 0, 5 * kHour);
    early.onArrival(ubahn::api::Dir::I, 0, 6 * kHour);
    early.onArrival(ubahn::api::Dir::I, 0, 6 * kHour + 50);
    early.onLateness(1, 900, 4 * kHour);
    early.onHold(12, 5 * kHour);

    const ubahn::api::SimSummary opened = early.fold(12 * kHour, false);
    QCOMPARE(opened.delayCount[0], static_cast<qint64>(0));
    QCOMPARE(opened.stopCount[0], static_cast<qint64>(0));
    QCOMPARE(opened.maxLatenessSec, 0);
    expectAdded(opened.holds, 0, 0);
    // 05:00 must not seed the headway. 06:00 is the first sample of the service day.
    expectHeadway(opened.headwayByStation[0][0], 1, 50.0, 50);

    // 24:00 is the next midnight, outside [06:00, 24:00).
    ubahn::core::Stats midnight = makeStats(20 * kHour, 30 * kHour, 1);
    midnight.onStop(ubahn::api::Dir::II, 0, 24 * kHour - 1, 7);
    midnight.onStop(ubahn::api::Dir::II, 0, 24 * kHour, 7);
    midnight.onArrival(ubahn::api::Dir::II, 0, 22 * kHour);
    midnight.onArrival(ubahn::api::Dir::II, 0, 24 * kHour);
    midnight.onArrival(ubahn::api::Dir::II, 0, 24 * kHour + 6 * kHour);
    const ubahn::api::SimSummary night = midnight.fold(30 * kHour, true);
    QCOMPARE(night.delayCount[1], static_cast<qint64>(1));
    QCOMPARE(night.stopCount[1], static_cast<qint64>(1));
    expectHeadway(night.headwayByStation[1][0], 0, 0.0, 0);
}

void StatsTest::delayShareAndMeanSkipZeros() {
    ubahn::core::Stats stats = makeStats(6 * kHour, 24 * kHour, 2);
    stats.onStop(ubahn::api::Dir::I, 0, 8 * kHour, 10);
    stats.onStop(ubahn::api::Dir::I, 0, 9 * kHour, 0);
    stats.onStop(ubahn::api::Dir::I, 1, 10 * kHour, 30);
    stats.onStop(ubahn::api::Dir::I, 1, 11 * kHour, -5);
    stats.onStop(ubahn::api::Dir::II, 0, 12 * kHour, 20);
    stats.onStop(ubahn::api::Dir::II, 1, 13 * kHour, 0);

    const ubahn::api::SimSummary summary = stats.fold(18 * kHour, true);
    QCOMPARE(summary.delayCount[0], static_cast<qint64>(2));
    QCOMPARE(summary.delayCount[1], static_cast<qint64>(1));
    QCOMPARE(summary.stopCount[0], static_cast<qint64>(4));
    QCOMPARE(summary.stopCount[1], static_cast<qint64>(2));
    // Σ delayCount / Σ stopCount = 3/6. Mean is over the positive draws only: (10+30+20)/3.
    expectDouble(summary.delayShare, 0.5);
    expectDouble(summary.delayMeanSec, 20.0);
}

void StatsTest::headwayByStationDirAndTotal() {
    ubahn::core::Stats stats = makeStats(6 * kHour, 24 * kHour, 3);
    const ubahn::api::SimTime t6 = 6 * kHour;
    stats.onArrival(ubahn::api::Dir::I, 0, t6);
    stats.onArrival(ubahn::api::Dir::I, 0, t6 + 600);
    stats.onArrival(ubahn::api::Dir::I, 0, t6 + 600 + 1800);
    stats.onArrival(ubahn::api::Dir::I, 1, 7 * kHour);
    stats.onArrival(ubahn::api::Dir::I, 1, 7 * kHour + 300);
    stats.onArrival(ubahn::api::Dir::I, 2, 8 * kHour);
    stats.onArrival(ubahn::api::Dir::II, 2, 8 * kHour);
    stats.onArrival(ubahn::api::Dir::II, 2, 8 * kHour + 1200);
    stats.onArrival(ubahn::api::Dir::II, 2, 9 * kHour);

    const ubahn::api::SimSummary summary = stats.fold(12 * kHour, true);
    QCOMPARE(summary.headwayByStation[0].size(), static_cast<std::size_t>(3));
    QCOMPARE(summary.headwayByStation[1].size(), static_cast<std::size_t>(3));
    expectHeadway(summary.headwayByStation[0][0], 2, 1200.0, 1800);
    expectHeadway(summary.headwayByStation[0][1], 1, 300.0, 300);
    expectHeadway(summary.headwayByStation[0][2], 0, 0.0, 0);
    expectHeadway(summary.headwayByStation[1][0], 0, 0.0, 0);
    expectHeadway(summary.headwayByStation[1][1], 0, 0.0, 0);
    expectHeadway(summary.headwayByStation[1][2], 2, 1800.0, 2400);
    // Pooled mean, not the mean of the station means: (600+1800+300)/3 = 900.
    expectHeadway(summary.headwayByDir[0], 3, 900.0, 1800);
    expectHeadway(summary.headwayByDir[1], 2, 1800.0, 2400);
    expectHeadway(summary.headwayTotal, 5, 1260.0, 2400);
}

void StatsTest::eveningToMorningExcluded() {
    // Monday 06:00 through Tuesday noon. Decision R15.
    const ubahn::api::SimTime mondayOpen = 6 * kHour;
    const ubahn::api::SimTime tuesdayOpen = ubahn::api::kSecondsPerDay + 6 * kHour;
    ubahn::core::Stats stats = makeStats(mondayOpen, tuesdayOpen + 6 * kHour, 1);

    stats.onArrival(ubahn::api::Dir::I, 0, mondayOpen);
    stats.onArrival(ubahn::api::Dir::I, 0, 23 * kHour);
    stats.onArrival(ubahn::api::Dir::I, 0, 23 * kHour + 30 * 60);
    stats.onArrival(ubahn::api::Dir::I, 0, tuesdayOpen);
    stats.onArrival(ubahn::api::Dir::I, 0, tuesdayOpen + 600);

    const ubahn::api::SimSummary summary = stats.fold(tuesdayOpen + 6 * kHour, true);
    // Monday: 23:00-06:00 and 23:30-23:00. Tuesday's first arrival adds nothing;
    // 06:10-06:00 is the only Tuesday sample.
    const int32_t mondayLong = 17 * kHour;
    const int32_t mondayShort = 30 * 60;
    expectHeadway(summary.headwayByStation[0][0], 3, (mondayLong + mondayShort + 600) / 3.0,
                  mondayLong);
    expectHeadway(summary.headwayTotal, 3, (mondayLong + mondayShort + 600) / 3.0, mondayLong);
    QVERIFY(ubahn::core::dayIndex(23 * kHour) != ubahn::core::dayIndex(tuesdayOpen));
}

void StatsTest::latenessEpisode() {
    ubahn::core::Stats stats = makeStats(6 * kHour, 2 * ubahn::api::kSecondsPerDay, 1);
    const ubahn::api::SimTime t8 = 8 * kHour;
    stats.onLateness(0, 0, t8);
    stats.onLateness(0, 30, t8);
    stats.onLateness(0, 50, t8 + kHour);
    stats.onLateness(0, 0, t8 + 2 * kHour);
    stats.onDeparture(0, 10, t8 + 2 * kHour);
    stats.onDeparture(0, -1, t8 + 3 * kHour);
    stats.onDeparture(0, 0, t8 + 3 * kHour);

    // Off-window peak must not raise the maximum or open an episode.
    stats.onLateness(1, 1000, 5 * kHour);
    stats.onDeparture(1, 0, 7 * kHour);

    stats.onLateness(1, 15, 18 * kHour);
    const ubahn::api::SimTime tuesdayClose = ubahn::api::kSecondsPerDay + 6 * kHour + 30 * 60;
    stats.onDeparture(1, 0, tuesdayClose);

    // A second episode of train 0 starts only after the first one closed.
    stats.onLateness(0, 5, 20 * kHour);
    stats.onDeparture(0, 0, 21 * kHour);

    // Still open: present in the maximum, absent from recovery.
    stats.onLateness(2, 70, 16 * kHour);

    const ubahn::api::SimSummary summary = stats.fold(22 * kHour, false);
    QCOMPARE(summary.maxLatenessSec, 70);
    const qint64 first = 3 * kHour;
    const qint64 overnight = tuesdayClose - 18 * kHour;
    const qint64 second = kHour;
    expectEpisode(summary.recovery, 3, (first + overnight + second) / 3.0,
                  static_cast<qint32>(overnight));
}

void StatsTest::regulationCounters() {
    ubahn::core::Stats stats = makeStats(6 * kHour, 24 * kHour, 1);
    stats.onHold(30, 8 * kHour);
    stats.onHold(10, 9 * kHour);
    stats.onHold(0, 10 * kHour);
    stats.onHold(5, 5 * kHour);
    stats.onHold(5, 24 * kHour + 1);

    stats.onSlowdown(40, 11 * kHour);
    stats.onSlowdown(0, 12 * kHour);
    stats.onSlowdown(8, 3 * kHour);

    stats.onSignalStop(15, 13 * kHour);
    stats.onSignalStop(25, 14 * kHour);
    stats.onSignalStop(-3, 15 * kHour);

    const ubahn::api::SimSummary summary = stats.fold(16 * kHour, true);
    expectAdded(summary.holds, 2, 40);
    expectAdded(summary.slowdowns, 1, 40);
    expectAdded(summary.signalStops, 2, 40);
}

void StatsTest::foldCopiesStopAndStretches() {
    ubahn::core::Stats stats = makeStats(6 * kHour, 20 * kHour, 1);
    stats.onStop(ubahn::api::Dir::I, 0, 7 * kHour, 4);
    const std::vector<ubahn::api::StretchInfo> stretches = {
        {0, 1, 120, 150},
        {3, 0, 180, 200},
    };
    stats.setStretchedWindows(stretches);

    const ubahn::api::SimSummary stopped = stats.fold(9 * kHour, false);
    const ubahn::api::SimSummary finished = stats.fold(20 * kHour, true);

    QCOMPARE(stopped.period.start, static_cast<qint64>(6 * kHour));
    QCOMPARE(stopped.period.end, static_cast<qint64>(20 * kHour));
    QCOMPARE(stopped.stoppedAt, static_cast<qint64>(9 * kHour));
    QVERIFY(!stopped.completed);
    QCOMPARE(finished.stoppedAt, static_cast<qint64>(20 * kHour));
    QVERIFY(finished.completed);
    QCOMPARE(stopped.delayCount[0], finished.delayCount[0]);
    QCOMPARE(finished.delayCount[0], static_cast<qint64>(1));

    QCOMPARE(stopped.stretchedWindows.size(), static_cast<std::size_t>(2));
    QCOMPARE(stopped.stretchedWindows[0].dayOfWeek, 0);
    QCOMPARE(stopped.stretchedWindows[0].windowIndex, 1);
    QCOMPARE(stopped.stretchedWindows[0].planned, 120);
    QCOMPARE(stopped.stretchedWindows[0].effective, 150);
    QCOMPARE(stopped.stretchedWindows[1].dayOfWeek, 3);
    QCOMPARE(finished.stretchedWindows[1].effective, 200);

    stats.setStretchedWindows({});
    const ubahn::api::SimSummary cleared = stats.fold(9 * kHour, false);
    QVERIFY(cleared.stretchedWindows.empty());
    QCOMPARE(cleared.delayCount[0], static_cast<qint64>(1));
}

void StatsTest::headwayRowsMatchPhysicalStations() {
    const ubahn::api::SimSummary empty = makeStats(6 * kHour, 12 * kHour, 4).fold(0, false);
    QCOMPARE(empty.headwayByStation[0].size(), static_cast<std::size_t>(4));
    QCOMPARE(empty.headwayByStation[1].size(), static_cast<std::size_t>(4));
    expectHeadway(empty.headwayTotal, 0, 0.0, 0);

    const ubahn::api::SimSummary none = makeStats(6 * kHour, 12 * kHour, 0).fold(0, true);
    QVERIFY(none.headwayByStation[0].empty());
    QVERIFY(none.headwayByStation[1].empty());

    ubahn::core::Stats stats = makeStats(6 * kHour, 12 * kHour, 4);
    stats.onArrival(ubahn::api::Dir::II, 3, 9 * kHour);
    stats.onArrival(ubahn::api::Dir::II, 3, 9 * kHour + 90);
    stats.onArrival(ubahn::api::Dir::II, 0, 9 * kHour);
    const ubahn::api::SimSummary summary = stats.fold(12 * kHour, true);
    QCOMPARE(summary.headwayByStation[1].size(), static_cast<std::size_t>(4));
    expectHeadway(summary.headwayByStation[1][3], 1, 90.0, 90);
    expectHeadway(summary.headwayByStation[1][0], 0, 0.0, 0);
    expectHeadway(summary.headwayByStation[0][3], 0, 0.0, 0);
}

QTEST_APPLESS_MAIN(StatsTest)
#include "tst_stats.moc"
