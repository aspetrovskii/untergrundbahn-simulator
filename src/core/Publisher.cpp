#include "core/Publisher.h"

namespace ubahn::core {

api::TrainMode modeOf(const Train&) {
    // TODO(BE-14): ALGORITHM 5.3, SPEC 10.1.
    return api::TrainMode::OnTime;
}

api::TrainUpdate trainView(const EngineState&, int, api::SimTime) {
    // TODO(BE-14): ALGORITHM 13. Physical station via stationOf.
    return {};
}

api::BoardUpdate boardView(const EngineState&, api::Dir, int) {
    // TODO(BE-14): ALGORITHM 13.
    return {};
}

api::ClockUpdate clockView(const EngineState&, api::SimTime) {
    // TODO(BE-14): ALGORITHM 13.
    return {};
}

std::optional<api::ViewDelta> collectDelta(EngineState&, api::SimTime, DeltaMode) {
    // TODO(BE-14): ALGORITHM 13. Update sent* and take pendingDelays / pendingWarnings.
    return std::nullopt;
}

} // namespace ubahn::core
