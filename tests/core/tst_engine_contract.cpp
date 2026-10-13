#include "EngineFixture.h"
#include "core/Invariants.h"
#include "core/Time.h"

#include <QtTest>
#include <cstdint>
#include <vector>

namespace {

ubahn::api::SimParams trainingParams(int fleet) {
    return ubahn::core::makeParams(ubahn::core::trainingLineConfig(),
                                   ubahn::core::constantSchedule(120, 40, 0), fleet);
}

} // namespace

class EngineContractTest : public QObject {
    Q_OBJECT

private slots:
    void depotLayout();
    void depotSplitOdd();
    void platformEnds();
    void trackOrder();
    void priorityOfEvents();
    void dirtySetIsFifoWithoutDuplicates();
    void nextEventEquality();
    void handlersLink();
    void fixturePlacement();
};

void EngineContractTest::depotLayout() {
    const ubahn::api::SimParams params = trainingParams(26);
    const ubahn::core::EngineState state = ubahn::core::makeState(params, 42);

    QCOMPARE(state.seed, static_cast<uint64_t>(42));
    QCOMPARE(state.line.n, 8);
    QCOMPARE(state.now, ubahn::core::warmupFrom(params.period.start));
    QCOMPARE(static_cast<int>(state.trains.size()), 26);
    QVERIFY(!state.internalError.has_value());
    QVERIFY(!state.delayOverride);
    QVERIFY(state.trips.empty());
    QCOMPARE(state.nextTripId, 0);
    QVERIFY(state.heap.empty());
    QCOMPARE(state.heap.size(), 0);
    QCOMPARE(state.tracks[0].head, -1);
    QCOMPARE(state.tracks[0].tail, -1);
    QCOMPARE(state.tracks[1].head, -1);
    QCOMPARE(static_cast<int>(state.sentTrains.size()), 26);
    QCOMPARE(static_cast<int>(state.sentTrainAt.size()), 26);
    QCOMPARE(static_cast<int>(state.sentBoards[0].size()), 8);
    QCOMPARE(static_cast<int>(state.sentBoards[1].size()), 8);
    QCOMPARE(static_cast<int>(state.platforms[0].size()), 8);
    QCOMPARE(static_cast<int>(state.platforms[1].size()), 8);

    QCOMPARE(static_cast<int>(state.terms[0].depot.size()), 13);
    QCOMPARE(static_cast<int>(state.terms[1].depot.size()), 13);
    for (int id = 0; id < 26; ++id) {
        const ubahn::core::Train& train = state.trains[static_cast<std::size_t>(id)];
        QCOMPARE(train.id, id);
        QCOMPARE(train.state, ubahn::core::TrainState::InDepot);
        QCOMPARE(train.tripId, -1);
        QCOMPARE(train.latenessSince, static_cast<ubahn::api::SimTime>(-1));
        QVERIFY(!train.next.valid);
        QVERIFY(!train.flags.headingToDepot);
        if (id < 13) {
            QCOMPARE(state.terms[0].depot[static_cast<std::size_t>(id)], id);
            QCOMPARE(train.depotAt, ubahn::api::Term::A);
            QCOMPARE(train.dir, ubahn::api::Dir::I);
        } else {
            QCOMPARE(state.terms[1].depot[static_cast<std::size_t>(id - 13)], id);
            QCOMPARE(train.depotAt, ubahn::api::Term::B);
            QCOMPARE(train.dir, ubahn::api::Dir::II);
        }
    }

    QVERIFY(ubahn::core::checkState(state).empty());

    ubahn::core::Engine engine(params, 42);
    QCOMPARE(engine.now(), state.now);
    QCOMPARE(engine.warmupFrom(), state.now);
    QVERIFY(!engine.internalError().has_value());
}

void EngineContractTest::depotSplitOdd() {
    const ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(7), 1);
    QCOMPARE(static_cast<int>(state.terms[0].depot.size()), 4);
    QCOMPARE(static_cast<int>(state.terms[1].depot.size()), 3);
    QCOMPARE(state.terms[0].depot.front(), 0);
    QCOMPARE(state.terms[1].depot.front(), 4);
    QCOMPARE(state.trains[6].depotAt, ubahn::api::Term::B);
}

void EngineContractTest::platformEnds() {
    const ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(26), 1);
    const int last = state.line.n - 1;

    QCOMPARE(
        &ubahn::core::arrivalPlatform(state, ubahn::api::Term::B),
        &state.platforms[static_cast<int>(ubahn::api::Dir::I)][static_cast<std::size_t>(last)]);
    QCOMPARE(&ubahn::core::departurePlatform(state, ubahn::api::Term::B),
             &state.platforms[static_cast<int>(ubahn::api::Dir::II)][0]);
    QCOMPARE(
        &ubahn::core::arrivalPlatform(state, ubahn::api::Term::A),
        &state.platforms[static_cast<int>(ubahn::api::Dir::II)][static_cast<std::size_t>(last)]);
    QCOMPARE(&ubahn::core::departurePlatform(state, ubahn::api::Term::A),
             &state.platforms[static_cast<int>(ubahn::api::Dir::I)][0]);

    QCOMPARE(ubahn::core::termOfArrival(ubahn::api::Dir::I), ubahn::api::Term::B);
    QCOMPARE(ubahn::core::termOfArrival(ubahn::api::Dir::II), ubahn::api::Term::A);
    QCOMPARE(ubahn::core::termOfDeparture(ubahn::api::Dir::I), ubahn::api::Term::A);
    QCOMPARE(ubahn::core::termOfDeparture(ubahn::api::Dir::II), ubahn::api::Term::B);

    QCOMPARE(ubahn::core::dispatcherOwner(26, ubahn::api::Term::A), 26);
    QCOMPARE(ubahn::core::dispatcherOwner(26, ubahn::api::Term::B), 27);
    QCOMPARE(ubahn::core::calendarOwner(26), 28);
    QCOMPARE(ubahn::core::ownerCount(26), 29);
    QCOMPARE(ubahn::core::cascadeBudget(26), 4 * 26 + 16);
}

void EngineContractTest::trackOrder() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(26), 1);
    // Head 0, then 1, tail 2. ahead(1) is 0, behind(1) is 2 (ALGORITHM 5.5).
    state.tracks[0].head = 0;
    state.tracks[0].tail = 2;
    state.trains[0].prevOnTrack = -1;
    state.trains[0].nextOnTrack = 1;
    state.trains[1].prevOnTrack = 0;
    state.trains[1].nextOnTrack = 2;
    state.trains[2].prevOnTrack = 1;
    state.trains[2].nextOnTrack = -1;

    QCOMPARE(ubahn::core::ahead(state, 0), -1);
    QCOMPARE(ubahn::core::ahead(state, 1), 0);
    QCOMPARE(ubahn::core::behind(state, 1), 2);
    QCOMPARE(ubahn::core::behind(state, 2), -1);
    QCOMPARE(ubahn::core::ahead(state, -1), -1);
    QCOMPARE(ubahn::core::behind(state, 26), -1);
    QVERIFY(ubahn::core::tripOf(state, 0) == nullptr);
}

void EngineContractTest::priorityOfEvents() {
    struct Row {
        ubahn::core::EventType type;
        int priority;
    };
    const Row rows[] = {
        {ubahn::core::EventType::DayChange, 0},
        {ubahn::core::EventType::WindowBoundary, 0},
        {ubahn::core::EventType::ServiceOpen, 0},
        {ubahn::core::EventType::ServiceClose, 0},
        {ubahn::core::EventType::NightRebalance, 0},
        {ubahn::core::EventType::Depart, 1},
        {ubahn::core::EventType::LeaveToDeadEnd, 1},
        {ubahn::core::EventType::LeaveToDepot, 1},
        {ubahn::core::EventType::LeaveDeadEnd, 1},
        {ubahn::core::EventType::LeaveDepot, 1},
        {ubahn::core::EventType::Arrive, 2},
        {ubahn::core::EventType::ReachSignal, 2},
        {ubahn::core::EventType::DeadEndReached, 2},
        {ubahn::core::EventType::CabChangeDone, 2},
        {ubahn::core::EventType::DepotReached, 2},
        {ubahn::core::EventType::PlatformReachedFromDeadEnd, 2},
        {ubahn::core::EventType::PlatformReachedFromDepot, 2},
        {ubahn::core::EventType::DispatcherWake, 3},
    };
    for (const Row& row : rows) {
        QCOMPARE(static_cast<int>(ubahn::core::priorityOf(row.type)), row.priority);
    }
}

void EngineContractTest::dirtySetIsFifoWithoutDuplicates() {
    ubahn::core::DirtySet dirty(4);
    dirty.push(-1);
    dirty.push(1);
    dirty.push(2);
    dirty.push(1);
    dirty.push(3);
    dirty.push(4);
    QCOMPARE(dirty.size(), 3);
    QVERIFY(dirty.contains(1));
    QVERIFY(!dirty.contains(0));
    QCOMPARE(dirty.pop(), 1);
    QCOMPARE(dirty.pop(), 2);
    dirty.push(1);
    QCOMPARE(dirty.pop(), 3);
    QCOMPARE(dirty.pop(), 1);
    QVERIFY(dirty.empty());
}

void EngineContractTest::nextEventEquality() {
    ubahn::core::NextEvent left;
    ubahn::core::NextEvent right;
    QVERIFY(left == right);
    QVERIFY(!left.valid);

    right.time = 5;
    QVERIFY(!(left == right));
    right = left;
    right.valid = true;
    QVERIFY(!(left == right));
    right = left;
    right.type = ubahn::core::EventType::Depart;
    QVERIFY(!(left == right));
}

void EngineContractTest::handlersLink() {
    const ubahn::core::Handlers& real = ubahn::core::defaultHandlers();
    QVERIFY(real.handleTrain);
    QVERIFY(real.handleDispatcher);
    QVERIFY(real.handleCalendar);
    QVERIFY(real.computeNext);
    QVERIFY(real.dispatcherNext);
    QVERIFY(real.calendarNext);
    QVERIFY(real.applyReschedule);
    QVERIFY(real.collectDelta);
    QVERIFY(real.checkInvariants);

    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    const ubahn::core::Handlers fake = ubahn::core::fakeHandlers();
    const ubahn::core::NextEvent next = fake.computeNext(state, 0, state.now);
    QVERIFY(!next.valid);
    QVERIFY(!fake.dispatcherNext(state, ubahn::api::Term::A, state.now).valid);
    QVERIFY(!fake.calendarNext(state, state.now).valid);
    QVERIFY(!fake.collectDelta(state, state.now, ubahn::core::DeltaMode::Play).has_value());

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(4));
    fake.handleTrain(state, 0, state.now, dirty);
    QVERIFY(dirty.empty());
    real.checkInvariants(state);
}

void EngineContractTest::fixturePlacement() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(8), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    QCOMPARE(state.trains[0].state, ubahn::core::TrainState::WaitingSlot);
    QCOMPARE(state.platforms[0][0].occupant, 0);

    ubahn::core::placeAtPlatform(state, 1, ubahn::api::Dir::I, 3);
    QCOMPARE(state.trains[1].state, ubahn::core::TrainState::Dwelling);
    QCOMPARE(state.platforms[0][3].occupant, 1);

    ubahn::core::placeAtPlatform(state, 2, ubahn::api::Dir::I, 7);
    QCOMPARE(state.trains[2].state, ubahn::core::TrainState::AlightingTerminal);

    ubahn::core::placeOnSpan(state, 3, ubahn::api::Dir::II, 1, 100, 220);
    QCOMPARE(state.trains[3].state, ubahn::core::TrainState::Running);
    QCOMPARE(state.trains[3].dir, ubahn::api::Dir::II);
    QCOMPARE(state.trains[3].pos, 1);
    QCOMPARE(state.trains[3].departAct, static_cast<ubahn::api::SimTime>(100));
    QCOMPARE(state.trains[3].seg.t0, static_cast<ubahn::api::SimTime>(100));
    QCOMPARE(state.trains[3].seg.t1, static_cast<ubahn::api::SimTime>(220));

    ubahn::core::placeInDeadEnd(state, 4, ubahn::api::Term::B);
    QCOMPARE(state.trains[4].state, ubahn::core::TrainState::InDeadEndWaiting);
    QCOMPARE(state.trains[4].dir, ubahn::api::Dir::I);
    QCOMPARE(state.terms[static_cast<int>(ubahn::api::Term::B)].deadEnd.occupant, 4);

    const ubahn::core::Trip& trip = ubahn::core::addTrip(state, ubahn::api::Dir::I, 1000, 40);
    QCOMPARE(trip.id, 0);
    QCOMPARE(trip.trainId, -1);
    QCOMPARE(static_cast<int>(trip.aPlan.size()), state.line.n);
    QCOMPARE(static_cast<int>(trip.dPlan.size()), state.line.n);
    QCOMPARE(state.nextTripId, 1);
    QVERIFY(ubahn::core::tripOf(state, 0) == nullptr);

    ubahn::core::NextEvent depart;
    depart.type = ubahn::core::EventType::Depart;
    depart.time = 100;
    depart.valid = true;
    ubahn::core::setNext(state, 0, depart);
    QVERIFY(state.trains[0].next == depart);
    QVERIFY(state.heap.empty());
}

QTEST_APPLESS_MAIN(EngineContractTest)
#include "tst_engine_contract.moc"
