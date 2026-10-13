#include "core/Schedule.h"

#include "api/Functions.h"

namespace ubahn::core {

std::optional<int> windowIndexAt(const api::ScheduleConfig&, api::SimTime) {
    // TODO(BE-04)
    return std::nullopt;
}

const api::Window* windowAt(const api::ScheduleConfig&, api::SimTime) {
    // TODO(BE-04)
    return nullptr;
}

int32_t tToTerm(const Line&, int32_t) {
    // TODO(BE-04): ALGORITHM 5.2
    return 0;
}

int32_t tOneway(const Line&, int32_t) {
    // TODO(BE-04)
    return 0;
}

int32_t tCycle(const Line&, int32_t) {
    // TODO(BE-04)
    return 0;
}

int32_t tTurn(const api::LineConfig&) {
    // TODO(BE-04)
    return 0;
}

int32_t nReq(int32_t, int32_t) {
    // TODO(BE-04)
    return 0;
}

int32_t hEff(int32_t, int32_t, int) {
    // TODO(BE-04): SPEC 6.4
    return 0;
}

api::ScheduleAnalysis analyzeLine(const Line&, const api::ScheduleConfig&) {
    // TODO(BE-04)
    return {};
}

std::vector<api::Warning> stretchWarnings(const api::ScheduleAnalysis&,
                                          const api::ScheduleConfig&) {
    // TODO(BE-04)
    return {};
}

std::vector<api::StretchInfo> stretchedWindows(const api::ScheduleAnalysis&,
                                               const api::ScheduleConfig&) {
    // TODO(BE-04)
    return {};
}

int32_t hEffAt(const api::ScheduleAnalysis&, const api::ScheduleConfig&, api::SimTime) {
    // TODO(BE-04)
    return 0;
}

} // namespace ubahn::core

namespace ubahn::api {

ScheduleAnalysis analyze(const LineConfig&, const ScheduleConfig&) {
    // TODO(BE-04): analyzeLine(buildLine(line), schedule). Top-level tCycle is the max (decision
    // R5).
    return {};
}

} // namespace ubahn::api
