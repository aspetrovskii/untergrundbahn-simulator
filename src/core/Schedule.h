#pragma once

#include "api/Types.h"
#include "core/Line.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ubahn::core {

// Index of the window covering t, or nullopt outside service hours.
// Service hours are time-of-day in [kOpenTod, 86400). A window covers [fromTod, toTod).
std::optional<int> windowIndexAt(const api::ScheduleConfig& schedule, api::SimTime t);

// Pointer into schedule, or nullptr when windowIndexAt is empty. Invalidated if schedule changes.
const api::Window* windowAt(const api::ScheduleConfig& schedule, api::SimTime t);

// Slot to arrival at the opposite terminal, without the final alighting dwell. ALGORITHM 5.2.
int32_t tToTerm(const Line& line, int32_t dwell);
// tToTerm plus the alighting dwell.
int32_t tOneway(const Line& line, int32_t dwell);
// Full round trip for this dwell. ScheduleAnalysis::tCycle is the maximum over windows (decision
// R5).
int32_t tCycle(const Line& line, int32_t dwell);
int32_t tTurn(const api::LineConfig& line);

int32_t nReq(int32_t tCycle, int32_t headway);
// Planned headway, or the fleet-stretched interval rounded up to 5 s. SPEC 6.4.
int32_t hEff(int32_t tCycle, int32_t headway, int fleet);

api::ScheduleAnalysis analyzeLine(const Line& line, const api::ScheduleConfig& schedule);
std::vector<api::Warning> stretchWarnings(const api::ScheduleAnalysis& analysis,
                                          const api::ScheduleConfig& schedule);
std::vector<api::StretchInfo> stretchedWindows(const api::ScheduleAnalysis& analysis,
                                               const api::ScheduleConfig& schedule);
// Effective headway of the window at t, or 0 when the line is closed.
int32_t hEffAt(const api::ScheduleAnalysis& analysis, const api::ScheduleConfig& schedule,
               api::SimTime t);

} // namespace ubahn::core
