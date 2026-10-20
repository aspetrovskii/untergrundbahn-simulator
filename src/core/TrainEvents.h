#pragma once

#include "core/EngineState.h"

#include <cstdint>

namespace ubahn::core {

// Applies train.next at `now`. Pushes the cascade seed into dirty: the train,
// behind() on the track it left and the track it entered, trains waiting on a
// resource it took or freed (ALGORITHM 6.3), and a dispatcher owner when it
// reaches a departure platform, enters the depot, or leaves it (ALGORITHM 8).
void handleTrain(EngineState& state, int train, api::SimTime now, DirtySet& dirty);

// Flags and segment when a train already on a span is retimed (ALGORITHM 7.4).
// Position at `now` stays continuous; only the speed changes.
void applyReschedule(EngineState& state, int train, const NextEvent& next, api::SimTime now);

// One roll per dwell (ALGORITHM 9). delayOverride replaces the roll when set.
// A positive result is also appended to pendingDelays with the physical station.
int32_t drawDelay(EngineState& state, int train, api::SimTime now);

// Doubly linked track list (ALGORITHM 5.5). pushBack inserts at the tail.
void trackPushBack(EngineState& state, api::Dir dir, int train);
void trackRemove(EngineState& state, api::Dir dir, int train);

} // namespace ubahn::core
