#include "core/Invariants.h"

namespace ubahn::core {

std::vector<std::string> checkState(const EngineState&) {
    // TODO(BE-16): ALGORITHM 15. C9, C10, principle 1.
    return {};
}

std::vector<std::string> checkArrival(const EngineState&, int, api::SimTime) {
    // TODO(BE-16): C6, C7, C8.
    return {};
}

std::vector<std::string> checkDeparture(const EngineState&, int, api::SimTime, int32_t) {
    // TODO(BE-16): C3, C4, C5, C8.
    return {};
}

std::vector<std::string> checkDeadEndEntry(const EngineState&, int, api::SimTime) {
    // TODO(BE-16): C12.
    return {};
}

} // namespace ubahn::core
