#pragma once

#include "core/EngineState.h"

#include <string>
#include <vector>

namespace ubahn::core {

// Each function returns violations in English and does not abort. The caller asserts
// under kCheckInvariants (ALGORITHM 15).

// C9 (one occupant on a platform, in a dead end, in a throat), C10 (track order matches
// positions), principle 1. A train has exactly one heap entry, matching train.next,
// unless it is InDepot with no trip. "InDepot without an assignment" is the only
// exception; every other train has one entry (ALGORITHM 1, principle 1).
std::vector<std::string> checkState(const EngineState& state);

// C6, C7 against the platform's previous lastArrival / lastDeparture, C8 against aPlan.
// Call before those platform fields are overwritten with this arrival. kNoTime means
// there is no previous sample.
std::vector<std::string> checkArrival(const EngineState& state, int train, api::SimTime arrival);

// C3/C4 on tRun (signal time excluded), C5 on the passenger dwell depart - aAct,
// C8 on depart >= dPlan[pos].
std::vector<std::string> checkDeparture(const EngineState& state, int train, api::SimTime depart,
                                        int32_t tRun);

// C12. `entry` is the LeaveToDeadEnd time. Call before occupant and occupiedSince change:
// occupiedSince is the previous entry and freeAt is the previous release.
std::vector<std::string> checkDeadEndEntry(const EngineState& state, int train, api::SimTime entry);

} // namespace ubahn::core
