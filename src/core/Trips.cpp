#include "core/Trips.h"

namespace ubahn::core {

Trip buildTrip(const Line&, int32_t, api::Dir, api::SimTime, int32_t) {
    // TODO(BE-05): ALGORITHM 5.1
    return {};
}

api::SimTime firstSlot(const Line&, const api::ScheduleConfig&, int64_t) {
    // TODO(BE-05): ALGORITHM 10.1
    return 0;
}

api::SimTime lastSlot(const Line&, const api::ScheduleConfig&, int64_t) {
    // TODO(BE-05)
    return 0;
}

SlotCursor initialSlotCursor(const Line&, const api::ScheduleConfig&, api::SimTime) {
    // TODO(BE-05)
    return {};
}

std::vector<Trip> generateSlots(SlotCursor&, const Line&, const api::ScheduleConfig&,
                                const api::ScheduleAnalysis&, api::Dir, api::SimTime, int32_t&) {
    // TODO(BE-05): ALGORITHM 10.1
    return {};
}

} // namespace ubahn::core
