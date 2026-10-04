#pragma once

// Pure, stateless contract functions. Implemented by the backend in src/core.
// Safe to call from any thread; never throw.

#include "api/Types.h"

#include <cstdint>
#include <vector>

namespace ubahn::api {

std::vector<Preset> builtinPresets(); // SPEC 12; first is "training-8"
SimParams defaultParams();
ScheduleConfig defaultSchedule(); // SPEC 6.3

int32_t physMinRunTime(double lengthM);               // ceil(t_phys(L)), ALGORITHM 3.1
int32_t defaultStdRunTime(double lengthM);            // 1.5 * t_phys(L), rounded up to 5 s
int32_t minRunTime(const LineConfig& line, int span); // C3
int32_t maxRunTime(const LineConfig& line, int span); // C4

std::vector<ValidationIssue> validate(const SimParams& params); // SPEC 9.1 ranges + 9.4
std::vector<ValidationIssue> validateSchedule(const LineConfig& line,
                                              const ScheduleConfig& schedule);
ScheduleAnalysis analyze(const LineConfig& line, const ScheduleConfig& schedule); // SPEC 6.4

inline int dayOfWeek(SimTime t) {
    return static_cast<int>((t / kSecondsPerDay) % 7);
}

inline int32_t timeOfDay(SimTime t) {
    return static_cast<int32_t>(t % kSecondsPerDay);
}

} // namespace ubahn::api
