#pragma once

#include "core/EngineState.h"

#include <cstdint>
#include <vector>

namespace ubahn::core {

// ALGORITHM 10.1 names 2 * T_cycle as the materialisation horizon.
inline constexpr int kSlotHorizonCycles = 2;
// ALGORITHM 10.2. Wake this many seconds before the depot must start moving.
inline constexpr int32_t kDispatcherMargin = 30;

void handleDispatcher(EngineState& state, api::Term term, api::SimTime now, DirtySet& dirty);

// Next wake, strictly later than `now`. If the formula firstUncoveredSlot -
// depotOut - dwellMin - margin is not in the future, clamp to now + 1 so the
// same instant cannot queue the dispatcher again (ALGORITHM 6.1).
NextEvent dispatcherNext(const EngineState& state, api::Term term, api::SimTime now);

// Earliest unassigned trip of this terminal (ALGORITHM 10.2). Called at LeaveToDeadEnd.
void assignTripFromTurnaround(EngineState& state, int train, api::Term term);

// ALGORITHM 10.3. True when the train should set headingToDepot.
bool decideWithdrawal(const EngineState& state, int train, api::Term term, api::SimTime now);

// ALGORITHM 14.2. On any issue the state is unchanged and the issues are returned.
std::vector<api::ValidationIssue>
applyScheduleChange(EngineState& state, const api::ScheduleConfig& schedule, api::SimTime boundary);

} // namespace ubahn::core
