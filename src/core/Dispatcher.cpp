#include "core/Dispatcher.h"

namespace ubahn::core {

void handleDispatcher(EngineState&, api::Term, api::SimTime, DirtySet&) {
    // TODO(BE-12): ALGORITHM 10.1, 10.2.
}

NextEvent dispatcherNext(const EngineState&, api::Term, api::SimTime) {
    // TODO(BE-12): ALGORITHM 10.2. Time must be strictly later than now.
    return {};
}

void assignTripFromTurnaround(EngineState&, int, api::Term) {
    // TODO(BE-12): ALGORITHM 10.2.
}

bool decideWithdrawal(const EngineState&, int, api::Term, api::SimTime) {
    // TODO(BE-12): ALGORITHM 10.3.
    return false;
}

std::vector<api::ValidationIssue> applyScheduleChange(EngineState&, const api::ScheduleConfig&,
                                                      api::SimTime) {
    // TODO(BE-12): ALGORITHM 14.2.
    return {};
}

} // namespace ubahn::core
