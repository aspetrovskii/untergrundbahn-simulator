#pragma once

#include "api/Types.h"
#include "core/Line.h"

#include <cstdint>
#include <vector>

namespace ubahn::core {

// aPlan[0] is unused. dPlan[n - 1] is the planned end of alighting. ALGORITHM 5.1.
struct Trip {
    int32_t id = 0;
    api::Dir dir = api::Dir::I;
    api::SimTime slot = 0;
    std::vector<api::SimTime> aPlan;
    std::vector<api::SimTime> dPlan;
    int32_t dwellPlan = 0;
    int trainId = -1;
    bool shortageReported = false;
};

Trip buildTrip(const Line& line, int32_t id, api::Dir dir, api::SimTime slot, int32_t dwellPlan);

// Next departure slot to materialise for one leaving terminal. ALGORITHM 10.1.
struct SlotCursor {
    api::SimTime nextSlotTime = 0;
};

// day is a dayIndex (floor), not a SimTime.
// firstSlot = midnight(day) + kOpenTod - tToTerm(dwell of the day's first window).
api::SimTime firstSlot(const Line& line, const api::ScheduleConfig& schedule, int64_t day);
// lastSlot = midnight(day) + kCloseTod - tToTerm(dwell of the day's last window).
api::SimTime lastSlot(const Line& line, const api::ScheduleConfig& schedule, int64_t day);

// Cursor positioned at firstSlot of the calendar day that contains from. ALGORITHM 10.1.
SlotCursor initialSlotCursor(const Line& line, const api::ScheduleConfig& schedule,
                             api::SimTime from);

// Appends trips with slot <= horizon, then advances cursor and nextTripId. ALGORITHM 10.1.
std::vector<Trip> generateSlots(SlotCursor& cursor, const Line& line,
                                const api::ScheduleConfig& schedule,
                                const api::ScheduleAnalysis& analysis, api::Dir dir,
                                api::SimTime horizon, int32_t& nextTripId);

} // namespace ubahn::core
