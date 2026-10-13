#pragma once

#include "core/EngineState.h"

#include <optional>

namespace ubahn::core {

// SPEC 10.1, first match wins: Stopped, Service, Slowed, CatchUp, OnTime.
// lateness > 0 without catchingUp is still CatchUp (ALGORITHM 5.3).
api::TrainMode modeOf(const Train& train);

api::TrainUpdate trainView(const EngineState& state, int train, api::SimTime now);
api::BoardUpdate boardView(const EngineState& state, api::Dir dir, int pos);
api::ClockUpdate clockView(const EngineState& state, api::SimTime now);

enum class DeltaMode : uint8_t { Play, Snapshot, Seek };

// Play: objects that differ from the last packet. Snapshot: every train, board, and the clock.
// Seek: Play, plus a train whose along disagrees with extrapolation by more than 1e-4
// (ALGORITHM 13). Updates the sent* cache. Takes pendingDelays and pendingWarnings.
// nullopt when nothing changed. A delta that contains only messages is returned.
std::optional<api::ViewDelta> collectDelta(EngineState& state, api::SimTime now, DeltaMode mode);

} // namespace ubahn::core
