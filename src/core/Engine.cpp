#include "core/Engine.h"

#include "core/Calendar.h"
#include "core/Check.h"
#include "core/Dispatcher.h"
#include "core/Invariants.h"
#include "core/Regulator.h"
#include "core/Schedule.h"
#include "core/Time.h"
#include "core/TrainEvents.h"
#include "core/Trips.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <vector>

namespace ubahn::core {
namespace {

// F2 does not let Engine take a Handlers table. Tests of runUntil install one for
// this process; nullptr restores defaultHandlers. processInstant keeps the argument.
const Handlers* testHandlers = nullptr;

const Handlers& resolveHandlers(const Handlers* stored) {
    if (testHandlers != nullptr) {
        return *testHandlers;
    }
    return *stored;
}

int fleetOf(const EngineState& state) {
    return static_cast<int>(state.trains.size());
}

bool platformsReady(const EngineState& state) {
    return state.line.n > 0 && !state.platforms[0].empty() && !state.platforms[1].empty();
}

NextEvent ownerNext(const EngineState& state, int owner) {
    const int fleet = fleetOf(state);
    if (owner >= 0 && owner < fleet) {
        return state.trains[static_cast<std::size_t>(owner)].next;
    }
    if (owner == dispatcherOwner(fleet, api::Term::A)) {
        return state.terms[0].next;
    }
    if (owner == dispatcherOwner(fleet, api::Term::B)) {
        return state.terms[1].next;
    }
    if (owner == calendarOwner(fleet)) {
        return state.calendar.next;
    }
    return {};
}

void assignOwnerNext(EngineState& state, int owner, const NextEvent& next) {
    const int fleet = fleetOf(state);
    if (owner >= 0 && owner < fleet) {
        state.trains[static_cast<std::size_t>(owner)].next = next;
        return;
    }
    if (owner == dispatcherOwner(fleet, api::Term::A)) {
        state.terms[0].next = next;
        return;
    }
    if (owner == dispatcherOwner(fleet, api::Term::B)) {
        state.terms[1].next = next;
        return;
    }
    if (owner == calendarOwner(fleet)) {
        state.calendar.next = next;
    }
}

void writeHeap(EngineState& state, int owner, const NextEvent& next) {
    if (next.valid) {
        state.heap.set(owner,
                       EventKey{next.time, priorityOf(next.type), static_cast<int32_t>(owner)});
        return;
    }
    if (state.heap.contains(owner)) {
        state.heap.erase(owner);
    }
}

NextEvent nextFor(const EngineState& state, int owner, api::SimTime now, const Handlers& handlers) {
    const int fleet = fleetOf(state);
    if (owner >= 0 && owner < fleet) {
        return handlers.computeNext(state, owner, now);
    }
    if (owner == dispatcherOwner(fleet, api::Term::A)) {
        return handlers.dispatcherNext(state, api::Term::A, now);
    }
    if (owner == dispatcherOwner(fleet, api::Term::B)) {
        return handlers.dispatcherNext(state, api::Term::B, now);
    }
    if (owner == calendarOwner(fleet)) {
        return handlers.calendarNext(state, now);
    }
    return {};
}

api::SimTime slotOf(const EngineState& state, int train) {
    const Trip* trip = tripOf(state, train);
    if (trip == nullptr) {
        return std::numeric_limits<api::SimTime>::max();
    }
    return trip->slot;
}

struct WaiterRef {
    api::SimTime slot = 0;
    int id = -1;
};

bool earlierSlot(const WaiterRef& left, const WaiterRef& right) {
    if (left.slot != right.slot) {
        return left.slot < right.slot;
    }
    return left.id < right.id;
}

void addWaiter(std::vector<WaiterRef>& waiters, const EngineState& state, int id, int self) {
    if (id < 0 || id == self || id >= fleetOf(state)) {
        return;
    }
    waiters.push_back(WaiterRef{slotOf(state, id), id});
}

void collectArrivalWaiter(std::vector<WaiterRef>& waiters, const EngineState& state, api::Term term,
                          bool headingToDepot, int self) {
    if (!platformsReady(state)) {
        return;
    }
    const int id = arrivalPlatform(state, term).occupant;
    if (id < 0 ||
        state.trains[static_cast<std::size_t>(id)].flags.headingToDepot != headingToDepot) {
        return;
    }
    addWaiter(waiters, state, id, self);
}

void collectDepotAssignees(std::vector<WaiterRef>& waiters, const EngineState& state,
                           api::Term term, int self) {
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    for (const int id : terminal.depot) {
        if (id < 0 || id >= fleetOf(state)) {
            continue;
        }
        if (state.trains[static_cast<std::size_t>(id)].tripId < 0) {
            continue;
        }
        addWaiter(waiters, state, id, self);
    }
    for (const Train& unit : state.trains) {
        if (unit.state == TrainState::LeavingDepot && unit.depotAt == term) {
            addWaiter(waiters, state, unit.id, self);
        }
    }
}

void collectDeadEndWaiting(std::vector<WaiterRef>& waiters, const EngineState& state,
                           api::Term term, int self) {
    const int id = state.terms[static_cast<std::size_t>(term)].deadEnd.occupant;
    if (id < 0 || id >= fleetOf(state)) {
        return;
    }
    if (state.trains[static_cast<std::size_t>(id)].state != TrainState::InDeadEndWaiting) {
        return;
    }
    addWaiter(waiters, state, id, self);
}

// ALGORITHM 6.2–6.3. Behind on the current track, then waiters of a resource this
// train holds or is queued for (departure-platform waiters by assigned slot), then
// the dispatcher if the train is in that terminal's pipeline. The track left behind
// by the event itself is handleTrain's seed; the cascade sees the state after it.
void pushAffected(EngineState& state, int train, DirtySet& dirty) {
    dirty.push(behind(state, train));

    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    std::vector<WaiterRef> waiters;
    for (int index = 0; index < 2; ++index) {
        const api::Term term = static_cast<api::Term>(index);
        const Terminal& terminal = state.terms[static_cast<std::size_t>(index)];
        if (terminal.deadEnd.occupant == train) {
            collectArrivalWaiter(waiters, state, term, false, train);
        }
        if (terminal.throat.occupant == train) {
            collectArrivalWaiter(waiters, state, term, true, train);
            collectDepotAssignees(waiters, state, term, train);
        }
        if (platformsReady(state) && departurePlatform(state, term).occupant == train) {
            collectDeadEndWaiting(waiters, state, term, train);
            collectDepotAssignees(waiters, state, term, train);
        }
    }
    if (unit.state == TrainState::AlightingTerminal && unit.flags.headingToDepot) {
        collectDepotAssignees(waiters, state, termOfArrival(unit.dir), train);
    }
    if (unit.state == TrainState::InDeadEndWaiting) {
        for (int index = 0; index < 2; ++index) {
            const api::Term term = static_cast<api::Term>(index);
            if (state.terms[static_cast<std::size_t>(index)].deadEnd.occupant == train) {
                collectDepotAssignees(waiters, state, term, train);
            }
        }
    }
    if (unit.state == TrainState::LeavingDepot ||
        (unit.state == TrainState::InDepot && unit.tripId >= 0)) {
        collectArrivalWaiter(waiters, state, unit.depotAt, true, train);
        collectDeadEndWaiting(waiters, state, unit.depotAt, train);
    }

    std::sort(waiters.begin(), waiters.end(), earlierSlot);
    for (const WaiterRef& waiter : waiters) {
        dirty.push(waiter.id);
    }

    const int fleet = fleetOf(state);
    if (unit.state == TrainState::WaitingSlot && unit.pos == 0) {
        dirty.push(dispatcherOwner(fleet, termOfDeparture(unit.dir)));
    } else if (unit.state == TrainState::ToDepot) {
        dirty.push(dispatcherOwner(fleet, termOfArrival(unit.dir)));
    } else if (unit.state == TrainState::LeavingDepot ||
               (unit.state == TrainState::InDepot && unit.tripId >= 0)) {
        dirty.push(dispatcherOwner(fleet, unit.depotAt));
    }
}

void dispatchEvent(EngineState& state, int owner, api::SimTime now, DirtySet& dirty,
                   const Handlers& handlers) {
    const int fleet = fleetOf(state);
    if (owner >= 0 && owner < fleet) {
        handlers.handleTrain(state, owner, now, dirty);
        return;
    }
    if (owner == dispatcherOwner(fleet, api::Term::A)) {
        handlers.handleDispatcher(state, api::Term::A, now, dirty);
        return;
    }
    if (owner == dispatcherOwner(fleet, api::Term::B)) {
        handlers.handleDispatcher(state, api::Term::B, now, dirty);
        return;
    }
    if (owner == calendarOwner(fleet)) {
        handlers.handleCalendar(state, now, dirty);
    }
}

void scheduleOwner(EngineState& state, int owner, const NextEvent& next) {
    assignOwnerNext(state, owner, next);
    writeHeap(state, owner, next);
}

// Trains first, then both dispatchers, then the calendar (ALGORITHM 4.1 owners).
void scheduleInitial(EngineState& state, const Handlers& handlers) {
    const int fleet = fleetOf(state);
    const api::SimTime now = state.now;
    for (int id = 0; id < fleet; ++id) {
        scheduleOwner(state, id, handlers.computeNext(state, id, now));
    }
    const api::Term terms[] = {api::Term::A, api::Term::B};
    for (const api::Term term : terms) {
        const int owner = dispatcherOwner(fleet, term);
        scheduleOwner(state, owner, handlers.dispatcherNext(state, term, now));
    }
    const int calendar = calendarOwner(fleet);
    scheduleOwner(state, calendar, handlers.calendarNext(state, now));
}

void checkInvariants(const EngineState& state) {
    // Event-local checks are called by TrainEvents. This handler is the whole-state pass.
    if constexpr (kCheckInvariants) {
        const std::vector<std::string> violations = checkState(state);
        if (!violations.empty()) {
            std::fprintf(stderr, "checkState now=%lld\n", static_cast<long long>(state.now));
            for (const std::string& line : violations) {
                std::fprintf(stderr, "  %s\n", line.c_str());
            }
        }
        UBAHN_ASSERT(violations.empty(), "checkState");
    } else {
        static_cast<void>(state);
    }
}

void parkInDepot(EngineState& state, api::Term term, int first, int count, api::Dir dir) {
    auto& depot = state.terms[static_cast<std::size_t>(term)].depot;
    for (int i = 0; i < count; ++i) {
        const int id = first + i;
        Train& train = state.trains[static_cast<std::size_t>(id)];
        train.state = TrainState::InDepot;
        train.depotAt = term;
        train.dir = dir;
        depot.push_back(id);
    }
}

} // namespace

const Handlers& defaultHandlers() {
    static const Handlers handlers = {
        &handleTrain,  &handleDispatcher, &handleCalendar, &computeNext,     &dispatcherNext,
        &calendarNext, &applyReschedule,  &collectDelta,   &checkInvariants,
    };
    return handlers;
}

EngineState initEngineState(const api::SimParams& params, uint64_t seed) {
    EngineState state;
    state.params = params;
    state.seed = seed;
    state.schedule = params.schedule;
    state.line = buildLine(params.line);
    if (state.line.n <= 0) {
        // buildLine (BE-03) returns n = 0 while it is still a stub. The station list is the
        // count ALGORITHM 3.2 uses. A positive n from buildLine is left alone.
        state.line.n = static_cast<int>(params.line.stationNames.size());
    }
    const int n = std::max(state.line.n, 0);
    state.line.n = n;
    state.analysis = analyzeLine(state.line, state.schedule);

    const int k = std::max(params.line.fleetSize, 0);
    state.trains.resize(static_cast<std::size_t>(k));
    for (int id = 0; id < k; ++id) {
        state.trains[static_cast<std::size_t>(id)].id = id;
    }
    // SPEC 6.4: ceil(k/2) in A, floor(k/2) in B. Front of the deque is issued first.
    const int inA = (k + 1) / 2;
    parkInDepot(state, api::Term::A, 0, inA, api::Dir::I);
    parkInDepot(state, api::Term::B, inA, k - inA, api::Dir::II);

    state.platforms[0].assign(static_cast<std::size_t>(n), {});
    state.platforms[1].assign(static_cast<std::size_t>(n), {});
    state.sentBoards[0].assign(static_cast<std::size_t>(n), std::nullopt);
    state.sentBoards[1].assign(static_cast<std::size_t>(n), std::nullopt);
    state.sentTrains.assign(static_cast<std::size_t>(k), std::nullopt);
    state.sentTrainAt.assign(static_cast<std::size_t>(k), api::kNoTime);

    state.heap = IndexedMinHeap(ownerCount(k));
    state.rng = Rng(seed);
    state.stats = Stats(params.period, n);

    const api::SimTime origin = ::ubahn::core::warmupFrom(params.period.start);
    state.now = origin;
    for (Terminal& term : state.terms) {
        term.cursor = initialSlotCursor(state.line, state.schedule, origin);
    }
    return state;
}

bool runCascade(EngineState& state, DirtySet& dirty, api::SimTime now, const Handlers& handlers) {
    const int fleet = fleetOf(state);
    int budget = cascadeBudget(fleet);
    while (!dirty.empty()) {
        const int owner = dirty.pop();
        const NextEvent updated = nextFor(state, owner, now, handlers);
        if (updated == ownerNext(state, owner)) {
            continue;
        }
        assignOwnerNext(state, owner, updated);
        writeHeap(state, owner, updated);
        if (owner >= 0 && owner < fleet) {
            handlers.applyReschedule(state, owner, updated, now);
            pushAffected(state, owner, dirty);
        }
        // Unchanged predictions do not spend the budget (ALGORITHM 6.2).
        if (--budget == 0) {
            return false;
        }
    }
    return true;
}

namespace {

// Test doubles may echo the popped event. That pop is consumption (ALGORITHM 6.2).
// The real predictor stays on the same instant only when the event was rejected
// because a later event of this second still holds the resource (ALGORITHM 7.6).
bool realPredictor(const Handlers& handlers, int owner, int fleet) {
    const Handlers& real = defaultHandlers();
    if (owner >= 0 && owner < fleet) {
        return handlers.computeNext == real.computeNext;
    }
    if (owner == dispatcherOwner(fleet, api::Term::A) ||
        owner == dispatcherOwner(fleet, api::Term::B)) {
        return handlers.dispatcherNext == real.dispatcherNext;
    }
    if (owner == calendarOwner(fleet)) {
        return handlers.calendarNext == real.calendarNext;
    }
    return false;
}

enum class Flush : uint8_t { None, Inserted, Stuck };

// A rejected event was popped and computeNext did not move it, so the heap entry is
// gone. Put it back once, after movement of this second, so the freeing event can run
// first. A second rejection is a deadlock: the caller stops the instant.
Flush flushDeferred(EngineState& state, std::vector<char>& deferred, std::vector<char>& retried) {
    bool inserted = false;
    const int owners = static_cast<int>(deferred.size());
    for (int id = 0; id < owners; ++id) {
        if (deferred[static_cast<std::size_t>(id)] == 0) {
            continue;
        }
        deferred[static_cast<std::size_t>(id)] = 0;
        if (state.heap.contains(id)) {
            continue;
        }
        if (retried[static_cast<std::size_t>(id)] != 0) {
            return Flush::Stuck;
        }
        const NextEvent next = ownerNext(state, id);
        if (!next.valid) {
            continue;
        }
        retried[static_cast<std::size_t>(id)] = 1;
        writeHeap(state, id, next);
        inserted = true;
    }
    return inserted ? Flush::Inserted : Flush::None;
}

bool drainInstant(EngineState& state, api::SimTime now, const Handlers& handlers) {
    state.now = now;
    const int fleet = fleetOf(state);
    const int owners = ownerCount(fleet);
    std::vector<char> deferred(static_cast<std::size_t>(owners), 0);
    std::vector<char> retried(static_cast<std::size_t>(owners), 0);
    // Priority 2 is the last movement priority (arrivals and resource releases).
    // Rejected leaves are priority 1 and must run again before a dispatcher wake.
    const uint8_t movement = priorityOf(EventType::PlatformReachedFromDepot);
    const int guardLimit = owners * 8 + 16;
    int steps = 0;

    const auto missingDeferred = [&]() {
        for (int id = 0; id < owners; ++id) {
            if (deferred[static_cast<std::size_t>(id)] != 0 && !state.heap.contains(id)) {
                return true;
            }
        }
        return false;
    };
    const auto noteCascade = [&]() {
        for (int id = 0; id < owners; ++id) {
            if (deferred[static_cast<std::size_t>(id)] != 0 && state.heap.contains(id)) {
                deferred[static_cast<std::size_t>(id)] = 0;
            }
        }
    };

    while (true) {
        if (++steps > guardLimit) {
            return false;
        }
        const bool pending = !state.heap.empty() && state.heap.top().time == now;
        if (!pending || (missingDeferred() && state.heap.top().priority > movement)) {
            const Flush flush = flushDeferred(state, deferred, retried);
            if (flush == Flush::Stuck) {
                return false;
            }
            if (flush == Flush::Inserted) {
                if constexpr (kCheckInvariants) {
                    handlers.checkInvariants(state);
                }
                continue;
            }
            break;
        }

        const EventKey key = state.heap.pop();
        const NextEvent before = ownerNext(state, key.owner);
        DirtySet dirty(owners);
        dispatchEvent(state, key.owner, now, dirty, handlers);
        dirty.push(key.owner);
        if (!runCascade(state, dirty, now, handlers)) {
            return false;
        }
        noteCascade();
        const NextEvent after = ownerNext(state, key.owner);
        const int owner = static_cast<int>(key.owner);
        if (owner >= 0 && owner < owners && !state.heap.contains(owner) && after.valid &&
            after == before && after.time == now && realPredictor(handlers, owner, fleet)) {
            deferred[static_cast<std::size_t>(owner)] = 1;
        } else if constexpr (kCheckInvariants) {
            if (!missingDeferred()) {
                handlers.checkInvariants(state);
            }
        }
    }
    return true;
}

api::ViewDelta collectedOrEmpty(EngineState& state, const Handlers& handlers, DeltaMode mode) {
    if (std::optional<api::ViewDelta> delta = handlers.collectDelta(state, state.now, mode)) {
        return std::move(*delta);
    }
    return {};
}

} // namespace

void processInstant(EngineState& state, api::SimTime now, const Handlers& handlers) {
    static_cast<void>(drainInstant(state, now, handlers));
}

Engine::Engine(const api::SimParams& params, uint64_t seed)
    : state_(initEngineState(params, seed)), handlers_(&defaultHandlers()) {
    scheduleInitial(state_, resolveHandlers(handlers_));
}

api::SimTime Engine::warmupFrom() const {
    return ::ubahn::core::warmupFrom(state_.params.period.start);
}

api::SimTime Engine::now() const {
    return state_.now;
}

api::RunInfo Engine::runInfo() const {
    api::RunInfo info;
    info.seed = state_.seed;
    info.period = state_.params.period;
    info.warmupFrom = warmupFrom();
    info.analysis = state_.analysis;
    info.warnings = stretchWarnings(state_.analysis, state_.schedule);
    return info;
}

void Engine::runUntil(api::SimTime t, bool emit) {
    if (state_.internalError) {
        return;
    }
    const Handlers& handlers = resolveHandlers(handlers_);
    while (!state_.heap.empty() && state_.heap.top().time <= t) {
        const api::SimTime instant = state_.heap.top().time;
        if (!drainInstant(state_, instant, handlers)) {
            // Debug aborts. Release keeps the message for error(InternalError) (ALGORITHM 6.2).
            state_.internalError = "cascade budget exceeded";
            UBAHN_ASSERT(false, "cascade budget exceeded");
            return;
        }
        if (emit) {
            if (std::optional<api::ViewDelta> delta =
                    handlers.collectDelta(state_, instant, DeltaMode::Play)) {
                deltas_.push_back(std::move(*delta));
            }
        } else {
            state_.pendingDelays.clear();
        }
    }
    state_.now = t;
}

std::vector<api::ViewDelta> Engine::takeDeltas() {
    std::vector<api::ViewDelta> taken;
    taken.swap(deltas_);
    return taken;
}

api::ViewDelta Engine::snapshotDelta() {
    return collectedOrEmpty(state_, resolveHandlers(handlers_), DeltaMode::Snapshot);
}

api::ViewDelta Engine::seekDelta() {
    return collectedOrEmpty(state_, resolveHandlers(handlers_), DeltaMode::Seek);
}

std::vector<api::ValidationIssue> Engine::applySchedule(const api::ScheduleConfig& schedule) {
    return applyScheduleChange(state_, schedule, state_.now);
}

api::SimSummary Engine::summary(api::SimTime stoppedAt, bool completed) const {
    return state_.stats.fold(stoppedAt, completed);
}

void installHandlersForTest(const Handlers* handlers) {
    testHandlers = handlers;
}

const std::optional<std::string>& Engine::internalError() const {
    return state_.internalError;
}

} // namespace ubahn::core
