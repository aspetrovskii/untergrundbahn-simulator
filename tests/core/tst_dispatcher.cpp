#include "EngineFixture.h"
#include "api/Functions.h"
#include "core/Dispatcher.h"
#include "core/Regulator.h"
#include "core/Schedule.h"
#include "core/Time.h"

#include <QtTest>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace {

// Qt defines `slots` as a macro, which is also the name of Terminal's trip queue.
const std::deque<int32_t>& tripQueue(const ubahn::core::EngineState& state, ubahn::api::Term term) {
#pragma push_macro("slots")
#undef slots
    return state.terms[static_cast<std::size_t>(term)].slots;
#pragma pop_macro("slots")
}

std::deque<int32_t>& tripQueue(ubahn::core::EngineState& state, ubahn::api::Term term) {
#pragma push_macro("slots")
#undef slots
    return state.terms[static_cast<std::size_t>(term)].slots;
#pragma pop_macro("slots")
}

ubahn::core::EngineState opened(int fleet, int32_t headway) {
    const ubahn::api::SimParams params = ubahn::core::makeParams(
        ubahn::core::trainingLineConfig(), ubahn::core::constantSchedule(headway, 40, 0), fleet);
    return ubahn::core::makeState(params, 1);
}

void pullFromDepot(ubahn::core::EngineState& state, ubahn::api::Term term, int train) {
    auto& depot = state.terms[static_cast<std::size_t>(term)].depot;
    for (auto it = depot.begin(); it != depot.end(); ++it) {
        if (*it == train) {
            depot.erase(it);
            return;
        }
    }
}

void parkCursor(ubahn::core::EngineState& state, ubahn::api::Term term, ubahn::api::SimTime now) {
    const ubahn::api::SimTime horizon =
        now +
        static_cast<ubahn::api::SimTime>(ubahn::core::kSlotHorizonCycles) * state.analysis.tCycle;
    state.terms[static_cast<std::size_t>(term)].cursor.nextSlotTime = horizon + 1;
}

ubahn::core::Trip& queueTrip(ubahn::core::EngineState& state, ubahn::api::Term term,
                             ubahn::api::SimTime slot, int trainId) {
    const ubahn::api::Dir dir =
        term == ubahn::api::Term::A ? ubahn::api::Dir::I : ubahn::api::Dir::II;
    ubahn::core::Trip& trip = ubahn::core::addTrip(state, dir, slot, 40);
    trip.trainId = trainId;
    if (trainId >= 0 && trainId < static_cast<int>(state.trains.size())) {
        state.trains[static_cast<std::size_t>(trainId)].tripId = trip.id;
    }
    tripQueue(state, term).push_back(trip.id);
    return trip;
}

void markOut(ubahn::core::EngineState& state, int train) {
    ubahn::core::Train& unit = state.trains[static_cast<std::size_t>(train)];
    unit.state = ubahn::core::TrainState::Dwelling;
    unit.flags.headingToDepot = false;
}

int32_t needAt(const ubahn::core::EngineState& state, ubahn::api::SimTime now) {
    const ubahn::api::Window* window = ubahn::core::windowAt(state.schedule, now);
    if (window == nullptr) {
        return 0;
    }
    const int32_t step = ubahn::core::hEffAt(state.analysis, state.schedule, now);
    return ubahn::core::nReq(ubahn::core::tCycle(state.line, window->dwell),
                             step > 0 ? step : window->headway);
}

bool listed(const ubahn::core::EngineState& state, ubahn::api::Term term, int32_t id) {
    for (const int32_t queued : tripQueue(state, term)) {
        if (queued == id) {
            return true;
        }
    }
    return false;
}

} // namespace

class DispatcherTest : public QObject {
    Q_OBJECT

private slots:
    void horizonDoesNotDuplicate();
    void turnaroundKeepsSlotWhenReady();
    void lateTurnaroundLosesSlotToDepot();
    void shiftPastQueueEndDirtiesLastHolder();
    void latePlatformHolderKeepsSlot();
    void emptyDepotWarnsNoTrain();
    void assignTripTakesEarliestUnassigned();
    void withdrawsWhenLineHasMoreTrainsThanNeed();
    void withdrawsAfterClose();
    void keepsTrainWhenNeedIsMetAndSlotRemains();
    void rejectedScheduleLeavesStateUntouched();
    void appliedScheduleRebuildsUnassignedSlots();
    void wakeUsesMarginBeforeUncoveredSlot();
};

void DispatcherTest::horizonDoesNotDuplicate() {
    ubahn::core::EngineState state = opened(26, 120);
    const ubahn::api::SimTime now = 8 * 3600;
    state.now = now;
    QVERIFY(state.analysis.tCycle > 0);
    const ubahn::api::SimTime horizon =
        now +
        static_cast<ubahn::api::SimTime>(ubahn::core::kSlotHorizonCycles) * state.analysis.tCycle;
    const int32_t step = ubahn::core::hEffAt(state.analysis, state.schedule, now);
    QVERIFY(step > 0);

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    const ubahn::core::Terminal& term = state.terms[0];
    const std::deque<int32_t>& queue = tripQueue(state, ubahn::api::Term::A);
    QVERIFY(!state.trips.empty());
    QVERIFY(!queue.empty());
    QCOMPARE(static_cast<int>(queue.size()), static_cast<int>(state.trips.size()));
    const ubahn::api::SimTime first = ubahn::api::kOpenTod - ubahn::core::tToTerm(state.line, 40);
    QCOMPARE(state.trips.front().slot, first);
    QVERIFY(state.trips.back().slot <= horizon);
    QVERIFY(term.cursor.nextSlotTime > horizon);
    for (std::size_t i = 0; i < state.trips.size(); ++i) {
        const ubahn::core::Trip& trip = state.trips[i];
        QCOMPARE(trip.id, static_cast<int32_t>(i));
        QCOMPARE(trip.dir, ubahn::api::Dir::I);
        QVERIFY(trip.slot <= horizon);
        QCOMPARE(queue[i], trip.id);
        if (i > 0) {
            QCOMPARE(trip.slot - state.trips[i - 1].slot, static_cast<ubahn::api::SimTime>(step));
        }
    }

    const int tripCount = static_cast<int>(state.trips.size());
    const ubahn::api::SimTime cursor = term.cursor.nextSlotTime;
    ubahn::core::DirtySet again(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, again);
    QCOMPARE(static_cast<int>(state.trips.size()), tripCount);
    QCOMPARE(static_cast<int>(tripQueue(state, ubahn::api::Term::A).size()), tripCount);
    QCOMPARE(state.terms[0].cursor.nextSlotTime, cursor);
    QCOMPARE(state.trips.front().slot, first);
}

void DispatcherTest::turnaroundKeepsSlotWhenReady() {
    // Slot is inside the lookahead, and the train is already on the departure platform.
    // readyAt is "when it will stand there": now, or 0 while BE-10 is a stub. Both are
    // <= slot - dwellMin, so the slot stays with the train either way.
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = 0;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    pullFromDepot(state, ubahn::api::Term::A, 0);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    state.trains[0].aAct = now;
    const int depotBefore = static_cast<int>(state.terms[0].depot.size());
    const int32_t tripId = queueTrip(state, ubahn::api::Term::A, now + 150, 0).id;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    QCOMPARE(state.trips[static_cast<std::size_t>(tripId)].trainId, 0);
    QCOMPARE(state.trains[0].tripId, tripId);
    QCOMPARE(static_cast<int>(state.terms[0].depot.size()), depotBefore);
    QVERIFY(state.pendingWarnings.empty());
}

void DispatcherTest::lateTurnaroundLosesSlotToDepot() {
    // slot - dwellMin is negative, so a readyAt of 0 (BE-10 stub) and any non-negative
    // estimate both miss the slot. The dead-end next event is far ahead so a live
    // resourceFreeAt misses as well. The depot train takes this slot; the turnaround
    // train takes the next one (ALGORITHM 10.2).
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = 0;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    pullFromDepot(state, ubahn::api::Term::A, 0);
    ubahn::core::placeInDeadEnd(state, 0, ubahn::api::Term::A);
    ubahn::core::NextEvent far;
    far.type = ubahn::core::EventType::LeaveDeadEnd;
    far.time = 1000000;
    far.valid = true;
    ubahn::core::setNext(state, 0, far);
    QVERIFY(!state.terms[0].depot.empty());
    const int depotTrain = state.terms[0].depot.front();
    // Copy ids: the next addTrip can reallocate state.trips.
    const int32_t earlyId = queueTrip(state, ubahn::api::Term::A, now + 20, 0).id;
    const int32_t nextId = queueTrip(state, ubahn::api::Term::A, now + 400, -1).id;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    const ubahn::core::Trip& early = state.trips[static_cast<std::size_t>(earlyId)];
    const ubahn::core::Trip& next = state.trips[static_cast<std::size_t>(nextId)];
    QCOMPARE(early.trainId, depotTrain);
    QCOMPARE(state.trains[static_cast<std::size_t>(depotTrain)].tripId, early.id);
    QCOMPARE(next.trainId, 0);
    QCOMPARE(state.trains[0].tripId, next.id);
    QVERIFY(early.slot < next.slot);
    QVERIFY(dirty.contains(depotTrain));
    QVERIFY(dirty.contains(0));
    QVERIFY(state.pendingWarnings.empty());
}

void DispatcherTest::shiftPastQueueEndDirtiesLastHolder() {
    // Same chain as lateTurnaroundLosesSlotToDepot, but the following slot is held and
    // there is no slot after it. The displaced holder ends without a trip and must be
    // recomputed, or its old event stays in the heap.
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = 0;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    pullFromDepot(state, ubahn::api::Term::A, 0);
    ubahn::core::placeInDeadEnd(state, 0, ubahn::api::Term::A);
    ubahn::core::NextEvent far;
    far.type = ubahn::core::EventType::LeaveDeadEnd;
    far.time = 1000000;
    far.valid = true;
    ubahn::core::setNext(state, 0, far);
    pullFromDepot(state, ubahn::api::Term::B, 2);
    markOut(state, 2);
    queueTrip(state, ubahn::api::Term::A, now + 20, 0);
    queueTrip(state, ubahn::api::Term::A, now + 400, 2);

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    QCOMPARE(state.trains[2].tripId, -1);
    QVERIFY(dirty.contains(2));
}

void DispatcherTest::latePlatformHolderKeepsSlot() {
    // The holder stands on the departure platform but arrived later than slot - dwellMin.
    // A depot train would wait behind it, so the slot stays and nothing shifts.
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = 0;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    pullFromDepot(state, ubahn::api::Term::A, 0);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    QVERIFY(ubahn::core::readyAt(state, 0) > now + 20 - state.line.cfg.dwellMin);
    const int depotBefore = static_cast<int>(state.terms[0].depot.size());
    const int32_t tripId = queueTrip(state, ubahn::api::Term::A, now + 20, 0).id;
    const int32_t nextId = queueTrip(state, ubahn::api::Term::A, now + 400, -1).id;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    QCOMPARE(state.trips[static_cast<std::size_t>(tripId)].trainId, 0);
    QCOMPARE(state.trains[0].tripId, tripId);
    QCOMPARE(state.trips[static_cast<std::size_t>(nextId)].trainId, -1);
    QCOMPARE(static_cast<int>(state.terms[0].depot.size()), depotBefore);
    QVERIFY(!dirty.contains(0));
    const ubahn::api::SimTime lead =
        state.line.cfg.depotOut + state.line.cfg.dwellMin + ubahn::core::kDispatcherMargin;
    const ubahn::core::NextEvent wake =
        ubahn::core::dispatcherNext(state, ubahn::api::Term::A, now);
    QCOMPARE(wake.time, std::max(now + 400 - lead, now + 1));
}

void DispatcherTest::emptyDepotWarnsNoTrain() {
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = 10 * 3600;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    state.terms[0].depot.clear();
    const ubahn::api::SimTime slot = now + 50;
    queueTrip(state, ubahn::api::Term::A, slot, -1);

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(static_cast<int>(state.trains.size())));
    ubahn::core::handleDispatcher(state, ubahn::api::Term::A, now, dirty);

    QCOMPARE(static_cast<int>(state.pendingWarnings.size()), 1);
    const ubahn::api::Warning& warning = state.pendingWarnings.front();
    QCOMPARE(warning.code, ubahn::api::WarningCode::NoTrainForTrip);
    QCOMPARE(warning.terminal, static_cast<int>(ubahn::api::Term::A));
    QCOMPARE(warning.tripSlot, slot);
    QVERIFY(warning.message.find("нет поезда") != std::string::npos);
}

void DispatcherTest::assignTripTakesEarliestUnassigned() {
    ubahn::core::EngineState state = opened(4, 3000);
    const int32_t takenId = queueTrip(state, ubahn::api::Term::A, 1000, 1).id;
    const int32_t earliestId = queueTrip(state, ubahn::api::Term::A, 2000, -1).id;
    const int32_t laterId = queueTrip(state, ubahn::api::Term::A, 3000, -1).id;

    ubahn::core::assignTripFromTurnaround(state, 0, ubahn::api::Term::A);

    QCOMPARE(state.trips[static_cast<std::size_t>(takenId)].trainId, 1);
    QCOMPARE(state.trips[static_cast<std::size_t>(earliestId)].trainId, 0);
    QCOMPARE(state.trains[0].tripId, earliestId);
    QCOMPARE(state.trips[static_cast<std::size_t>(laterId)].trainId, -1);
    QVERIFY(listed(state, ubahn::api::Term::A, earliestId));
}

void DispatcherTest::withdrawsWhenLineHasMoreTrainsThanNeed() {
    ubahn::core::EngineState state = opened(30, 3000);
    const ubahn::api::SimTime now = 10 * 3600;
    state.now = now;
    const int32_t need = needAt(state, now);
    QVERIFY(need >= 1);
    QVERIFY(need + 1 <= static_cast<int>(state.trains.size()));
    for (int id = 0; id < need + 1; ++id) {
        markOut(state, id);
    }
    // Already on the departure platform, so a live readyAt still reaches the future slot.
    // Withdrawal then comes from the surplus, not from a missing slot.
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    state.trains[0].aAct = now;
    queueTrip(state, ubahn::api::Term::A, now + 2 * 3600, -1);
    QVERIFY(ubahn::core::decideWithdrawal(state, 0, ubahn::api::Term::A, now));
}

void DispatcherTest::withdrawsAfterClose() {
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime now = ubahn::api::kCloseTod + 60;
    state.now = now;
    markOut(state, 0);
    queueTrip(state, ubahn::api::Term::A, now + 3600, -1);
    QVERIFY(ubahn::core::windowAt(state.schedule, now) == nullptr);
    QVERIFY(ubahn::core::decideWithdrawal(state, 0, ubahn::api::Term::A, now));
}

void DispatcherTest::keepsTrainWhenNeedIsMetAndSlotRemains() {
    ubahn::core::EngineState state = opened(30, 3000);
    const ubahn::api::SimTime now = 10 * 3600;
    state.now = now;
    const int32_t need = needAt(state, now);
    QVERIFY(need >= 1);
    QVERIFY(need < static_cast<int>(state.trains.size()));
    for (int id = 0; id < need; ++id) {
        markOut(state, id);
    }
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    state.trains[0].aAct = now;
    state.trains[static_cast<std::size_t>(need)].state = ubahn::core::TrainState::Dwelling;
    state.trains[static_cast<std::size_t>(need)].flags.headingToDepot = true;
    queueTrip(state, ubahn::api::Term::A, now + 2 * 3600, -1);
    QVERIFY(!ubahn::core::decideWithdrawal(state, 0, ubahn::api::Term::A, now));
}

void DispatcherTest::rejectedScheduleLeavesStateUntouched() {
    ubahn::core::EngineState state = opened(4, 3000);
    ubahn::api::ScheduleConfig broken = state.schedule;
    for (ubahn::api::DayProfile& day : broken.days) {
        day.windows.clear();
        ubahn::api::Window gap;
        gap.fromTod = 8 * 3600;
        gap.toTod = 10 * 3600;
        gap.headway = 3000;
        gap.dwell = 40;
        day.windows.push_back(gap);
    }
    const ubahn::api::ScheduleConfig saved = state.schedule;
    const int trips = static_cast<int>(state.trips.size());
    const auto issues = ubahn::core::applyScheduleChange(state, broken, 12 * 3600);
    QVERIFY(!issues.empty());
    QCOMPARE(state.schedule.days[0].windows[0].headway, saved.days[0].windows[0].headway);
    QCOMPARE(static_cast<int>(state.schedule.days[0].windows.size()),
             static_cast<int>(saved.days[0].windows.size()));
    QCOMPARE(static_cast<int>(state.trips.size()), trips);
}

void DispatcherTest::appliedScheduleRebuildsUnassignedSlots() {
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime boundary = 12 * 3600;
    state.now = boundary;
    parkCursor(state, ubahn::api::Term::A, boundary);
    parkCursor(state, ubahn::api::Term::B, boundary);
    ubahn::core::Trip& assigned = queueTrip(state, ubahn::api::Term::A, boundary + 100, 0);
    const int32_t assignedDwell = assigned.dwellPlan;
    const ubahn::api::SimTime assignedSlot = assigned.slot;
    const int32_t assignedId = assigned.id;
    ubahn::core::Trip& kept = queueTrip(state, ubahn::api::Term::A, boundary, -1);
    const int32_t keptId = kept.id;
    ubahn::core::Trip& dropped = queueTrip(state, ubahn::api::Term::A, boundary + 1000, -1);
    const int32_t droppedId = dropped.id;
    const int32_t oldHeadway = state.schedule.days[0].windows[0].headway;

    ubahn::api::ScheduleConfig next = ubahn::core::constantSchedule(120, 40, 0);
    const auto issues = ubahn::core::applyScheduleChange(state, next, boundary);
    QVERIFY(issues.empty());
    QCOMPARE(state.schedule.days[0].windows[0].headway, static_cast<int32_t>(120));
    QCOMPARE(state.params.schedule.days[0].windows[0].headway, oldHeadway);

    QVERIFY(listed(state, ubahn::api::Term::A, assignedId));
    QCOMPARE(state.trips[static_cast<std::size_t>(assignedId)].trainId, 0);
    QCOMPARE(state.trips[static_cast<std::size_t>(assignedId)].slot, assignedSlot);
    QCOMPARE(state.trips[static_cast<std::size_t>(assignedId)].dwellPlan, assignedDwell);
    QVERIFY(listed(state, ubahn::api::Term::A, keptId));
    QVERIFY(!listed(state, ubahn::api::Term::A, droppedId));
    QCOMPARE(state.trips[static_cast<std::size_t>(droppedId)].trainId, -1);

    const int32_t step = ubahn::core::hEffAt(state.analysis, state.schedule, boundary);
    QVERIFY(step > 120);
    QVERIFY(state.terms[0].cursor.nextSlotTime > boundary + 100);
    std::vector<ubahn::api::SimTime> rebuilt;
    for (const int32_t id : tripQueue(state, ubahn::api::Term::A)) {
        const ubahn::core::Trip& trip = state.trips[static_cast<std::size_t>(id)];
        if (trip.trainId < 0 && trip.slot > boundary) {
            rebuilt.push_back(trip.slot);
        }
    }
    QVERIFY(rebuilt.size() >= 2);
    for (std::size_t i = 1; i < rebuilt.size(); ++i) {
        QCOMPARE(rebuilt[i] - rebuilt[i - 1], static_cast<ubahn::api::SimTime>(step));
    }
    QVERIFY(!state.pendingWarnings.empty());
    bool stretched = false;
    for (const ubahn::api::Warning& warning : state.pendingWarnings) {
        if (warning.code == ubahn::api::WarningCode::IntervalStretched) {
            stretched = true;
            QCOMPARE(warning.effectiveHeadway, step);
            QVERIFY(!warning.message.empty());
        }
    }
    QVERIFY(stretched);
    QVERIFY(state.terms[0].next.valid);
    QVERIFY(state.terms[1].next.valid);
    QVERIFY(state.terms[0].next.time > state.now);
}

void DispatcherTest::wakeUsesMarginBeforeUncoveredSlot() {
    ubahn::core::EngineState state = opened(4, 3000);
    const ubahn::api::SimTime slot = 10000;
    const ubahn::api::SimTime now = 9000;
    state.now = now;
    parkCursor(state, ubahn::api::Term::A, now);
    queueTrip(state, ubahn::api::Term::A, slot, -1);
    const ubahn::api::SimTime lead =
        state.line.cfg.depotOut + state.line.cfg.dwellMin + ubahn::core::kDispatcherMargin;
    ubahn::core::NextEvent wake = ubahn::core::dispatcherNext(state, ubahn::api::Term::A, now);
    QCOMPARE(wake.type, ubahn::core::EventType::DispatcherWake);
    QVERIFY(wake.valid);
    QCOMPARE(wake.time, slot - lead);

    wake = ubahn::core::dispatcherNext(state, ubahn::api::Term::A, slot);
    QCOMPARE(wake.time, slot + 1);
}

QTEST_APPLESS_MAIN(DispatcherTest)
#include "tst_dispatcher.moc"
