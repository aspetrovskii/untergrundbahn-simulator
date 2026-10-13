#include "EngineFixture.h"
#include "core/Schedule.h"

#include <QtTest>
#include <cstdint>
#include <optional>
#include <vector>

namespace ubahn::core {
// F2 has no Engine(params, seed, handlers). The seam lives in Engine.cpp.
void installHandlersForTest(const Handlers* handlers);
} // namespace ubahn::core

namespace {

std::vector<int> visited;
std::vector<int> handled;
std::vector<int> delaySizes;
std::vector<ubahn::api::SimTime> published;
std::vector<ubahn::core::DeltaMode> publishedModes;
int collectCalls = 0;
int delaysSeenByCollect = -1;
int rescheduleCount = 0;
int scheduledTrains = 0;
ubahn::api::SimTime scheduledTime[8] = {};
int recordFleet = 0;

ubahn::api::SimParams paramsAt(ubahn::api::SimTime start, int fleet) {
    ubahn::api::SimPeriod period;
    period.start = start;
    period.end = start + 6 * ubahn::api::kSecondsPerDay / 24;
    return ubahn::core::makeParams(ubahn::core::trainingLineConfig(),
                                   ubahn::core::constantSchedule(120, 40, 0), fleet, period);
}

bool indexedHeapWorks() {
    ubahn::core::IndexedMinHeap heap(4);
    heap.set(1, ubahn::core::EventKey{10, 1, 1});
    return heap.contains(1) && !heap.empty() && heap.top().time == 10 && heap.top().owner == 1;
}

void expectIds(const std::vector<int>& got, const std::vector<int>& expected) {
    QCOMPARE(got.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        QCOMPARE(got[i], expected[i]);
    }
}

void countReschedule(ubahn::core::EngineState&, int, const ubahn::core::NextEvent&,
                     ubahn::api::SimTime) {
    ++rescheduleCount;
}

ubahn::core::NextEvent echoNext(const ubahn::core::EngineState& state, int train,
                                ubahn::api::SimTime) {
    visited.push_back(train);
    return state.trains[static_cast<std::size_t>(train)].next;
}

ubahn::core::NextEvent advanceOnce(const ubahn::core::EngineState& state, int train,
                                   ubahn::api::SimTime) {
    visited.push_back(train);
    if (state.trains[static_cast<std::size_t>(train)].next.valid) {
        return state.trains[static_cast<std::size_t>(train)].next;
    }
    ubahn::core::NextEvent next;
    next.type = ubahn::core::EventType::Depart;
    next.time = 1000 + train;
    next.valid = true;
    return next;
}

ubahn::core::NextEvent loopingNext(const ubahn::core::EngineState&, int train,
                                   ubahn::api::SimTime) {
    visited.push_back(train);
    ubahn::core::NextEvent next;
    next.type = ubahn::core::EventType::Depart;
    next.time = static_cast<ubahn::api::SimTime>(visited.size());
    next.valid = true;
    return next;
}

ubahn::core::NextEvent scheduledNext(const ubahn::core::EngineState&, int train,
                                     ubahn::api::SimTime) {
    if (train < 0 || train >= scheduledTrains) {
        return {};
    }
    ubahn::core::NextEvent next;
    next.type = ubahn::core::EventType::Depart;
    next.time = scheduledTime[train];
    next.valid = true;
    return next;
}

void recordTrain(ubahn::core::EngineState&, int train, ubahn::api::SimTime,
                 ubahn::core::DirtySet&) {
    handled.push_back(train);
}

void recordDispatcher(ubahn::core::EngineState&, ubahn::api::Term term, ubahn::api::SimTime,
                      ubahn::core::DirtySet&) {
    handled.push_back(ubahn::core::dispatcherOwner(recordFleet, term));
}

void recordCalendar(ubahn::core::EngineState&, ubahn::api::SimTime, ubahn::core::DirtySet&) {
    handled.push_back(ubahn::core::calendarOwner(recordFleet));
}

void noteDelay(ubahn::core::EngineState& state, int train, ubahn::api::SimTime,
               ubahn::core::DirtySet&) {
    delaySizes.push_back(static_cast<int>(state.pendingDelays.size()));
    ubahn::api::DelayNotice notice;
    notice.trainId = train;
    notice.seconds = 10;
    state.pendingDelays.push_back(notice);
    handled.push_back(train);
}

std::optional<ubahn::api::ViewDelta>
publishDelta(ubahn::core::EngineState&, ubahn::api::SimTime now, ubahn::core::DeltaMode mode) {
    published.push_back(now);
    publishedModes.push_back(mode);
    ubahn::api::ViewDelta delta;
    delta.t = now;
    return delta;
}

std::optional<ubahn::api::ViewDelta> watchDelays(ubahn::core::EngineState& state,
                                                 ubahn::api::SimTime, ubahn::core::DeltaMode) {
    ++collectCalls;
    delaysSeenByCollect = static_cast<int>(state.pendingDelays.size());
    return std::nullopt;
}

} // namespace

class EngineLoopTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void cascadeStopsWhenUnchanged();
    void cascadeIsFifoWithoutDuplicates();
    void cascadeBudgetReturnsFalse();
    void sameInstantFollowsPriorityThenOwner();
    void oneDeltaPerInstantWhenEmitting();
    void suppressClearsPendingDelays();
    void runUntilSetsNow();
    void runUntilLeavesLaterEvents();
    void warmupFromMorningAndAfterMidnight();
    void runInfoKeepsSeed();
};

void EngineLoopTest::init() {
    ubahn::core::installHandlersForTest(nullptr);
    visited.clear();
    handled.clear();
    delaySizes.clear();
    published.clear();
    publishedModes.clear();
    collectCalls = 0;
    delaysSeenByCollect = -1;
    rescheduleCount = 0;
    scheduledTrains = 0;
    recordFleet = 0;
    for (ubahn::api::SimTime& time : scheduledTime) {
        time = 0;
    }
}

void EngineLoopTest::cleanup() {
    ubahn::core::installHandlersForTest(nullptr);
}

void EngineLoopTest::cascadeStopsWhenUnchanged() {
    ubahn::core::EngineState state = ubahn::core::makeState(paramsAt(ubahn::api::kOpenTod, 2), 1);
    ubahn::core::NextEvent kept;
    kept.type = ubahn::core::EventType::Arrive;
    kept.time = 50;
    kept.valid = true;
    state.trains[0].next = kept;
    state.trains[0].nextOnTrack = 1;

    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &echoNext;
    handlers.applyReschedule = &countReschedule;
    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(2));
    dirty.push(0);

    QVERIFY(ubahn::core::runCascade(state, dirty, state.now, handlers));
    expectIds(visited, {0});
    QCOMPARE(rescheduleCount, 0);
    QVERIFY(state.trains[0].next == kept);
    QVERIFY(!state.trains[1].next.valid);
    QVERIFY(dirty.empty());
}

void EngineLoopTest::cascadeIsFifoWithoutDuplicates() {
    ubahn::core::EngineState state = ubahn::core::makeState(paramsAt(ubahn::api::kOpenTod, 4), 1);
    state.trains[0].nextOnTrack = 1;
    state.trains[1].nextOnTrack = 2;

    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &advanceOnce;
    handlers.applyReschedule = &countReschedule;
    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(4));
    dirty.push(0);
    dirty.push(0);

    QVERIFY(ubahn::core::runCascade(state, dirty, state.now, handlers));
    expectIds(visited, {0, 1, 2});
    QCOMPARE(rescheduleCount, 3);
    QVERIFY(state.trains[0].next.valid);
    QVERIFY(state.trains[1].next.valid);
    QVERIFY(state.trains[2].next.valid);
    QCOMPARE(state.trains[0].next.time, static_cast<ubahn::api::SimTime>(1000));
    QCOMPARE(state.trains[2].next.time, static_cast<ubahn::api::SimTime>(1002));

    visited.clear();
    rescheduleCount = 0;
    state.trains[0].next = {};
    state.trains[1].next = {};
    state.trains[0].nextOnTrack = 1;
    state.trains[1].nextOnTrack = -1;
    state.terms[static_cast<int>(ubahn::api::Term::B)].deadEnd.occupant = 0;
    ubahn::core::arrivalPlatform(state, ubahn::api::Term::B).occupant = 1;
    state.trains[1].flags.headingToDepot = false;

    ubahn::core::DirtySet again(ubahn::core::ownerCount(4));
    again.push(0);
    QVERIFY(ubahn::core::runCascade(state, again, state.now, handlers));
    expectIds(visited, {0, 1});
    QCOMPARE(rescheduleCount, 2);
}

void EngineLoopTest::cascadeBudgetReturnsFalse() {
    ubahn::core::EngineState state = ubahn::core::makeState(paramsAt(ubahn::api::kOpenTod, 1), 1);
    state.trains[0].nextOnTrack = 0;
    QVERIFY(!state.internalError.has_value());

    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &loopingNext;
    handlers.applyReschedule = &countReschedule;
    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(1));
    dirty.push(0);

    const int budget = ubahn::core::cascadeBudget(1);
    QCOMPARE(budget, 4 * 1 + 16);
    QVERIFY(!ubahn::core::runCascade(state, dirty, state.now, handlers));
    QCOMPARE(static_cast<int>(visited.size()), budget);
    QCOMPARE(rescheduleCount, budget);
    QVERIFY(!state.internalError.has_value());
    QVERIFY(!dirty.empty());
}

void EngineLoopTest::sameInstantFollowsPriorityThenOwner() {
    if (!indexedHeapWorks()) {
        QSKIP("IndexedMinHeap is still the BE-02 stub on this branch");
    }

    constexpr int fleet = 4;
    recordFleet = fleet;
    ubahn::core::EngineState state =
        ubahn::core::makeState(paramsAt(ubahn::api::kOpenTod, fleet), 1);
    const ubahn::api::SimTime instant = 5000;
    const auto add = [&](int owner, ubahn::core::EventType type) {
        state.heap.set(owner, ubahn::core::EventKey{instant, ubahn::core::priorityOf(type), owner});
    };
    add(2, ubahn::core::EventType::Depart);
    add(0, ubahn::core::EventType::Arrive);
    add(1, ubahn::core::EventType::LeaveDepot);
    add(ubahn::core::dispatcherOwner(fleet, ubahn::api::Term::A),
        ubahn::core::EventType::DispatcherWake);
    add(ubahn::core::calendarOwner(fleet), ubahn::core::EventType::ServiceOpen);
    state.heap.set(3, ubahn::core::EventKey{instant + 10,
                                            ubahn::core::priorityOf(ubahn::core::EventType::Depart),
                                            3});

    ubahn::api::DelayNotice notice;
    notice.trainId = 3;
    notice.seconds = 4;
    state.pendingDelays.push_back(notice);

    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.handleTrain = &recordTrain;
    handlers.handleDispatcher = &recordDispatcher;
    handlers.handleCalendar = &recordCalendar;

    ubahn::core::processInstant(state, instant, handlers);
    expectIds(handled, {ubahn::core::calendarOwner(fleet), 1, 2, 0,
                        ubahn::core::dispatcherOwner(fleet, ubahn::api::Term::A)});
    QVERIFY(state.heap.contains(3));
    QCOMPARE(state.heap.keyOf(3).time, instant + 10);
    QCOMPARE(static_cast<int>(state.pendingDelays.size()), 1);
    QCOMPARE(state.now, instant);
}

void EngineLoopTest::oneDeltaPerInstantWhenEmitting() {
    if (!indexedHeapWorks()) {
        QSKIP("IndexedMinHeap is still the BE-02 stub on this branch");
    }

    scheduledTrains = 3;
    scheduledTime[0] = 100;
    scheduledTime[1] = 100;
    scheduledTime[2] = 200;
    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &scheduledNext;
    handlers.handleTrain = &recordTrain;
    handlers.collectDelta = &publishDelta;
    ubahn::core::installHandlersForTest(&handlers);

    ubahn::core::Engine engine(paramsAt(ubahn::api::kOpenTod, 4), 5);
    engine.runUntil(200, true);

    expectIds(handled, {0, 1, 2});
    QCOMPARE(published.size(), std::size_t{2});
    QCOMPARE(published[0], static_cast<ubahn::api::SimTime>(100));
    QCOMPARE(published[1], static_cast<ubahn::api::SimTime>(200));
    QCOMPARE(publishedModes.size(), std::size_t{2});
    QCOMPARE(publishedModes[0], ubahn::core::DeltaMode::Play);
    QCOMPARE(publishedModes[1], ubahn::core::DeltaMode::Play);

    const std::vector<ubahn::api::ViewDelta> deltas = engine.takeDeltas();
    QCOMPARE(deltas.size(), std::size_t{2});
    QCOMPARE(deltas[0].t, static_cast<ubahn::api::SimTime>(100));
    QCOMPARE(deltas[1].t, static_cast<ubahn::api::SimTime>(200));
    QVERIFY(engine.takeDeltas().empty());
    QCOMPARE(engine.now(), static_cast<ubahn::api::SimTime>(200));
}

void EngineLoopTest::suppressClearsPendingDelays() {
    if (!indexedHeapWorks()) {
        QSKIP("IndexedMinHeap is still the BE-02 stub on this branch");
    }

    scheduledTrains = 2;
    scheduledTime[0] = 100;
    scheduledTime[1] = 200;
    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &scheduledNext;
    handlers.handleTrain = &noteDelay;
    handlers.collectDelta = &watchDelays;
    ubahn::core::installHandlersForTest(&handlers);

    ubahn::core::Engine engine(paramsAt(ubahn::api::kOpenTod, 4), 5);
    engine.runUntil(200, false);

    expectIds(handled, {0, 1});
    expectIds(delaySizes, {0, 0});
    QCOMPARE(collectCalls, 0);
    QVERIFY(engine.takeDeltas().empty());

    const ubahn::api::ViewDelta snapshot = engine.snapshotDelta();
    QCOMPARE(collectCalls, 1);
    QCOMPARE(delaysSeenByCollect, 0);
    QCOMPARE(snapshot.t, static_cast<ubahn::api::SimTime>(0));
    QVERIFY(!engine.internalError().has_value());
}

void EngineLoopTest::runUntilSetsNow() {
    ubahn::core::Engine engine(paramsAt(7 * 3600, 2), 1);
    const ubahn::api::SimTime target = engine.now() + 500;
    engine.runUntil(target, false);
    QCOMPARE(engine.now(), target);
    QVERIFY(engine.takeDeltas().empty());
    QVERIFY(!engine.internalError().has_value());
}

void EngineLoopTest::runUntilLeavesLaterEvents() {
    if (!indexedHeapWorks()) {
        QSKIP("IndexedMinHeap is still the BE-02 stub on this branch");
    }

    scheduledTrains = 3;
    scheduledTime[0] = 100;
    scheduledTime[1] = 250;
    scheduledTime[2] = 251;
    ubahn::core::Handlers handlers = ubahn::core::fakeHandlers();
    handlers.computeNext = &scheduledNext;
    handlers.handleTrain = &recordTrain;
    ubahn::core::installHandlersForTest(&handlers);

    ubahn::core::Engine engine(paramsAt(ubahn::api::kOpenTod, 4), 5);
    engine.runUntil(250, true);
    expectIds(handled, {0, 1});
    QCOMPARE(engine.now(), static_cast<ubahn::api::SimTime>(250));
    QVERIFY(engine.takeDeltas().empty());
}

void EngineLoopTest::warmupFromMorningAndAfterMidnight() {
    const ubahn::core::Engine morning(paramsAt(7 * 3600, 2), 3);
    QCOMPARE(morning.warmupFrom(), static_cast<ubahn::api::SimTime>(3 * 3600));
    QCOMPARE(morning.now(), morning.warmupFrom());

    const ubahn::core::Engine afterMidnight(paramsAt(2 * 3600, 2), 3);
    const ubahn::api::SimTime previous =
        static_cast<ubahn::api::SimTime>(3 * 3600 - ubahn::api::kSecondsPerDay);
    QCOMPARE(afterMidnight.warmupFrom(), previous);
    QCOMPARE(afterMidnight.now(), previous);
}

void EngineLoopTest::runInfoKeepsSeed() {
    const ubahn::api::SimParams params = paramsAt(7 * 3600, 4);
    const ubahn::core::EngineState state = ubahn::core::makeState(params, 99);
    const ubahn::core::Engine engine(params, 99);
    const ubahn::api::RunInfo info = engine.runInfo();

    QCOMPARE(info.seed, static_cast<uint64_t>(99));
    QCOMPARE(info.period.start, params.period.start);
    QCOMPARE(info.period.end, params.period.end);
    QCOMPARE(info.warmupFrom, engine.warmupFrom());
    QCOMPARE(info.analysis.tCycle, state.analysis.tCycle);
    QCOMPARE(info.analysis.minFleetForAllWindows, state.analysis.minFleetForAllWindows);

    const std::vector<ubahn::api::Warning> expected =
        ubahn::core::stretchWarnings(state.analysis, state.schedule);
    QCOMPARE(info.warnings.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        QCOMPARE(info.warnings[i].code, expected[i].code);
        QCOMPARE(info.warnings[i].dayOfWeek, expected[i].dayOfWeek);
        QCOMPARE(info.warnings[i].windowIndex, expected[i].windowIndex);
        QCOMPARE(info.warnings[i].plannedHeadway, expected[i].plannedHeadway);
        QCOMPARE(info.warnings[i].effectiveHeadway, expected[i].effectiveHeadway);
        QCOMPARE(QString::fromStdString(info.warnings[i].message),
                 QString::fromStdString(expected[i].message));
    }
}

QTEST_APPLESS_MAIN(EngineLoopTest)
#include "tst_engine_loop.moc"
