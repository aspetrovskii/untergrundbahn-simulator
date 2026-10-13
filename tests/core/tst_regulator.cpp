#include "EngineFixture.h"
#include "core/Regulator.h"

#include <QtTest>
#include <algorithm>
#include <cstdint>

using ubahn::api::Dir;
using ubahn::api::kNoTime;
using ubahn::api::SimTime;
using ubahn::api::Term;
using ubahn::core::DwellDecision;
using ubahn::core::EngineState;
using ubahn::core::EventType;
using ubahn::core::NextEvent;
using ubahn::core::Span;
using ubahn::core::Train;
using ubahn::core::Trip;

namespace {

EngineState newState() {
    return ubahn::core::makeState(
        ubahn::core::makeParams(ubahn::core::trainingLineConfig(),
                                ubahn::core::constantSchedule(180, 60, 0.0), 4),
        1);
}

void fillPlan(EngineState& state, Trip& trip) {
    const int n = state.line.n;
    trip.aPlan.assign(static_cast<std::size_t>(n), trip.slot);
    trip.dPlan.assign(static_cast<std::size_t>(n), trip.slot);
    for (int pos = 0; pos < n - 1; ++pos) {
        const Span& span = ubahn::core::spanOf(state.line, trip.dir, pos);
        trip.aPlan[static_cast<std::size_t>(pos + 1)] =
            trip.dPlan[static_cast<std::size_t>(pos)] + span.tStd;
        trip.dPlan[static_cast<std::size_t>(pos + 1)] =
            trip.aPlan[static_cast<std::size_t>(pos + 1)] + trip.dwellPlan;
    }
}

void assignTrip(EngineState& state, int train, Trip& trip) {
    state.trains[static_cast<std::size_t>(train)].tripId = trip.id;
    trip.trainId = train;
}

NextEvent at(EventType type, SimTime time) {
    NextEvent next;
    next.type = type;
    next.time = time;
    next.valid = true;
    return next;
}

// Macros, so a failure returns from the test slot rather than from a helper.
#define EXPECT_EVENT(got, eventType, eventTime)                                                    \
    do {                                                                                           \
        QVERIFY((got).valid);                                                                      \
        QCOMPARE(static_cast<int>((got).type), static_cast<int>(eventType));                       \
        QCOMPARE((got).time, static_cast<SimTime>(eventTime));                                     \
    } while (false)

Trip& planTrip(EngineState& state, int train, Dir dir, SimTime slot, int32_t dwell) {
    Trip& trip = ubahn::core::addTrip(state, dir, slot, dwell);
    fillPlan(state, trip);
    assignTrip(state, train, trip);
    return trip;
}

} // namespace

class RegulatorTest : public QObject {
    Q_OBJECT

private slots:
    void onTimeDepartsAtPlan();
    void lateDwellShrinksToMinimumNotBeforePlan();
    void closeLeaderSlowsWithoutHold();
    void wideGapHolds();
    void runningDoesNotSpeedUp();
    void runningSlowsSmoothly();
    void runningStopsAtSignal();
    void stoppedAtSignalWaitsApproach();
    void spanResultIgnoresNow();
    void earliestArrivalHonoursHeadwayAndClear();
    void earliestArrivalUsesLeaderProjection();
    void deadEndWaitDelaysTurnaround();
    void headingToDepotUsesThroat();
    void deadEndAndDepotFormulas();
    void inDepotWithoutTripHasNoEvent();
    void delayAheadOnlyPostpones();
    void functionsDoNotMutateState();
    void outOfRangeIsSafe();
};

void RegulatorTest::onTimeDepartsAtPlan() {
    EngineState state = newState();
    const Span& span = ubahn::core::spanOf(state.line, Dir::I, 0);
    QCOMPARE(span.tStd, 120);
    QCOMPARE(span.tMin, 80);
    QCOMPARE(span.tMax, 156);

    Trip& trip = planTrip(state, 0, Dir::I, 20000, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 0);
    Train& train = state.trains[0];
    train.aAct = 19960;
    train.drawnDelay = 0;
    QCOMPARE(trip.dPlan[0], static_cast<SimTime>(20000));
    QCOMPARE(trip.aPlan[1], static_cast<SimTime>(20120));

    const DwellDecision decision = ubahn::core::dwellDecision(state, 0);
    QCOMPARE(decision.depart, trip.dPlan[0]);
    QVERIFY(!decision.held);
    QVERIFY(!decision.catchingUp);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, state.now)), EventType::Depart, 20000);
}

void RegulatorTest::lateDwellShrinksToMinimumNotBeforePlan() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 20000, 60);
    QCOMPARE(trip.aPlan[1], static_cast<SimTime>(20120));
    QCOMPARE(trip.dPlan[1], static_cast<SimTime>(20180));
    QCOMPARE(trip.aPlan[2], static_cast<SimTime>(20270));

    ubahn::core::placeAtPlatform(state, 0, Dir::I, 1);
    Train& train = state.trains[0];
    train.drawnDelay = 0;

    train.aAct = 20150;
    DwellDecision decision = ubahn::core::dwellDecision(state, 0);
    QCOMPARE(decision.depart, static_cast<SimTime>(20190));
    QCOMPARE(decision.depart - train.aAct, static_cast<SimTime>(40));
    QVERIFY(decision.depart >= trip.dPlan[1]);
    QVERIFY(decision.catchingUp);
    QVERIFY(!decision.held);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::Depart, 20190);

    // 10 s late: dwellMin would leave before dPlan, so C8 keeps the plan.
    train.aAct = 20130;
    decision = ubahn::core::dwellDecision(state, 0);
    QCOMPARE(decision.depart, trip.dPlan[1]);
    QVERIFY(decision.depart - train.aAct > 40);
    QVERIFY(decision.catchingUp);
    QVERIFY(!decision.held);
}

void RegulatorTest::closeLeaderSlowsWithoutHold() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 20000, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 0);
    state.trains[0].aAct = 19960;
    state.trains[0].drawnDelay = 0;

    ubahn::core::placeAtPlatform(state, 1, Dir::I, 1);
    ubahn::core::setNext(state, 1, at(EventType::Depart, 20105));

    const DwellDecision decision = ubahn::core::dwellDecision(state, 0);
    const SimTime earliest = ubahn::core::earliestArrival(state, 0, 1);
    QCOMPARE(earliest, static_cast<SimTime>(20130));
    const int64_t run = earliest - decision.depart;
    QVERIFY(run > ubahn::core::spanOf(state.line, Dir::I, 0).tStd);
    QVERIFY(run <= ubahn::core::spanOf(state.line, Dir::I, 0).tMax);
    QCOMPARE(decision.depart, trip.dPlan[0]);
    QVERIFY(!decision.held);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::Depart, decision.depart);
}

void RegulatorTest::wideGapHolds() {
    EngineState state = newState();
    planTrip(state, 0, Dir::I, 20000, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 0);
    state.trains[0].aAct = 19960;
    state.trains[0].drawnDelay = 0;

    ubahn::core::placeAtPlatform(state, 1, Dir::I, 1);
    ubahn::core::setNext(state, 1, at(EventType::Depart, 20175));

    const DwellDecision decision = ubahn::core::dwellDecision(state, 0);
    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), static_cast<SimTime>(20200));
    QCOMPARE(decision.depart, static_cast<SimTime>(20044));
    QVERIFY(decision.held);
    QVERIFY(decision.depart > 20000);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::Depart, 20044);
}

void RegulatorTest::runningDoesNotSpeedUp() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 1000, 60);
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    ubahn::core::setNext(state, 0, at(EventType::Arrive, 1120));
    trip.aPlan[1] = 1100;

    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), static_cast<SimTime>(1100));
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 1000)), EventType::Arrive, 1120);
}

void RegulatorTest::runningSlowsSmoothly() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 1000, 60);
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    ubahn::core::setNext(state, 0, at(EventType::Arrive, 1120));
    trip.aPlan[1] = 1150;

    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 1000)), EventType::Arrive, 1150);
}

void RegulatorTest::runningStopsAtSignal() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 1000, 60);
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    ubahn::core::setNext(state, 0, at(EventType::Arrive, 1120));
    trip.aPlan[1] = 1300;

    // 900 m of 1100 m over 120 s: ceil(98.181...) = 99, so 1000 + 99.
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 1000)), EventType::ReachSignal, 1099);
}

void RegulatorTest::stoppedAtSignalWaitsApproach() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 1000, 60);
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    Train& train = state.trains[0];
    train.state = ubahn::core::TrainState::StoppedAtSignal;
    train.signalAt = 5000;

    trip.aPlan[1] = 5010;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 1)), EventType::Arrive, 5020);

    trip.aPlan[1] = 5100;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 2)), EventType::Arrive, 5100);
}

void RegulatorTest::spanResultIgnoresNow() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 1000, 60);
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1000, 1120);
    ubahn::core::setNext(state, 0, at(EventType::Arrive, 1120));
    trip.aPlan[1] = 1300;

    const NextEvent early = ubahn::core::computeNext(state, 0, 0);
    const NextEvent late = ubahn::core::computeNext(state, 0, 500000);
    QVERIFY(early == late);
    EXPECT_EVENT(early, EventType::ReachSignal, 1099);

    state.trains[0].state = ubahn::core::TrainState::StoppedAtSignal;
    state.trains[0].signalAt = 5000;
    trip.aPlan[1] = 5100;
    QVERIFY(ubahn::core::computeNext(state, 0, 0) == ubahn::core::computeNext(state, 0, 800000));
}

void RegulatorTest::earliestArrivalHonoursHeadwayAndClear() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 0, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 0);
    trip.aPlan[1] = 1000;

    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 1);
    platform.lastArrival = 5000;
    platform.lastDeparture = 4800;
    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), static_cast<SimTime>(5065));

    platform.occupant = 1;
    ubahn::core::setNext(state, 1, at(EventType::Depart, 6000));
    QCOMPARE(ubahn::core::platformFreeAt(state, Dir::I, 1), static_cast<SimTime>(6000));
    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), static_cast<SimTime>(6025));
}

void RegulatorTest::earliestArrivalUsesLeaderProjection() {
    EngineState state = newState();
    planTrip(state, 0, Dir::I, 0, 60);
    planTrip(state, 1, Dir::I, 0, 60);
    // Index after both inserts: addTrip can reallocate state.trips.
    Trip& follower = state.trips[static_cast<std::size_t>(state.trains[0].tripId)];
    Trip& leader = state.trips[static_cast<std::size_t>(state.trains[1].tripId)];
    follower.aPlan[1] = 1000;
    leader.aPlan[1] = 2000;
    leader.dPlan[1] = 2060;
    leader.dwellPlan = 60;

    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 1500, 2000);
    ubahn::core::placeOnSpan(state, 1, Dir::I, 0, 1400, 2000);
    ubahn::core::setNext(state, 1, at(EventType::Arrive, 2100));
    state.trains[0].prevOnTrack = 1;
    state.trains[1].nextOnTrack = 0;

    QCOMPARE(ubahn::core::projectedArrival(state, 1), static_cast<SimTime>(2100));
    QCOMPARE(ubahn::core::projectedDepart(state, 1, 1), static_cast<SimTime>(2140));
    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), static_cast<SimTime>(2165));
}

void RegulatorTest::deadEndWaitDelaysTurnaround() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 20000, 60);
    const int last = state.line.n - 1;
    ubahn::core::placeAtPlatform(state, 0, Dir::I, last);
    Train& train = state.trains[0];
    train.aAct = trip.aPlan[static_cast<std::size_t>(last)];
    train.drawnDelay = 0;
    const SimTime dBase = trip.dPlan[static_cast<std::size_t>(last)];

    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveToDeadEnd, dBase);

    train.drawnDelay = 15;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveToDeadEnd, dBase + 15);
    train.drawnDelay = 0;

    const SimTime reached = dBase + 1000;
    state.trains[1].state = ubahn::core::TrainState::ToDeadEnd;
    ubahn::core::setNext(state, 1, at(EventType::DeadEndReached, reached));
    state.terms[static_cast<std::size_t>(Term::B)].deadEnd.occupant = 1;
    const SimTime freeAt = reached + 30 + 45;
    QCOMPARE(
        ubahn::core::resourceFreeAt(state, state.terms[static_cast<std::size_t>(Term::B)].deadEnd),
        freeAt);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveToDeadEnd, freeAt);

    // Path II arrives at A.
    Trip& back = planTrip(state, 2, Dir::II, 20000, 60);
    ubahn::core::placeAtPlatform(state, 2, Dir::II, last);
    state.trains[2].aAct = back.aPlan[static_cast<std::size_t>(last)];
    state.trains[2].drawnDelay = 0;
    const SimTime backBase = back.dPlan[static_cast<std::size_t>(last)];
    state.trains[3].state = ubahn::core::TrainState::ToDeadEnd;
    ubahn::core::setNext(state, 3, at(EventType::DeadEndReached, backBase + 500));
    state.terms[static_cast<std::size_t>(Term::A)].deadEnd.occupant = 3;
    EXPECT_EVENT((ubahn::core::computeNext(state, 2, 0)), EventType::LeaveToDeadEnd,
                 backBase + 500 + 75);
}

void RegulatorTest::headingToDepotUsesThroat() {
    EngineState state = newState();
    Trip& trip = planTrip(state, 0, Dir::I, 20000, 60);
    const int last = state.line.n - 1;
    ubahn::core::placeAtPlatform(state, 0, Dir::I, last);
    Train& train = state.trains[0];
    train.aAct = trip.aPlan[static_cast<std::size_t>(last)];
    train.drawnDelay = 0;
    train.flags.headingToDepot = true;
    const SimTime dBase = trip.dPlan[static_cast<std::size_t>(last)];

    state.trains[1].state = ubahn::core::TrainState::ToDepot;
    ubahn::core::setNext(state, 1, at(EventType::DepotReached, dBase + 400));
    state.terms[static_cast<std::size_t>(Term::B)].throat.occupant = 1;
    QCOMPARE(
        ubahn::core::resourceFreeAt(state, state.terms[static_cast<std::size_t>(Term::B)].throat),
        dBase + 400);
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveToDepot, dBase + 400);
}

void RegulatorTest::deadEndAndDepotFormulas() {
    EngineState state = newState();
    const ubahn::api::LineConfig& cfg = state.line.cfg;

    state.trains[0].state = ubahn::core::TrainState::ToDeadEnd;
    ubahn::core::setNext(state, 0, at(EventType::LeaveToDeadEnd, 1000));
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::DeadEndReached,
                 1000 + cfg.toDeadEnd);

    state.trains[0].state = ubahn::core::TrainState::CabChange;
    ubahn::core::setNext(state, 0, at(EventType::DeadEndReached, 1045));
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::CabChangeDone,
                 1045 + cfg.cabChange);

    state.trains[0].state = ubahn::core::TrainState::InDeadEndWaiting;
    state.trains[0].dir = Dir::I;
    ubahn::core::setNext(state, 0, at(EventType::CabChangeDone, 10000));
    ubahn::core::Platform& dep = ubahn::core::platformAt(state, static_cast<int>(Dir::II), 0);
    dep.lastDeparture = 10200;
    dep.lastArrival = 10100;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveDeadEnd, 10180);

    dep.lastDeparture = kNoTime;
    dep.lastArrival = kNoTime;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::LeaveDeadEnd, 10000);

    state.trains[0].state = ubahn::core::TrainState::FromDeadEnd;
    state.trains[0].dir = Dir::II;
    ubahn::core::setNext(state, 0, at(EventType::LeaveDeadEnd, 1000));
    ubahn::core::platformAt(state, static_cast<int>(Dir::II), 0).lastArrival = 50000;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::PlatformReachedFromDeadEnd,
                 50065);

    state.trains[0].state = ubahn::core::TrainState::ToDepot;
    ubahn::core::setNext(state, 0, at(EventType::LeaveToDepot, 2000));
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::DepotReached,
                 2000 + cfg.depotIn);

    state.trains[0].state = ubahn::core::TrainState::LeavingDepot;
    state.trains[0].depotAt = Term::A;
    ubahn::core::setNext(state, 0, at(EventType::LeaveDepot, 3000));
    ubahn::core::platformAt(state, static_cast<int>(Dir::I), 0).lastArrival = kNoTime;
    EXPECT_EVENT((ubahn::core::computeNext(state, 0, 0)), EventType::PlatformReachedFromDepot,
                 3000 + cfg.depotOut);

    // Train 2 starts in depot B. The binding sample is on B's departure platform.
    Trip& trip = planTrip(state, 2, Dir::II, 50000, 60);
    QVERIFY(state.trains[2].state == ubahn::core::TrainState::InDepot);
    QCOMPARE(state.trains[2].depotAt, Term::B);
    ubahn::core::platformAt(state, static_cast<int>(Dir::II), 0).lastArrival = 50000;
    const SimTime bySlot = trip.slot - cfg.depotOut - cfg.dwellMin;
    const SimTime byHeadway = 50000 + cfg.headwayMin - cfg.depotOut;
    EXPECT_EVENT((ubahn::core::computeNext(state, 2, 0)), EventType::LeaveDepot,
                 (std::max(bySlot, byHeadway)));
    QCOMPARE(std::max(bySlot, byHeadway), static_cast<SimTime>(49975));

    state.trains[3].state = ubahn::core::TrainState::LeavingDepot;
    ubahn::core::setNext(state, 3, at(EventType::PlatformReachedFromDepot, 60000));
    state.terms[static_cast<std::size_t>(Term::B)].throat.occupant = 3;
    EXPECT_EVENT((ubahn::core::computeNext(state, 2, 1)), EventType::LeaveDepot, 60000);

    ubahn::core::placeInDeadEnd(state, 0, Term::B);
    ubahn::core::setNext(state, 0, at(EventType::LeaveDeadEnd, 30000));
    QCOMPARE(ubahn::core::readyAt(state, 0), static_cast<SimTime>(30000 + cfg.fromDeadEnd));

    ubahn::core::placeAtPlatform(state, 1, Dir::I, 0);
    state.trains[1].aAct = 12345;
    QCOMPARE(ubahn::core::readyAt(state, 1), static_cast<SimTime>(12345));
}

void RegulatorTest::inDepotWithoutTripHasNoEvent() {
    const EngineState state = newState();
    const NextEvent first = ubahn::core::computeNext(state, 0, 0);
    const NextEvent second = ubahn::core::computeNext(state, 0, 999999);
    QVERIFY(!first.valid);
    QVERIFY(first == second);
    QCOMPARE(state.trains[0].state, ubahn::core::TrainState::InDepot);
    QCOMPARE(state.trains[0].tripId, -1);
}

void RegulatorTest::delayAheadOnlyPostpones() {
    EngineState state = newState();
    planTrip(state, 0, Dir::I, 20000, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 0);
    state.trains[0].aAct = 19960;
    state.trains[0].drawnDelay = 0;
    ubahn::core::placeAtPlatform(state, 1, Dir::I, 1);

    ubahn::core::setNext(state, 1, at(EventType::Depart, 20175));
    const SimTime held = ubahn::core::computeNext(state, 0, 0).time;
    ubahn::core::setNext(state, 1, at(EventType::Depart, 20275));
    const SimTime heldLater = ubahn::core::computeNext(state, 0, 0).time;
    QVERIFY(heldLater > held);

    ubahn::core::setNext(state, 1, at(EventType::Depart, 20105));
    const SimTime slack = ubahn::core::computeNext(state, 0, 0).time;
    ubahn::core::setNext(state, 1, at(EventType::Depart, 20120));
    const SimTime slackLater = ubahn::core::computeNext(state, 0, 0).time;
    QVERIFY(slackLater >= slack);

    ubahn::core::platformAt(state, 0, 0).occupant = -1;
    ubahn::core::platformAt(state, 0, 1).occupant = -1;
    Trip& follower = state.trips[static_cast<std::size_t>(state.trains[0].tripId)];
    follower.aPlan[1] = 10000;
    ubahn::core::placeOnSpan(state, 0, Dir::I, 0, 10000, 10120);
    ubahn::core::setNext(state, 0, at(EventType::Arrive, 10080));

    Trip& leader = planTrip(state, 1, Dir::I, 9000, 40);
    leader.aPlan[1] = 0;
    leader.dPlan[1] = 0;
    ubahn::core::placeOnSpan(state, 1, Dir::I, 0, 9000, 10100);
    state.trains[0].prevOnTrack = 1;
    state.trains[1].nextOnTrack = 0;
    state.trains[1].prevOnTrack = -1;

    ubahn::core::setNext(state, 1, at(EventType::Arrive, 10035));
    const NextEvent first = ubahn::core::computeNext(state, 0, 10000);
    ubahn::core::setNext(state, 1, at(EventType::Arrive, 10075));
    const NextEvent second = ubahn::core::computeNext(state, 0, 10000);
    EXPECT_EVENT(first, EventType::Arrive, 10100);
    EXPECT_EVENT(second, EventType::Arrive, 10140);
    QVERIFY(second.time >= first.time);
}

void RegulatorTest::functionsDoNotMutateState() {
    EngineState state = newState();
    planTrip(state, 0, Dir::I, 20000, 60);
    ubahn::core::placeAtPlatform(state, 0, Dir::I, 1);
    state.trains[0].aAct = 20120;
    ubahn::core::setNext(state, 0, at(EventType::Depart, 20180));
    const Train before = state.trains[0];
    const int occupant = ubahn::core::platformAt(state, 0, 1).occupant;
    const int trips = static_cast<int>(state.trips.size());
    const SimTime now = state.now;

    ubahn::core::computeNext(state, 0, now + 50);
    ubahn::core::earliestArrival(state, 0, 2);
    ubahn::core::platformFreeAt(state, Dir::I, 1);
    ubahn::core::projectedArrival(state, 0);
    ubahn::core::projectedDepart(state, 0, 1);
    ubahn::core::resourceFreeAt(state, state.terms[0].deadEnd);
    ubahn::core::dwellDecision(state, 0);
    ubahn::core::readyAt(state, 0);

    QCOMPARE(state.trains[0].state, before.state);
    QCOMPARE(state.trains[0].aAct, before.aAct);
    QCOMPARE(state.trains[0].dBase, before.dBase);
    QCOMPARE(state.trains[0].pos, before.pos);
    QCOMPARE(state.trains[0].drawnDelay, before.drawnDelay);
    QVERIFY(state.trains[0].next == before.next);
    QCOMPARE(state.trains[0].flags.held, before.flags.held);
    QCOMPARE(state.trains[0].flags.catchingUp, before.flags.catchingUp);
    QCOMPARE(ubahn::core::platformAt(state, 0, 1).occupant, occupant);
    QCOMPARE(static_cast<int>(state.trips.size()), trips);
    QCOMPARE(state.now, now);
}

void RegulatorTest::outOfRangeIsSafe() {
    const EngineState state = newState();
    QVERIFY(!ubahn::core::computeNext(state, -1, 0).valid);
    QVERIFY(!ubahn::core::computeNext(state, 50, 0).valid);
    QCOMPARE(ubahn::core::earliestArrival(state, -1, 1), kNoTime);
    QCOMPARE(ubahn::core::earliestArrival(state, 0, 1), kNoTime);
    QCOMPARE(ubahn::core::platformFreeAt(state, Dir::I, 99), kNoTime);
    QCOMPARE(ubahn::core::projectedArrival(state, 3), kNoTime);
    QCOMPARE(ubahn::core::projectedDepart(state, 0, 1), kNoTime);
    QCOMPARE(ubahn::core::readyAt(state, -1), kNoTime);
    QCOMPARE(ubahn::core::readyAt(state, 0), kNoTime);
}

QTEST_APPLESS_MAIN(RegulatorTest)
#include "tst_regulator.moc"
