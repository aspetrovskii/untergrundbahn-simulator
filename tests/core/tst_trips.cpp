#include "core/Schedule.h"
#include "core/Time.h"
#include "core/Trips.h"

#include <QtTest>
#include <cstdint>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;

ubahn::core::Line trainingLine() {
    ubahn::core::Line line;
    line.n = 8;
    line.cfg.fleetSize = 26;
    // SPEC 12. Path II walks these spans from the other end.
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

// SPEC 6.3. Monday..Friday share one profile, Saturday and Sunday the other.
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

void fillDay(std::vector<ubahn::api::WindowAnalysis>& rows, const int32_t* headways, int count) {
    rows.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        rows[static_cast<size_t>(i)].hEff = headways[i];
    }
}

// Hand-built analysis: fleet covers every window, so hEff is the planned headway.
ubahn::api::ScheduleAnalysis coveredAnalysis() {
    ubahn::api::ScheduleAnalysis analysis;
    const int32_t weekday[] = {180, 120, 160, 120, 300};
    const int32_t weekend[] = {360, 240, 360};
    for (int day = 0; day < 5; ++day) {
        fillDay(analysis.days[static_cast<size_t>(day)], weekday, 5);
    }
    for (int day = 5; day < 7; ++day) {
        fillDay(analysis.days[static_cast<size_t>(day)], weekend, 3);
    }
    return analysis;
}

// Fleet 20 stretches the two weekday peaks (nReq 26) to 155 s. SPEC 6.4, BE-04 numbers.
ubahn::api::ScheduleAnalysis stretchedAnalysis() {
    ubahn::api::ScheduleAnalysis analysis = coveredAnalysis();
    for (int day = 0; day < 5; ++day) {
        analysis.days[static_cast<size_t>(day)][1].hEff = 155;
        analysis.days[static_cast<size_t>(day)][3].hEff = 155;
    }
    return analysis;
}

ubahn::api::SimTime probeOf(ubahn::api::SimTime slot) {
    const ubahn::api::SimTime open = ubahn::core::dayStart(slot) + ubahn::api::kOpenTod;
    return slot > open ? slot : open;
}

void checkPlan(const ubahn::core::Line& line, const ubahn::core::Trip& trip) {
    QCOMPARE(trip.aPlan.size(), static_cast<size_t>(line.n));
    QCOMPARE(trip.dPlan.size(), static_cast<size_t>(line.n));
    QCOMPARE(trip.aPlan[0], qint64{0});
    QCOMPARE(trip.dPlan[0], trip.slot);
    QCOMPARE(trip.trainId, -1);
    for (int pos = 0; pos < line.n - 1; ++pos) {
        const qint64 run = ubahn::core::spanOf(line, trip.dir, pos).tStd;
        const qint64 arrival = trip.dPlan[static_cast<size_t>(pos)] + run;
        QCOMPARE(trip.aPlan[static_cast<size_t>(pos + 1)], arrival);
        QCOMPARE(trip.dPlan[static_cast<size_t>(pos + 1)], arrival + trip.dwellPlan);
    }
}

void checkSteps(const ubahn::core::Line& line, const ubahn::api::ScheduleConfig& schedule,
                const ubahn::api::ScheduleAnalysis& analysis,
                const std::vector<ubahn::core::Trip>& trips) {
    QVERIFY(!trips.empty());
    for (size_t i = 0; i < trips.size(); ++i) {
        const ubahn::core::Trip& trip = trips[i];
        const int64_t day = ubahn::core::dayIndex(trip.slot);
        QVERIFY(trip.slot <= ubahn::core::lastSlot(line, schedule, day));
        const ubahn::api::SimTime probe = probeOf(trip.slot);
        const ubahn::api::Window* window = ubahn::core::windowAt(schedule, probe);
        QVERIFY(window != nullptr);
        QCOMPARE(trip.dwellPlan, window->dwell);
        if (i == 0) {
            continue;
        }
        const ubahn::core::Trip& prev = trips[i - 1];
        QVERIFY(trip.slot > prev.slot);
        const int64_t prevDay = ubahn::core::dayIndex(prev.slot);
        if (day != prevDay) {
            QCOMPARE(trip.slot, ubahn::core::firstSlot(line, schedule, day));
            QCOMPARE(day, prevDay + 1);
            continue;
        }
        const qint64 step = ubahn::core::hEffAt(analysis, schedule, probeOf(prev.slot));
        QCOMPARE(trip.slot - prev.slot, step);
    }
}

} // namespace

class TripsTest : public QObject {
    Q_OBJECT

private slots:
    void buildTripBothDirections();
    void firstAndLastSlot();
    void slotsFollowWindowHeadway();
    void stretchedWindowStep();
    void horizonAndSplitCalls();
    void idsAndDirection();
    void midnightAndSundayToMonday();
};

void TripsTest::buildTripBothDirections() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::SimTime slot = 20470;
    const ubahn::core::Trip forward = ubahn::core::buildTrip(line, 3, ubahn::api::Dir::I, slot, 60);
    const ubahn::core::Trip back = ubahn::core::buildTrip(line, 4, ubahn::api::Dir::II, slot, 60);

    QCOMPARE(forward.id, 3);
    QCOMPARE(forward.dir, ubahn::api::Dir::I);
    QCOMPARE(forward.dwellPlan, 60);
    QCOMPARE(forward.aPlan[1], slot + 120);
    QCOMPARE(back.id, 4);
    QCOMPARE(back.dir, ubahn::api::Dir::II);
    // SPEC 12, last span is 125 s. Path II departs along it.
    QCOMPARE(back.aPlan[1], slot + 125);
    checkPlan(line, forward);
    checkPlan(line, back);
    // Sum of tStd and intermediate dwells does not depend on direction. Arrival is 06:00.
    QCOMPARE(forward.aPlan.back(), qint64{6 * kHour});
    QCOMPARE(back.aPlan.back(), qint64{6 * kHour});
    QCOMPARE(forward.dPlan.back(), qint64{6 * kHour + 60});
    QCOMPARE(back.dPlan.back(), forward.dPlan.back());
}

void TripsTest::firstAndLastSlot() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    QCOMPARE(ubahn::core::tToTerm(line, 60), 1130);
    QCOMPARE(ubahn::core::tToTerm(line, 40), 1010);

    // Monday: first window dwell 60, last window dwell 40.
    QCOMPARE(ubahn::core::firstSlot(line, schedule, 0), qint64{21600 - 1130});
    QCOMPARE(ubahn::core::firstSlot(line, schedule, 0), qint64{20470});
    QCOMPARE(ubahn::core::lastSlot(line, schedule, 0), qint64{86400 - 1010});

    // Weekend: both edge windows have dwell 40, so the offset is 21600 - 1010.
    QCOMPARE(ubahn::core::firstSlot(line, schedule, 5), qint64{5 * 86400 + (21600 - 1010)});
    QCOMPARE(ubahn::core::firstSlot(line, schedule, 5), qint64{5 * 86400 + 20590});
    QCOMPARE(ubahn::core::firstSlot(line, schedule, 6), qint64{6 * 86400 + 20590});
    QCOMPARE(ubahn::core::lastSlot(line, schedule, 6), qint64{7 * 86400 - 1010});

    // dayIndex floors. Day -1 is the Sunday before week zero.
    QCOMPARE(ubahn::core::dayIndex(-1), qint64{-1});
    QCOMPARE(ubahn::core::firstSlot(line, schedule, -1), qint64{-86400 + 20590});

    const ubahn::core::SlotCursor monday =
        ubahn::core::initialSlotCursor(line, schedule, 12 * kHour);
    QCOMPARE(monday.nextSlotTime, qint64{20470});
    const ubahn::core::SlotCursor beforeEpoch =
        ubahn::core::initialSlotCursor(line, schedule, -100);
    QCOMPARE(beforeEpoch.nextSlotTime, ubahn::core::firstSlot(line, schedule, -1));

    const ubahn::core::Trip opening =
        ubahn::core::buildTrip(line, 0, ubahn::api::Dir::I, 20470, 60);
    QCOMPARE(opening.aPlan.back(), qint64{6 * kHour});
    const ubahn::core::Trip weekend =
        ubahn::core::buildTrip(line, 0, ubahn::api::Dir::II, 20590, 40);
    QCOMPARE(weekend.aPlan[1], qint64{20590 + 125});
    QCOMPARE(weekend.aPlan.back(), qint64{6 * kHour});
}

void TripsTest::slotsFollowWindowHeadway() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const ubahn::api::ScheduleAnalysis analysis = coveredAnalysis();
    ubahn::core::SlotCursor cursor = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t nextId = 0;
    // Through Tuesday morning, so the Monday evening tail and the midnight jump are included.
    const ubahn::api::SimTime horizon = 86400 + 8 * kHour;
    const std::vector<ubahn::core::Trip> trips = ubahn::core::generateSlots(
        cursor, line, schedule, analysis, ubahn::api::Dir::I, horizon, nextId);

    QCOMPARE(trips.front().slot, qint64{20470});
    QCOMPARE(trips.front().aPlan.back(), qint64{6 * kHour});
    checkSteps(line, schedule, analysis, trips);

    bool sawMonday = false;
    ubahn::api::SimTime lastMonday = 0;
    for (const ubahn::core::Trip& trip : trips) {
        QVERIFY(trip.slot <= horizon);
        if (ubahn::core::dayIndex(trip.slot) == 0) {
            sawMonday = true;
            lastMonday = trip.slot;
            QVERIFY(trip.slot <= qint64{86400 - 1010});
        }
    }
    QVERIFY(sawMonday);
    QVERIFY(lastMonday <= qint64{86400 - 1010});
    QVERIFY(cursor.nextSlotTime > horizon);
}

void TripsTest::stretchedWindowStep() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const ubahn::api::ScheduleAnalysis analysis = stretchedAnalysis();
    QCOMPARE(ubahn::core::hEffAt(analysis, schedule, 8 * kHour), 155);

    ubahn::core::SlotCursor cursor = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t nextId = 0;
    const std::vector<ubahn::core::Trip> trips = ubahn::core::generateSlots(
        cursor, line, schedule, analysis, ubahn::api::Dir::I, 12 * kHour, nextId);
    checkSteps(line, schedule, analysis, trips);

    int stretchedSteps = 0;
    for (size_t i = 1; i < trips.size(); ++i) {
        const ubahn::api::SimTime probe = probeOf(trips[i - 1].slot);
        if (ubahn::core::windowIndexAt(schedule, probe) != 1) {
            continue;
        }
        QCOMPARE(trips[i].slot - trips[i - 1].slot, qint64{155});
        QCOMPARE(trips[i - 1].dwellPlan, 80);
        ++stretchedSteps;
    }
    QVERIFY(stretchedSteps > 0);
}

void TripsTest::horizonAndSplitCalls() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const ubahn::api::ScheduleAnalysis analysis = coveredAnalysis();
    const ubahn::api::SimTime origin = 20470;

    ubahn::core::SlotCursor early = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t earlyId = 0;
    const ubahn::api::SimTime inside = origin + 500;
    const std::vector<ubahn::core::Trip> kept = ubahn::core::generateSlots(
        early, line, schedule, analysis, ubahn::api::Dir::I, inside, earlyId);
    QVERIFY(!kept.empty());
    for (const ubahn::core::Trip& trip : kept) {
        QVERIFY(trip.slot <= inside);
    }
    QVERIFY(early.nextSlotTime > inside);
    const int keptCount = static_cast<int>(kept.size());

    ubahn::core::SlotCursor cut = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t cutId = 0;
    const std::vector<ubahn::core::Trip> shorter = ubahn::core::generateSlots(
        cut, line, schedule, analysis, ubahn::api::Dir::I, kept.back().slot - 1, cutId);
    QCOMPARE(static_cast<int>(shorter.size()), keptCount - 1);

    // A cursor placed before the day's first slot snaps up to it and still respects the horizon.
    ubahn::core::SlotCursor low;
    low.nextSlotTime = 0;
    int32_t lowId = 0;
    const std::vector<ubahn::core::Trip> none =
        ubahn::core::generateSlots(low, line, schedule, analysis, ubahn::api::Dir::I, 100, lowId);
    QVERIFY(none.empty());
    QCOMPARE(low.nextSlotTime, origin);

    constexpr ubahn::api::SimTime kLen1 = 4000;
    constexpr ubahn::api::SimTime kLen2 = 9000;
    ubahn::core::SlotCursor split = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t splitId = 10;
    const std::vector<ubahn::core::Trip> part1 = ubahn::core::generateSlots(
        split, line, schedule, analysis, ubahn::api::Dir::II, origin + kLen1, splitId);
    const std::vector<ubahn::core::Trip> part2 = ubahn::core::generateSlots(
        split, line, schedule, analysis, ubahn::api::Dir::II, origin + kLen1 + kLen2, splitId);

    ubahn::core::SlotCursor once = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t onceId = 10;
    const std::vector<ubahn::core::Trip> whole = ubahn::core::generateSlots(
        once, line, schedule, analysis, ubahn::api::Dir::II, origin + kLen1 + kLen2, onceId);

    QCOMPARE(part1.size() + part2.size(), whole.size());
    QCOMPARE(split.nextSlotTime, once.nextSlotTime);
    QCOMPARE(splitId, onceId);
    for (size_t i = 0; i < part1.size(); ++i) {
        QCOMPARE(part1[i].slot, whole[i].slot);
        QCOMPARE(part1[i].id, whole[i].id);
    }
    for (size_t i = 0; i < part2.size(); ++i) {
        QCOMPARE(part2[i].slot, whole[part1.size() + i].slot);
        QCOMPARE(part2[i].id, whole[part1.size() + i].id);
    }

    const std::vector<ubahn::core::Trip> again = ubahn::core::generateSlots(
        once, line, schedule, analysis, ubahn::api::Dir::II, origin + kLen1 + kLen2, onceId);
    QVERIFY(again.empty());
}

void TripsTest::idsAndDirection() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const ubahn::api::ScheduleAnalysis analysis = coveredAnalysis();
    ubahn::core::SlotCursor cursor = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t nextId = 7;
    const std::vector<ubahn::core::Trip> trips = ubahn::core::generateSlots(
        cursor, line, schedule, analysis, ubahn::api::Dir::II, 10 * kHour, nextId);
    QVERIFY(trips.size() >= 2);
    for (size_t i = 0; i < trips.size(); ++i) {
        QCOMPARE(trips[i].id, static_cast<int32_t>(7 + i));
        QCOMPARE(trips[i].dir, ubahn::api::Dir::II);
        QCOMPARE(trips[i].aPlan[1], trips[i].slot + 125);
        QCOMPARE(trips[i].trainId, -1);
    }
    QCOMPARE(nextId, static_cast<int32_t>(7 + trips.size()));
}

void TripsTest::midnightAndSundayToMonday() {
    const ubahn::core::Line line = trainingLine();
    const ubahn::api::ScheduleConfig schedule = defaultSchedule();
    const ubahn::api::ScheduleAnalysis analysis = coveredAnalysis();

    ubahn::core::SlotCursor week = ubahn::core::initialSlotCursor(line, schedule, 0);
    int32_t weekId = 0;
    const ubahn::api::SimTime tuesdayMorning = 86400 + 7 * kHour;
    const std::vector<ubahn::core::Trip> weekTrips = ubahn::core::generateSlots(
        week, line, schedule, analysis, ubahn::api::Dir::I, tuesdayMorning, weekId);
    checkSteps(line, schedule, analysis, weekTrips);

    bool crossedMidnight = false;
    for (size_t i = 1; i < weekTrips.size(); ++i) {
        if (ubahn::core::dayIndex(weekTrips[i - 1].slot) == 0 &&
            ubahn::core::dayIndex(weekTrips[i].slot) == 1) {
            crossedMidnight = true;
            QVERIFY(weekTrips[i - 1].slot <= qint64{86400 - 1010});
            QVERIFY(weekTrips[i - 1].slot < 86400);
            QCOMPARE(weekTrips[i].slot, ubahn::core::firstSlot(line, schedule, 1));
            QVERIFY(weekTrips[i].slot > 86400);
            QVERIFY(weekTrips[i].slot < 86400 + 6 * kHour);
            QCOMPARE(weekTrips[i].dwellPlan, 60);
            QCOMPARE(weekTrips[i].aPlan.back(), qint64{86400 + 6 * kHour});
        }
    }
    QVERIFY(crossedMidnight);

    // Sunday (day 6) into the next Monday (day 7): the profile switches back to weekdays.
    const ubahn::api::SimTime sundayFrom = 6 * 86400;
    ubahn::core::SlotCursor weekend = ubahn::core::initialSlotCursor(line, schedule, sundayFrom);
    QCOMPARE(weekend.nextSlotTime, qint64{6 * 86400 + 20590});
    int32_t weekendId = 0;
    const ubahn::api::SimTime mondayMorning = 7 * 86400 + 7 * kHour;
    const std::vector<ubahn::core::Trip> weekendTrips = ubahn::core::generateSlots(
        weekend, line, schedule, analysis, ubahn::api::Dir::I, mondayMorning, weekendId);
    checkSteps(line, schedule, analysis, weekendTrips);

    bool crossedWeek = false;
    for (size_t i = 1; i < weekendTrips.size(); ++i) {
        const int64_t prevDay = ubahn::core::dayIndex(weekendTrips[i - 1].slot);
        const int64_t day = ubahn::core::dayIndex(weekendTrips[i].slot);
        if (prevDay == 6 && day == 7) {
            crossedWeek = true;
            QCOMPARE(ubahn::core::dowOf(weekendTrips[i - 1].slot), 6);
            QCOMPARE(ubahn::core::dowOf(weekendTrips[i].slot), 0);
            QCOMPARE(weekendTrips[i - 1].dwellPlan, 40);
            QCOMPARE(weekendTrips[i].slot, qint64{7 * 86400 + 20470});
            QCOMPARE(weekendTrips[i].dwellPlan, 60);
            QCOMPARE(weekendTrips[i].aPlan.back(), qint64{7 * 86400 + 6 * kHour});
            const qint64 gap = weekendTrips[i].slot - weekendTrips[i - 1].slot;
            QVERIFY(gap != 360);
            QVERIFY(gap != 180);
        }
    }
    QVERIFY(crossedWeek);

    // Negative t: the Sunday before the epoch, then Monday 20470.
    ubahn::core::SlotCursor before;
    before.nextSlotTime = ubahn::core::firstSlot(line, schedule, -1);
    int32_t beforeId = 0;
    const std::vector<ubahn::core::Trip> intoMonday = ubahn::core::generateSlots(
        before, line, schedule, analysis, ubahn::api::Dir::II, 20470, beforeId);
    QVERIFY(!intoMonday.empty());
    QCOMPARE(intoMonday.front().slot, qint64{-86400 + 20590});
    QCOMPARE(intoMonday.front().dir, ubahn::api::Dir::II);
    QCOMPARE(intoMonday.back().slot, qint64{20470});
    QCOMPARE(ubahn::core::dayIndex(intoMonday.back().slot), qint64{0});
    QCOMPARE(intoMonday.back().dwellPlan, 60);
    checkSteps(line, schedule, analysis, intoMonday);
}

QTEST_APPLESS_MAIN(TripsTest)

#include "tst_trips.moc"
