#pragma once

#include "core/EngineState.h"

#include <cstdint>

namespace ubahn::core {

// Pure: every function reads state and does not change it (ALGORITHM 7).
// The result for a train on a span does not depend on `now` (ALGORITHM 7.4).

NextEvent computeNext(const EngineState& state, int train, api::SimTime now);

// kNoTime on the platform means "no sample". Do not add a duration to it (it overflows).
api::SimTime earliestArrival(const EngineState& state, int train, int q);
api::SimTime platformFreeAt(const EngineState& state, api::Dir dir, int pos);
api::SimTime projectedArrival(const EngineState& state, int train);
api::SimTime projectedDepart(const EngineState& state, int train, int q);
api::SimTime resourceFreeAt(const EngineState& state, const Resource& resource);

// ALGORITHM 7.2. catchingUp means the dwell was shortened below the planned dwell,
// which is not the same flag as a shortened running time.
struct DwellDecision {
    api::SimTime depart = 0;
    bool held = false;
    bool catchingUp = false;
};

DwellDecision dwellDecision(const EngineState& state, int train);

// ALGORITHM 10.2. When the train will stand on the departure platform.
// Dispatcher compares this with slot - dwellMin. Dead-end occupant: derived from
// resourceFreeAt of that dead end.
api::SimTime readyAt(const EngineState& state, int train);

} // namespace ubahn::core
