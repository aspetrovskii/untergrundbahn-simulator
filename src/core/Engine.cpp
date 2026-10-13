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

namespace ubahn::core {
namespace {

void checkInvariants(const EngineState& state) {
    // Event-local checks are called by TrainEvents. This handler is the whole-state pass.
    if constexpr (kCheckInvariants) {
        UBAHN_ASSERT(checkState(state).empty(), "checkState");
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

bool runCascade(EngineState&, DirtySet&, api::SimTime, const Handlers&) {
    // TODO(BE-09): ALGORITHM 6.2. Return false when cascadeBudget is exhausted.
    return true;
}

void processInstant(EngineState&, api::SimTime, const Handlers&) {
    // TODO(BE-09): ALGORITHM 6.1. Drain events at this t, cascade after each, do not publish.
}

Engine::Engine(const api::SimParams& params, uint64_t seed)
    : state_(initEngineState(params, seed)), handlers_(&defaultHandlers()) {
    // TODO(BE-09): first heap entries — calendar, both dispatchers, trains via computeNext.
}

api::SimTime Engine::warmupFrom() const {
    return ::ubahn::core::warmupFrom(state_.params.period.start);
}

api::SimTime Engine::now() const {
    return state_.now;
}

api::RunInfo Engine::runInfo() const {
    // TODO(BE-09): seed, period, warmupFrom, analysis, stretch warnings (ALGORITHM 14.1).
    return {};
}

void Engine::runUntil(api::SimTime, bool) {
    // TODO(BE-09): ALGORITHM 6.1. On a blown cascade budget, assert in Debug and set
    // internalError in Release. emit == false clears pendingDelays instead of publishing.
}

std::vector<api::ViewDelta> Engine::takeDeltas() {
    // TODO(BE-09)
    return {};
}

api::ViewDelta Engine::snapshotDelta() {
    // TODO(BE-09): collectDelta(Snapshot).
    return {};
}

api::ViewDelta Engine::seekDelta() {
    // TODO(BE-09): collectDelta(Seek).
    return {};
}

std::vector<api::ValidationIssue> Engine::applySchedule(const api::ScheduleConfig&) {
    // TODO(BE-09): Dispatcher::applyScheduleChange.
    return {};
}

api::SimSummary Engine::summary(api::SimTime, bool) const {
    // TODO(BE-09): Stats::fold.
    return {};
}

const std::optional<std::string>& Engine::internalError() const {
    return state_.internalError;
}

} // namespace ubahn::core
