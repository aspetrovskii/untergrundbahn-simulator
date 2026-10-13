#include "core/Calendar.h"

namespace ubahn::core {

NextEvent calendarNext(const EngineState&, api::SimTime) {
    // TODO(BE-13): ALGORITHM 11. Strictly later than now. Use Time.h, not api::dayOfWeek.
    return {};
}

void handleCalendar(EngineState&, api::SimTime, DirtySet&) {
    // TODO(BE-13): ALGORITHM 11. Apply every boundary that falls on this timestamp.
}

void rebalanceDepots(EngineState&, api::SimTime) {
    // TODO(BE-13): ALGORITHM 10.4.
}

} // namespace ubahn::core
