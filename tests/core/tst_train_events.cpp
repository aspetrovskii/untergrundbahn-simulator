#include "EngineFixture.h"
#include "core/Regulator.h"
#include "core/TrainEvents.h"

#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using ubahn::api::Dir;
using ubahn::api::SimTime;
using ubahn::api::Term;
using ubahn::core::addTrip;
using ubahn::core::ahead;
using ubahn::core::behind;
using ubahn::core::constantSchedule;
using ubahn::core::DirtySet;
using ubahn::core::dispatcherOwner;
using ubahn::core::drawDelay;
using ubahn::core::EngineState;
using ubahn::core::EventType;
using ubahn::core::handleTrain;
using ubahn::core::makeParams;
using ubahn::core::makeState;
using ubahn::core::NextEvent;
using ubahn::core::ownerCount;
using ubahn::core::placeAtPlatform;
using ubahn::core::placeOnSpan;
using ubahn::core::platformAt;
using ubahn::core::setNext;
using ubahn::core::spanOf;
using ubahn::core::stationOf;
using ubahn::core::trackPushBack;
using ubahn::core::trackRemove;
using ubahn::core::trainingLineConfig;
using ubahn::core::TrainState;
using ubahn::core::windowAt;

EngineState serviceState(double probability, uint64_t seed = 1) {
    const ubahn::api::SimParams params =
        makeParams(trainingLineConfig(), constantSchedule(120, 60, probability), 4);
    EngineState state = makeState(params, seed);
    state.now = 10 * 3600;
    return state;
}

void pullOut(EngineState& state, int train) {
    for (ubahn::core::Terminal& term : state.terms) {
        auto& depot = term.depot;
        depot.erase(std::remove(depot.begin(), depot.end(), train), depot.end());
    }
}

void arm(EngineState& state, int train, EventType type, SimTime time) {
    setNext(state, train, NextEvent{type, time, true});
}

DirtySet dirtyFor(const EngineState& state) {
    return DirtySet(ownerCount(state.params.line.fleetSize));
}

int32_t clampRun(SimTime earliest, SimTime now, int32_t tMin, int32_t tMax) {
    const SimTime delta = earliest - now;
    if (delta <= tMin) {
        return tMin;
    }
    if (delta >= tMax) {
        return tMax;
    }
    return static_cast<int32_t>(delta);
}

// Qt defines `slots` as a keyword macro; Terminal::slots needs it out of the way.
#pragma push_macro("slots")
#undef slots
auto& slotQueue(EngineState& state, Term term) {
    return state.terms[static_cast<std::size_t>(term)].slots;
}
#pragma pop_macro("slots")

bool depotContains(const EngineState& state, Term term, int train) {
    const auto& depot = state.terms[static_cast<std::size_t>(term)].depot;
    return std::find(depot.begin(), depot.end(), train) != depot.end();
}

} // namespace

class TrainEventsTest : public QObject {
    Q_OBJECT

private slots:
    void trackListInsertAndRemove();
    void arriveIntermediateFixesDwellOnce();
    void arriveChecksDestinationPlatform();
    void arriveTerminalAlights();
    void originDepartureLeavesSlotQueue();
    void departReleasesPlatform();
    void reachSignalStops();
    void followerSignalStopsBehindLeader();
    void originWaitIsNotCatchUp();
    void deadEndWaitIsNotLateness();
    void turnaroundChain();
    void depotChain();
    void leaveDepotOffHoursFollowsClock();
    void drawOutsideServiceIsZero();
    void drawOverrideReplacesRoll();
    void drawCertainDelayFollowsRng();
    void occupiedPlatformIsNotApplied();
    void rescheduleKeepsPosition();
};

void TrainEventsTest::trackListInsertAndRemove() {
    EngineState state = serviceState(0);
    pullOut(state, 0);
    pullOut(state, 1);
    pullOut(state, 2);

    trackPushBack(state, Dir::I, 0);
    trackPushBack(state, Dir::I, 1);
    trackPushBack(state, Dir::I, 2);

    QCOMPARE(state.tracks[0].head, 0);
    QCOMPARE(state.tracks[0].tail, 2);
    QCOMPARE(ahead(state, 1), 0);
    QCOMPARE(behind(state, 1), 2);
    QCOMPARE(ahead(state, 0), -1);
    QCOMPARE(behind(state, 2), -1);

    trackRemove(state, Dir::I, 1);
    QCOMPARE(state.tracks[0].head, 0);
    QCOMPARE(state.tracks[0].tail, 2);
    QCOMPARE(behind(state, 0), 2);
    QCOMPARE(ahead(state, 2), 0);
    QCOMPARE(ahead(state, 1), -1);
    QCOMPARE(behind(state, 1), -1);

    trackRemove(state, Dir::I, 1);
    QCOMPARE(state.tracks[0].head, 0);
    QCOMPARE(behind(state, 0), 2);

    trackRemove(state, Dir::I, 0);
    QCOMPARE(state.tracks[0].head, 2);
    QCOMPARE(state.tracks[0].tail, 2);
    trackRemove(state, Dir::I, 2);
    QCOMPARE(state.tracks[0].head, -1);
    QCOMPARE(state.tracks[0].tail, -1);
}

void TrainEventsTest::arriveIntermediateFixesDwellOnce() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    pullOut(state, 1);

    ubahn::core::Trip& trip = addTrip(state, Dir::I, now - 1000, 60);
    trip.aPlan[3] = now - 10;
    trip.dPlan[3] = now + 30;
    state.trains[0].tripId = trip.id;
    trip.trainId = 0;

    placeOnSpan(state, 0, Dir::I, 2, now - 100, now);
    trackPushBack(state, Dir::I, 0);
    trackPushBack(state, Dir::I, 1);

    int rolls = 0;
    state.delayOverride = [&rolls](int, SimTime) {
        ++rolls;
        return 15;
    };
    arm(state, 0, EventType::Arrive, now);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    const ubahn::core::Train& train = state.trains[0];
    QCOMPARE(train.state, TrainState::Dwelling);
    QCOMPARE(train.pos, 3);
    QCOMPARE(train.aAct, now);
    QCOMPARE(train.lateness, 10);
    QCOMPARE(train.dwellEff, 50);
    QCOMPARE(train.drawnDelay, 15);
    // ALGORITHM 7.2: max(aAct + dwellEff + drawnDelay, dPlan) = max(now+65, now+30).
    QCOMPARE(train.dBase, now + 65);
    QVERIFY(train.flags.catchingUp);
    QVERIFY(!train.flags.held);
    QVERIFY(!train.flags.slowed);
    QCOMPARE(rolls, 1);

    const ubahn::core::Platform& platform = platformAt(state, static_cast<int>(Dir::I), 3);
    QCOMPARE(platform.occupant, 0);
    QCOMPARE(platform.lastArrival, now);
    QCOMPARE(platform.lastShift, 10);
    QCOMPARE(static_cast<int>(state.pendingDelays.size()), 1);
    QCOMPARE(state.pendingDelays[0].trainId, 0);
    QCOMPARE(state.pendingDelays[0].dir, Dir::I);
    QCOMPARE(state.pendingDelays[0].station, 3);
    QCOMPARE(state.pendingDelays[0].seconds, 15);
    QVERIFY(dirty.contains(0));
    QVERIFY(dirty.contains(1));
    QCOMPARE(behind(state, 0), 1);
}

void TrainEventsTest::arriveChecksDestinationPlatform() {
    // The platform just left can have a fresh lastArrival. C6 is about the
    // platform being entered (ALGORITHM 8), so that sample must not trip the check.
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    ubahn::core::Trip& trip = addTrip(state, Dir::I, now - 1000, 60);
    trip.aPlan[3] = now;
    trip.dPlan[3] = now + 60;
    state.trains[0].tripId = trip.id;
    trip.trainId = 0;
    placeOnSpan(state, 0, Dir::I, 2, now - 100, now);
    platformAt(state, static_cast<int>(Dir::I), 2).lastArrival = now - 10;
    arm(state, 0, EventType::Arrive, now);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    QCOMPARE(state.trains[0].pos, 3);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), 3).occupant, 0);
}

void TrainEventsTest::arriveTerminalAlights() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    const int last = state.line.n - 1;
    pullOut(state, 0);
    placeOnSpan(state, 0, Dir::I, last - 1, now - 100, now);
    arm(state, 0, EventType::Arrive, now);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    const ubahn::core::Train& train = state.trains[0];
    QCOMPARE(train.state, TrainState::AlightingTerminal);
    QCOMPARE(train.pos, last);
    QCOMPARE(train.aAct, now);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), last).occupant, 0);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), last).lastArrival, now);
    QVERIFY(dirty.contains(0));
}

void TrainEventsTest::originDepartureLeavesSlotQueue() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    placeAtPlatform(state, 0, Dir::I, 0);
    state.trains[0].aAct = now - 60;
    const int32_t sentId = addTrip(state, Dir::I, now, 60).id;
    const int32_t laterId = addTrip(state, Dir::I, now + 180, 60).id;
    state.trips[static_cast<std::size_t>(sentId)].trainId = 0;
    state.trains[0].tripId = sentId;
    auto& queue = slotQueue(state, Term::A);
    queue.clear();
    queue.push_back(sentId);
    queue.push_back(laterId);
    trackPushBack(state, Dir::I, 0);
    arm(state, 0, EventType::Depart, now);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    QCOMPARE(state.trains[0].state, TrainState::Running);
    // A sent trip stays out of the queue, so the turnaround cannot pick it again.
    QCOMPARE(static_cast<int>(queue.size()), 1);
    QCOMPARE(static_cast<int>(queue.front()), static_cast<int>(laterId));
}

void TrainEventsTest::departReleasesPlatform() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    placeAtPlatform(state, 0, Dir::I, 2);
    state.trains[0].aAct = now - 70;
    ubahn::core::Trip& trip = addTrip(state, Dir::I, now - 1000, 60);
    trip.dPlan[2] = now - 8;
    state.trains[0].tripId = trip.id;
    trip.trainId = 0;
    trackPushBack(state, Dir::I, 0);
    arm(state, 0, EventType::Depart, now);

    const ubahn::core::Span& span = spanOf(state.line, Dir::I, 2);
    const SimTime earliest = ubahn::core::earliestArrival(state, 0, 3);
    const int32_t tRun = clampRun(earliest, now, span.tMin, span.tMax);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    const ubahn::core::Train& train = state.trains[0];
    QCOMPARE(train.state, TrainState::Running);
    QCOMPARE(train.pos, 2);
    QCOMPARE(train.departAct, now);
    QCOMPARE(train.seg.t0, now);
    QCOMPARE(train.seg.t1, now + tRun);
    QCOMPARE(train.seg.x0, 0.0);
    QCOMPARE(train.seg.x1, span.lengthM);
    QCOMPARE(train.flags.catchingUp, tRun < span.tStd);
    QCOMPARE(train.flags.slowed, tRun > span.tStd);
    QVERIFY(!train.flags.held);
    QCOMPARE(train.lateness, 8);

    const ubahn::core::Platform& platform = platformAt(state, static_cast<int>(Dir::I), 2);
    QCOMPARE(platform.occupant, -1);
    QCOMPARE(platform.lastDeparture, now);
    QCOMPARE(platform.lastDwell, 70);
    QCOMPARE(platform.lastShift, 8);
    QCOMPARE(platform.lastTrain, 0);
    QCOMPARE(state.tracks[0].head, 0);
    QVERIFY(dirty.contains(0));
}

void TrainEventsTest::reachSignalStops() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    placeOnSpan(state, 0, Dir::I, 0, now - 50, now + 50);
    arm(state, 0, EventType::ReachSignal, now);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    const ubahn::core::Train& train = state.trains[0];
    const ubahn::core::Span& span = spanOf(state.line, Dir::I, 0);
    const double stopX = span.lengthM - state.line.cfg.signalStopOffsetM;
    QCOMPARE(train.state, TrainState::StoppedAtSignal);
    QCOMPARE(train.signalAt, now);
    QCOMPARE(train.pos, 0);
    QCOMPARE(train.seg.t0, now);
    QCOMPARE(train.seg.t1, now);
    QCOMPARE(train.seg.x0, stopX);
    QCOMPARE(train.seg.x1, stopX);
    QVERIFY(dirty.contains(0));
}

void TrainEventsTest::followerSignalStopsBehindLeader() {
    EngineState state = serviceState(0);
    pullOut(state, 0);
    pullOut(state, 1);
    const SimTime depart = 1000;
    placeOnSpan(state, 1, Dir::I, 0, depart, depart + 120);
    ubahn::core::Trip& trip = addTrip(state, Dir::I, depart, 60);
    trip.trainId = 1;
    state.trains[1].tripId = trip.id;
    trip.aPlan[1] = 1300;
    setNext(state, 1, NextEvent{EventType::Arrive, depart + 120, true});

    const ubahn::core::Span& span = spanOf(state.line, Dir::I, 0);
    const double stopX = span.lengthM - state.line.cfg.signalStopOffsetM;
    ubahn::core::Train& leader = state.trains[0];
    leader.state = TrainState::StoppedAtSignal;
    leader.dir = Dir::I;
    leader.pos = 0;
    leader.seg = ubahn::core::Segment{depart, depart, stopX, stopX};
    leader.signalAt = depart;
    setNext(state, 0, NextEvent{EventType::Arrive, 3000, true});
    trackPushBack(state, Dir::I, 0);
    trackPushBack(state, Dir::I, 1);

    const NextEvent next = ubahn::core::computeNext(state, 1, depart);
    QCOMPARE(next.type, EventType::ReachSignal);
    ubahn::core::applyReschedule(state, 1, next, depart);
    QVERIFY(state.trains[1].seg.x1 < stopX - 0.5);
    QVERIFY(state.trains[1].seg.x1 < leader.seg.x1 - 0.5);

    // The event time may not move when the queue is only a metre, so the segment can
    // still be aimed at the platform. The stop itself must not land on the leader.
    placeOnSpan(state, 1, Dir::I, 0, depart, depart + 120);
    state.trains[1].prevOnTrack = 0;
    state.trains[0].nextOnTrack = 1;
    arm(state, 1, EventType::ReachSignal, depart + 60);
    state.now = depart + 60;
    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 1, state.now, dirty);
    QCOMPARE(state.trains[1].state, TrainState::StoppedAtSignal);
    QVERIFY(state.trains[1].seg.x0 < stopX - 0.5);
}

void TrainEventsTest::originWaitIsNotCatchUp() {
    EngineState state = serviceState(0);
    pullOut(state, 0);
    placeAtPlatform(state, 0, Dir::I, 0);
    const SimTime slot = state.now + 100;
    ubahn::core::Trip& trip = addTrip(state, Dir::I, slot, 60);
    trip.trainId = 0;
    state.trains[0].tripId = trip.id;
    state.trains[0].dwellEff = state.line.cfg.dwellMin;
    state.trains[0].dBase = slot;
    state.trains[0].flags.catchingUp = false;

    ubahn::core::applyReschedule(state, 0, NextEvent{EventType::Depart, slot, true}, state.now);
    QVERIFY(!state.trains[0].flags.catchingUp);
    QVERIFY(!state.trains[0].flags.held);
}

void TrainEventsTest::deadEndWaitIsNotLateness() {
    const int last = serviceState(0).line.n - 1;
    for (const bool lateAlighting : {false, true}) {
        EngineState state = serviceState(0);
        const SimTime now = state.now;
        pullOut(state, 0);
        placeAtPlatform(state, 0, Dir::I, last);
        trackPushBack(state, Dir::I, 0);
        ubahn::core::Trip& trip = addTrip(state, Dir::I, now - 2000, 60);
        trip.dPlan[static_cast<std::size_t>(last)] = now - 15;
        trip.trainId = 0;
        state.trains[0].tripId = trip.id;
        // Alighting ended on plan and the train then waited 15 s for the dead end, or the
        // alighting itself ended 15 s late.
        state.trains[0].dBase = lateAlighting ? now : now - 15;

        arm(state, 0, EventType::LeaveToDeadEnd, now);
        DirtySet dirty = dirtyFor(state);
        handleTrain(state, 0, now, dirty);

        QCOMPARE(state.trains[0].state, TrainState::ToDeadEnd);
        const int32_t expected = lateAlighting ? 15 : 0;
        QCOMPARE(state.trains[0].lateness, expected);
        QCOMPARE(platformAt(state, static_cast<int>(Dir::I), last).lastShift, expected);
    }
}

void TrainEventsTest::turnaroundChain() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    const int last = state.line.n - 1;
    pullOut(state, 0);
    placeAtPlatform(state, 0, Dir::I, last);
    trackPushBack(state, Dir::I, 0);

    int rolls = 0;
    state.delayOverride = [&rolls](int, SimTime) {
        ++rolls;
        return 0;
    };

    arm(state, 0, EventType::LeaveToDeadEnd, now);
    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::ToDeadEnd);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupant, 0);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupiedSince, now);
    QCOMPARE(state.tracks[0].head, -1);
    QCOMPARE(state.tracks[0].tail, -1);
    QCOMPARE(ahead(state, 0), -1);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), last).occupant, -1);

    arm(state, 0, EventType::DeadEndReached, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::CabChange);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupant, 0);

    arm(state, 0, EventType::CabChangeDone, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::InDeadEndWaiting);

    arm(state, 0, EventType::LeaveDeadEnd, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::FromDeadEnd);
    QCOMPARE(state.trains[0].dir, Dir::II);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupant, 0);

    arm(state, 0, EventType::PlatformReachedFromDeadEnd, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    const ubahn::core::Train& train = state.trains[0];
    QCOMPARE(train.state, TrainState::WaitingSlot);
    QCOMPARE(train.dir, Dir::II);
    QCOMPARE(train.pos, 0);
    QCOMPARE(train.aAct, now);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupant, -1);
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].deadEnd.freeAt, now);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::II), 0).occupant, 0);
    QCOMPARE(state.tracks[static_cast<std::size_t>(Dir::II)].head, 0);
    QCOMPARE(state.tracks[static_cast<std::size_t>(Dir::II)].tail, 0);
    QCOMPARE(rolls, 1);
    QVERIFY(dirty.contains(0));
    QVERIFY(dirty.contains(dispatcherOwner(state.params.line.fleetSize, Term::B)));
}

void TrainEventsTest::depotChain() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    const int last = state.line.n - 1;
    pullOut(state, 0);
    placeAtPlatform(state, 0, Dir::II, last);
    trackPushBack(state, Dir::II, 0);
    state.trains[0].flags.headingToDepot = true;
    state.trains[0].flags.catchingUp = true;
    state.trains[0].flags.slowed = true;
    state.trains[0].flags.held = true;
    state.trains[0].flags.offHours = true;

    int rolls = 0;
    state.delayOverride = [&rolls](int, SimTime) {
        ++rolls;
        return 0;
    };

    arm(state, 0, EventType::LeaveToDepot, now);
    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::ToDepot);
    QCOMPARE(state.trains[0].depotAt, Term::A);
    QCOMPARE(state.terms[0].throat.occupant, 0);
    QCOMPARE(state.tracks[static_cast<std::size_t>(Dir::II)].head, -1);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::II), last).occupant, -1);

    arm(state, 0, EventType::DepotReached, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    const ubahn::core::Train& parked = state.trains[0];
    QCOMPARE(parked.state, TrainState::InDepot);
    QCOMPARE(parked.depotAt, Term::A);
    QVERIFY(!parked.flags.headingToDepot);
    QVERIFY(!parked.flags.catchingUp);
    QVERIFY(!parked.flags.slowed);
    QVERIFY(!parked.flags.held);
    QVERIFY(!parked.flags.offHours);
    QCOMPARE(parked.tripId, -1);
    QVERIFY(depotContains(state, Term::A, 0));
    QCOMPARE(state.terms[0].throat.occupant, -1);
    QCOMPARE(state.terms[0].throat.freeAt, now);
    QVERIFY(dirty.contains(dispatcherOwner(state.params.line.fleetSize, Term::A)));

    arm(state, 0, EventType::LeaveDepot, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::LeavingDepot);
    QVERIFY(!state.trains[0].flags.offHours);
    QVERIFY(!depotContains(state, Term::A, 0));
    QCOMPARE(state.terms[0].throat.occupant, 0);
    QCOMPARE(state.trains[0].dir, Dir::I);

    arm(state, 0, EventType::PlatformReachedFromDepot, now);
    dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);
    QCOMPARE(state.trains[0].state, TrainState::WaitingSlot);
    QCOMPARE(state.trains[0].pos, 0);
    QCOMPARE(state.terms[0].throat.occupant, -1);
    QCOMPARE(state.terms[0].throat.freeAt, now);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), 0).occupant, 0);
    QCOMPARE(state.tracks[static_cast<std::size_t>(Dir::I)].tail, 0);
    QCOMPARE(rolls, 1);
    QVERIFY(dirty.contains(dispatcherOwner(state.params.line.fleetSize, Term::A)));
}

void TrainEventsTest::leaveDepotOffHoursFollowsClock() {
    EngineState state = serviceState(1);
    const SimTime closed = 5 * 3600;
    QVERIFY(windowAt(state.schedule, closed) == nullptr);
    arm(state, 2, EventType::LeaveDepot, closed);

    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 2, closed, dirty);

    QCOMPARE(state.trains[2].state, TrainState::LeavingDepot);
    QVERIFY(state.trains[2].flags.offHours);
    QCOMPARE(state.trains[2].depotAt, Term::B);
    QVERIFY(!depotContains(state, Term::B, 2));
    QCOMPARE(state.terms[static_cast<std::size_t>(Term::B)].throat.occupant, 2);
}

void TrainEventsTest::drawOutsideServiceIsZero() {
    EngineState state = serviceState(1, 42);
    const SimTime closed = 5 * 3600;
    int calls = 0;
    state.delayOverride = [&calls](int, SimTime) {
        ++calls;
        return 40;
    };
    state.trains[0].dir = Dir::I;
    state.trains[0].pos = 2;

    QCOMPARE(drawDelay(state, 0, closed), 0);
    QVERIFY(state.pendingDelays.empty());
    QCOMPARE(calls, 0);
}

void TrainEventsTest::drawOverrideReplacesRoll() {
    EngineState state = serviceState(0, 42);
    const SimTime now = state.now;
    state.trains[0].dir = Dir::II;
    state.trains[0].pos = 3;
    int calls = 0;
    state.delayOverride = [&calls](int, SimTime) {
        ++calls;
        return 25;
    };

    QCOMPARE(drawDelay(state, 0, now), 25);
    QCOMPARE(calls, 1);
    QCOMPARE(static_cast<int>(state.pendingDelays.size()), 1);
    QCOMPARE(state.pendingDelays[0].trainId, 0);
    QCOMPARE(state.pendingDelays[0].dir, Dir::II);
    QCOMPARE(state.pendingDelays[0].station, stationOf(state.line, Dir::II, 3));
    QCOMPARE(state.pendingDelays[0].station, 4);
    QCOMPARE(state.pendingDelays[0].seconds, 25);

    // p = 1 so the follow-up roll reads rng. It matches a fresh generator when the
    // override itself did not draw.
    state.delayOverride = nullptr;
    state.schedule.days[0].windows[0].delayProb = 1;
    EngineState fresh = serviceState(1, 42);
    fresh.trains[0].dir = Dir::II;
    fresh.trains[0].pos = 3;
    QCOMPARE(drawDelay(state, 0, now), drawDelay(fresh, 0, fresh.now));
}

void TrainEventsTest::drawCertainDelayFollowsRng() {
    EngineState state = serviceState(1, 42);
    state.trains[0].dir = Dir::II;
    state.trains[0].pos = 3;
    const int32_t delay = drawDelay(state, 0, state.now);

    EngineState probe = serviceState(1, 42);
    const ubahn::api::Window* window = windowAt(probe.schedule, probe.now);
    QVERIFY(window != nullptr);
    int32_t expected = 0;
    if (probe.rng.uniform01() < window->delayProb) {
        expected = probe.rng.uniformInt(probe.schedule.delayMin, probe.schedule.delayMax);
    }
    QCOMPARE(delay, expected);
    // Rng on this backend is still the BE-02 stub, so expected is 0 and the range
    // body stays dormant until uniformInt actually lands in [delayMin, delayMax].
    if (expected > 0) {
        QVERIFY(delay >= state.schedule.delayMin);
        QVERIFY(delay <= state.schedule.delayMax);
        QCOMPARE(static_cast<int>(state.pendingDelays.size()), 1);
        QCOMPARE(state.pendingDelays[0].station, 4);
        QCOMPARE(state.pendingDelays[0].dir, Dir::II);
        QCOMPARE(state.pendingDelays[0].seconds, delay);
    } else {
        QVERIFY(state.pendingDelays.empty());
    }
}

void TrainEventsTest::occupiedPlatformIsNotApplied() {
    EngineState state = serviceState(0);
    const SimTime now = state.now;
    pullOut(state, 0);
    pullOut(state, 1);
    placeOnSpan(state, 0, Dir::I, 2, now - 80, now);
    placeAtPlatform(state, 1, Dir::I, 3);
    int rolls = 0;
    state.delayOverride = [&rolls](int, SimTime) {
        ++rolls;
        return 15;
    };
    arm(state, 0, EventType::Arrive, now);

    const TrainState before = state.trains[0].state;
    const int posBefore = state.trains[0].pos;
    DirtySet dirty = dirtyFor(state);
    handleTrain(state, 0, now, dirty);

    QCOMPARE(state.trains[0].state, before);
    QCOMPARE(state.trains[0].pos, posBefore);
    QCOMPARE(state.trains[0].drawnDelay, 0);
    QCOMPARE(platformAt(state, static_cast<int>(Dir::I), 3).occupant, 1);
    QCOMPARE(rolls, 0);
    QVERIFY(state.pendingDelays.empty());
    QVERIFY(dirty.contains(0));
    QVERIFY(!dirty.contains(1));
    QCOMPARE(dirty.size(), 1);
}

void TrainEventsTest::rescheduleKeepsPosition() {
    EngineState state = serviceState(0);
    pullOut(state, 0);
    // Span 0 is 1100 m. Halfway from t=1000 to t=1120 is t=1060, x=550.
    placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    const SimTime now = 1060;
    const double before = 550.0;
    const ubahn::core::Span& span = spanOf(state.line, Dir::I, 0);

    ubahn::core::applyReschedule(state, 0, NextEvent{EventType::Arrive, 1200, true}, now);

    const ubahn::core::Train& train = state.trains[0];
    QCOMPARE(train.seg.t0, now);
    QCOMPARE(train.seg.x0, before);
    QCOMPARE(train.seg.x1, span.lengthM);
    QCOMPARE(train.seg.t1, static_cast<SimTime>(1200));
    const double speed =
        (train.seg.x1 - train.seg.x0) / static_cast<double>(train.seg.t1 - train.seg.t0);
    const double oldSpeed = span.lengthM / 120.0;
    QVERIFY(std::abs(speed - oldSpeed) > 1e-6);
    // 1200 - departAct 1000 = 200 > tStd 120.
    QVERIFY(train.flags.slowed);
    QVERIFY(!train.flags.catchingUp);
    QVERIFY(!train.flags.held);
}

QTEST_APPLESS_MAIN(TrainEventsTest)

#include "tst_train_events.moc"
