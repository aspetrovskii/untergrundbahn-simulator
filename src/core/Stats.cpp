#include "core/Stats.h"

namespace ubahn::core {

Stats::Stats(api::SimPeriod, int) {
    // TODO(BE-15): size the per-station rows and remember the period (ALGORITHM 12).
}

void Stats::onStop(api::Dir, int, api::SimTime, int32_t) {
    // TODO(BE-15)
}

void Stats::onArrival(api::Dir, int, api::SimTime) {
    // TODO(BE-15): one service day per decision R15.
}

void Stats::onLateness(int, int32_t, api::SimTime) {
    // TODO(BE-15)
}

void Stats::onDeparture(int, int32_t, api::SimTime) {
    // TODO(BE-15)
}

void Stats::onHold(int32_t, api::SimTime) {
    // TODO(BE-15)
}

void Stats::onSlowdown(int32_t, api::SimTime) {
    // TODO(BE-15)
}

void Stats::onSignalStop(int32_t, api::SimTime) {
    // TODO(BE-15)
}

void Stats::setStretchedWindows(std::vector<api::StretchInfo>) {
    // TODO(BE-15)
}

api::SimSummary Stats::fold(api::SimTime, bool) const {
    // TODO(BE-15): API.md 4.5.
    return {};
}

} // namespace ubahn::core
