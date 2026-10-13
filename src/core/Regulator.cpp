#include "core/Regulator.h"

namespace ubahn::core {

NextEvent computeNext(const EngineState&, int, api::SimTime) {
    // TODO(BE-10): ALGORITHM 7.
    return {};
}

api::SimTime earliestArrival(const EngineState&, int, int) {
    // TODO(BE-10): ALGORITHM 7.1. Treat kNoTime as "no sample".
    return 0;
}

api::SimTime platformFreeAt(const EngineState&, api::Dir, int) {
    // TODO(BE-10): ALGORITHM 7.1.
    return 0;
}

api::SimTime projectedArrival(const EngineState&, int) {
    // TODO(BE-10): ALGORITHM 7.1.
    return 0;
}

api::SimTime projectedDepart(const EngineState&, int, int) {
    // TODO(BE-10): ALGORITHM 7.1.
    return 0;
}

api::SimTime resourceFreeAt(const EngineState&, const Resource&) {
    // TODO(BE-10): ALGORITHM 7.5.
    return 0;
}

DwellDecision dwellDecision(const EngineState&, int) {
    // TODO(BE-10): ALGORITHM 7.2.
    return {};
}

api::SimTime readyAt(const EngineState&, int) {
    // TODO(BE-10): ALGORITHM 10.2.
    return 0;
}

} // namespace ubahn::core
