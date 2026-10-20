#pragma once

#include "core/EngineState.h"

namespace ubahn::core {

// Soonest service boundary strictly later than `now`: midnight, a window edge,
// 06:00, 24:00, or 03:00 (ALGORITHM 11). When several share that timestamp, the
// type is the first of ServiceClose, ServiceOpen, DayChange, WindowBoundary,
// NightRebalance. handleCalendar still performs every action due at that time:
// 24:00 and the next midnight are one SimTime, and 06:00 is also the first window.
// The period end is not a calendar event.
NextEvent calendarNext(const EngineState& state, api::SimTime now);

// Day or window change wakes both dispatchers by pushing their owner ids into dirty.
// Open clears offHours on trains outside the depot and pushes those trains.
// Close sets offHours on trains outside the depot (ALGORITHM 11).
void handleCalendar(EngineState& state, api::SimTime now, DirtySet& dirty);

// |depot A| - |depot B| <= 1. Surplus trains move instantly and are pushed into dirty
// (ALGORITHM 10.4). `now` is the 03:00 event.
void rebalanceDepots(EngineState& state, api::SimTime now);

} // namespace ubahn::core
