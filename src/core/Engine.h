#pragma once

#include "api/Types.h"
#include "core/EngineState.h"
#include "core/Publisher.h"

#include <optional>
#include <string>
#include <vector>

namespace ubahn::core {

// Cross-module calls. Engine reaches modules only through this table, so a test
// can replace the loop. Modules call each other by the declared functions:
//   TrainEvents -> Regulator::earliestArrival
//               -> Dispatcher::assignTripFromTurnaround, decideWithdrawal
//               -> Stats::on*
//               -> Invariants::checkArrival, checkDeparture, checkDeadEndEntry
//   Dispatcher  -> Regulator::readyAt, resourceFreeAt
//   checkInvariants (this table) -> Invariants::checkState
// Calendar does not call another module. It pushes both dispatcher owners into
// DirtySet; the cascade reschedules them with dispatcherNext.
// Dirty ids below k are trains (computeNext). Dispatcher owners use dispatcherNext.
// runCascade returns false when the budget below is spent and does not assert.
// Debug assert and Release internalError belong in Engine::runUntil (ALGORITHM 6.2).
struct Handlers {
    void (*handleTrain)(EngineState&, int train, api::SimTime now, DirtySet&);
    void (*handleDispatcher)(EngineState&, api::Term term, api::SimTime now, DirtySet&);
    void (*handleCalendar)(EngineState&, api::SimTime now, DirtySet&);
    NextEvent (*computeNext)(const EngineState&, int train, api::SimTime now);
    NextEvent (*dispatcherNext)(const EngineState&, api::Term term, api::SimTime now);
    NextEvent (*calendarNext)(const EngineState&, api::SimTime now);
    void (*applyReschedule)(EngineState&, int train, const NextEvent& next, api::SimTime now);
    std::optional<api::ViewDelta> (*collectDelta)(EngineState&, api::SimTime now, DeltaMode mode);
    void (*checkInvariants)(const EngineState&);
};

// ALGORITHM 6.2. k is the fleet size.
constexpr int cascadeBudget(int fleetSize) {
    return 4 * fleetSize + 16;
}

const Handlers& defaultHandlers();

// Structures, trains in the depots (ceil(k/2) in A, floor(k/2) in B), no heap entries.
// now is warmupFrom(period.start). The Engine constructor is what inserts the first events.
EngineState initEngineState(const api::SimParams& params, uint64_t seed);

// False when the cascade budget is exceeded. Does not assert and does not set internalError.
bool runCascade(EngineState& state, DirtySet& dirty, api::SimTime now, const Handlers& handlers);

// Every heap event whose time is exactly `now`, each followed by its cascade.
// Does not publish: runUntil collects one delta afterwards when emit is true.
void processInstant(EngineState& state, api::SimTime now, const Handlers& handlers);

// Public surface for SimWorker (ALGORITHM 14). The loop itself is BE-09.
class Engine {
public:
    Engine(const api::SimParams& validated, uint64_t seed);

    api::SimTime warmupFrom() const;
    api::SimTime now() const;
    api::RunInfo runInfo() const;
    void runUntil(api::SimTime t, bool emit);
    std::vector<api::ViewDelta> takeDeltas();
    api::ViewDelta snapshotDelta();
    api::ViewDelta seekDelta();
    std::vector<api::ValidationIssue> applySchedule(const api::ScheduleConfig& schedule);
    api::SimSummary summary(api::SimTime stoppedAt, bool completed) const;
    const std::optional<std::string>& internalError() const;

private:
    EngineState state_;
    std::vector<api::ViewDelta> deltas_;
    const Handlers* handlers_ = nullptr;
};

} // namespace ubahn::core
