#pragma once

#include "api/Types.h"

#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ubahn::app {

class SimulationState {
public:
    void clear();
    void apply(const api::ViewDelta& delta, bool includeDelayNotices = true);

    const std::map<int, api::TrainUpdate>& trains() const;
    const std::map<std::pair<int, int>, api::BoardUpdate>& boards() const;
    const std::optional<api::ClockUpdate>& clock() const;
    const std::vector<api::Warning>& warnings() const;
    const std::vector<api::DelayNotice>& delayNotices() const;

private:
    std::map<int, api::TrainUpdate> trains_;
    std::map<std::pair<int, int>, api::BoardUpdate> boards_;
    std::optional<api::ClockUpdate> clock_;
    std::vector<api::Warning> warnings_;
    std::vector<api::DelayNotice> delayNotices_;
};

} // namespace ubahn::app
