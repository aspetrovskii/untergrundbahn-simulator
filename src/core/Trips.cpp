#include "core/Trips.h"

#include "core/Schedule.h"
#include "core/Time.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ubahn::core {
namespace {

int profileIndex(int64_t day) {
    return static_cast<int>(floorMod(day, 7));
}

api::SimTime midnightOf(int64_t day) {
    return day * static_cast<api::SimTime>(api::kSecondsPerDay);
}

const std::vector<api::Window>& windowsOf(const api::ScheduleConfig& schedule, int64_t day) {
    return schedule.days[static_cast<size_t>(profileIndex(day))].windows;
}

// ALGORITHM 10.1: a preparatory slot (before opening) takes the first window of its
// calendar day. kOpenTod is a time of day, so the clamp stays inside that day.
api::SimTime windowProbe(api::SimTime t) {
    const api::SimTime open = dayStart(t) + api::kOpenTod;
    return std::max(t, open);
}

} // namespace

Trip buildTrip(const Line& line, int32_t id, api::Dir dir, api::SimTime slot, int32_t dwellPlan) {
    // ALGORITHM 5.1. aPlan[0] stays 0 (unused). dPlan[n - 1] is the end of alighting.
    Trip trip;
    trip.id = id;
    trip.dir = dir;
    trip.slot = slot;
    trip.dwellPlan = dwellPlan;
    if (line.n <= 0) {
        return trip;
    }
    trip.aPlan.assign(static_cast<size_t>(line.n), 0);
    trip.dPlan.assign(static_cast<size_t>(line.n), 0);
    trip.dPlan[0] = slot;
    for (int pos = 0; pos < line.n - 1; ++pos) {
        const api::SimTime arrival =
            trip.dPlan[static_cast<size_t>(pos)] + spanOf(line, dir, pos).tStd;
        trip.aPlan[static_cast<size_t>(pos + 1)] = arrival;
        trip.dPlan[static_cast<size_t>(pos + 1)] = arrival + dwellPlan;
    }
    return trip;
}

api::SimTime firstSlot(const Line& line, const api::ScheduleConfig& schedule, int64_t day) {
    const api::SimTime open = midnightOf(day) + api::kOpenTod;
    const std::vector<api::Window>& windows = windowsOf(schedule, day);
    if (windows.empty()) {
        return open;
    }
    return open - tToTerm(line, windows.front().dwell);
}

api::SimTime lastSlot(const Line& line, const api::ScheduleConfig& schedule, int64_t day) {
    const api::SimTime close = midnightOf(day) + api::kCloseTod;
    const std::vector<api::Window>& windows = windowsOf(schedule, day);
    if (windows.empty()) {
        return close;
    }
    return close - tToTerm(line, windows.back().dwell);
}

SlotCursor initialSlotCursor(const Line& line, const api::ScheduleConfig& schedule,
                             api::SimTime from) {
    SlotCursor cursor;
    cursor.nextSlotTime = firstSlot(line, schedule, dayIndex(from));
    return cursor;
}

std::vector<Trip> generateSlots(SlotCursor& cursor, const Line& line,
                                const api::ScheduleConfig& schedule,
                                const api::ScheduleAnalysis& analysis, api::Dir dir,
                                api::SimTime horizon, int32_t& nextTripId) {
    // ALGORITHM 10.1. Slots are emitted only while slot <= horizon.
    std::vector<Trip> trips;
    while (cursor.nextSlotTime <= horizon) {
        const api::SimTime scheduled = cursor.nextSlotTime;
        const int64_t day = dayIndex(scheduled);
        const api::SimTime first = firstSlot(line, schedule, day);
        const api::SimTime last = lastSlot(line, schedule, day);
        if (scheduled > last) {
            const api::SimTime upcoming = firstSlot(line, schedule, day + 1);
            // TODO(BE-05): если T_toTerm > 6 ч, firstSlot попадает на предыдущие сутки и
            // курсор может не сдвинуться. Дальше нитки не строим.
            if (upcoming <= scheduled) {
                break;
            }
            cursor.nextSlotTime = upcoming;
            continue;
        }

        api::SimTime slot = scheduled < first ? first : scheduled;
        if (slot > horizon) {
            cursor.nextSlotTime = slot > scheduled ? slot : scheduled;
            break;
        }

        const api::SimTime probe = windowProbe(slot);
        const api::Window* window = windowAt(schedule, probe);
        const int32_t step = hEffAt(analysis, schedule, probe);
        if (window == nullptr || step <= 0) {
            const api::SimTime upcoming = firstSlot(line, schedule, day + 1);
            if (upcoming <= scheduled) {
                break;
            }
            cursor.nextSlotTime = upcoming;
            continue;
        }

        trips.push_back(buildTrip(line, nextTripId, dir, slot, window->dwell));
        ++nextTripId;
        const api::SimTime next = slot + static_cast<api::SimTime>(step);
        if (next <= slot) {
            break;
        }
        cursor.nextSlotTime = next;
    }
    return trips;
}

} // namespace ubahn::core
