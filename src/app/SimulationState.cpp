#include "SimulationState.h"

namespace ubahn::app {

void SimulationState::clear() {
    trains_.clear();
    boards_.clear();
    clock_.reset();
    warnings_.clear();
    delayNotices_.clear();
}

void SimulationState::apply(const api::ViewDelta& delta, bool includeDelayNotices) {
    for (const auto& train : delta.trains) {
        trains_[train.id] = train;
    }
    for (const auto& board : delta.boards) {
        const auto key = std::make_pair(static_cast<int>(board.dir), board.station);
        boards_[key] = board;
    }
    if (delta.clock.has_value()) {
        clock_ = delta.clock;
    }
    warnings_.insert(warnings_.end(), delta.warnings.begin(), delta.warnings.end());

    delayNotices_.clear();
    if (includeDelayNotices) {
        delayNotices_ = delta.delays;
    }
}

const std::map<int, api::TrainUpdate>& SimulationState::trains() const {
    return trains_;
}

const std::map<std::pair<int, int>, api::BoardUpdate>& SimulationState::boards() const {
    return boards_;
}

const std::optional<api::ClockUpdate>& SimulationState::clock() const {
    return clock_;
}

const std::vector<api::Warning>& SimulationState::warnings() const {
    return warnings_;
}

const std::vector<api::DelayNotice>& SimulationState::delayNotices() const {
    return delayNotices_;
}

} // namespace ubahn::app
