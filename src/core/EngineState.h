#pragma once

#include "api/Types.h"
#include "core/IndexedMinHeap.h"
#include "core/Line.h"
#include "core/Rng.h"
#include "core/Schedule.h"
#include "core/Stats.h"
#include "core/Trips.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ubahn::core {

// Owners: trains 0..k-1, dispatcher A = k, dispatcher B = k+1, calendar = k+2
// (ALGORITHM 4.1). Term::A is 0 and Term::B is 1, so dispatcherOwner(k, term) matches.
constexpr int ownerCount(int fleet) {
    return fleet + 3;
}
constexpr int dispatcherOwner(int fleet, api::Term term) {
    return fleet + static_cast<int>(term);
}
constexpr int calendarOwner(int fleet) {
    return fleet + 2;
}

// ALGORITHM 5.3.
enum class TrainState : uint8_t {
    InDepot,
    LeavingDepot,
    WaitingSlot,
    Dwelling,
    Running,
    StoppedAtSignal,
    AlightingTerminal,
    ToDeadEnd,
    CabChange,
    InDeadEndWaiting,
    FromDeadEnd,
    ToDepot
};

// Train events use the names in ALGORITHM 4.2. Calendar kinds share priority 0.
// There is no PeriodEnd: the run stops when runUntil reaches the period, and the
// calendar does not own that moment (BE-13). DispatcherWake is one type; the
// owner id distinguishes terminal A from B.
enum class EventType : uint8_t {
    DayChange,
    WindowBoundary,
    ServiceOpen,
    ServiceClose,
    NightRebalance,
    Depart,
    LeaveToDeadEnd,
    LeaveToDepot,
    LeaveDeadEnd,
    LeaveDepot,
    Arrive,
    ReachSignal,
    DeadEndReached,
    CabChangeDone,
    DepotReached,
    PlatformReachedFromDeadEnd,
    PlatformReachedFromDepot,
    DispatcherWake
};

// ALGORITHM 4.2. Unknown values stay at 0 so a corrupt type is handled before movement.
constexpr uint8_t priorityOf(EventType type) {
    switch (type) {
    case EventType::DayChange:
    case EventType::WindowBoundary:
    case EventType::ServiceOpen:
    case EventType::ServiceClose:
    case EventType::NightRebalance:
        return 0;
    case EventType::Depart:
    case EventType::LeaveToDeadEnd:
    case EventType::LeaveToDepot:
    case EventType::LeaveDeadEnd:
    case EventType::LeaveDepot:
        return 1;
    case EventType::Arrive:
    case EventType::ReachSignal:
    case EventType::DeadEndReached:
    case EventType::CabChangeDone:
    case EventType::DepotReached:
    case EventType::PlatformReachedFromDeadEnd:
    case EventType::PlatformReachedFromDepot:
        return 2;
    case EventType::DispatcherWake:
        return 3;
    }
    return 0;
}

// One type for trains, both dispatchers, and the calendar. valid == false means
// the owner has no heap entry (ALGORITHM 4.1 stores the type with the owner).
struct NextEvent {
    EventType type = EventType::DayChange;
    api::SimTime time = 0;
    bool valid = false;

    bool operator==(const NextEvent&) const = default;
};

// Metres from the start of the span, for interpolation (ALGORITHM 5.3, 7.4).
struct Segment {
    api::SimTime t0 = 0;
    api::SimTime t1 = 0;
    double x0 = 0;
    double x1 = 0;
};

struct Train {
    int id = 0;
    TrainState state = TrainState::InDepot;
    api::TrainFlags flags;
    api::Dir dir = api::Dir::I;
    int pos = 0; // along dir: dwell station, or the span's start station
    api::Term depotAt = api::Term::A;
    int tripId = -1; // index into EngineState::trips, or -1

    api::SimTime aAct = 0;
    // Departure with the dwell decision applied and no holding yet (ALGORITHM 7.2).
    api::SimTime dBase = 0;
    int32_t dwellEff = 0;
    int32_t drawnDelay = 0; // ALGORITHM 9, one roll per dwell
    int32_t lateness = 0;
    api::SimTime departAct = 0; // ALGORITHM 7.3
    api::SimTime signalAt = 0;  // ALGORITHM 7.4, StoppedAtSignal
    Segment seg;

    NextEvent next;
    int prevOnTrack = -1; // ahead, toward the destination (ALGORITHM 5.5)
    int nextOnTrack = -1; // behind
    // Start of the open lateness episode, or -1 (ALGORITHM 5.3 / 12). Not kNoTime.
    api::SimTime latenessSince = -1;
};

// ALGORITHM 5.4. Time sentinels are api::kNoTime. Adding a duration to kNoTime
// overflows int64; callers treat kNoTime as "no sample" and drop that constraint.
struct Platform {
    int occupant = -1;
    api::SimTime lastArrival = api::kNoTime;
    api::SimTime lastDeparture = api::kNoTime;
    int lastTrain = -1;
    int32_t lastDwell = 0;
    int32_t lastShift = 0; // board shift, ALGORITHM 13
    // Previous passenger arrival for the service-day headway rule (ALGORITHM 12).
    api::SimTime lastPassengerArrival = api::kNoTime;
};

// freeAt is the last release. While the resource is occupied that value is stale
// for predictions; resourceFreeAt reads the occupant's event instead (ALGORITHM 7.5).
// occupiedSince is the entry time of the current (or last) occupant, for C12.
struct Resource {
    int occupant = -1;
    api::SimTime freeAt = api::kNoTime;
    api::SimTime occupiedSince = api::kNoTime;
};

struct Terminal {
    Resource deadEnd;
    Resource throat;
    std::deque<int> depot;     // free trains; front is issued first (ALGORITHM 10.2)
    std::deque<int32_t> slots; // trip ids, ascending slot, not yet departed
    SlotCursor cursor;         // next trip not yet materialised (ALGORITHM 10.1)
    NextEvent next;            // dispatcher wake; the heap does not store the type
};

// head is the train closest to the destination, tail is the rearmost (ALGORITHM 5.5).
struct TrackList {
    int head = -1;
    int tail = -1;
};

struct CalendarState {
    NextEvent next;
};

// FIFO without duplicates. Ids outside 0..owners-1, and negative ids, are ignored.
// A popped id may be pushed again. Cascade order is this order (ALGORITHM 6.2).
class DirtySet {
public:
    explicit DirtySet(int owners) : mark_(static_cast<std::size_t>(owners > 0 ? owners : 0), 0) {}

    bool empty() const { return order_.empty(); }
    int size() const { return static_cast<int>(order_.size()); }

    bool contains(int id) const {
        if (id < 0 || id >= static_cast<int>(mark_.size())) {
            return false;
        }
        return mark_[static_cast<std::size_t>(id)] != 0;
    }

    void push(int id) {
        if (id < 0 || id >= static_cast<int>(mark_.size()) || contains(id)) {
            return;
        }
        mark_[static_cast<std::size_t>(id)] = 1;
        order_.push_back(id);
    }

    // Precondition: !empty().
    int pop() {
        const int id = order_.front();
        order_.pop_front();
        mark_[static_cast<std::size_t>(id)] = 0;
        return id;
    }

    void clear() {
        for (const int id : order_) {
            mark_[static_cast<std::size_t>(id)] = 0;
        }
        order_.clear();
    }

private:
    std::deque<int> order_;
    std::vector<char> mark_;
};

// The whole model. Modules are free functions over this; they do not keep their own copies.
struct EngineState {
    api::SimParams params;
    uint64_t seed = 0;
    Line line;
    // Live schedule. applySchedule may diverge it from params.schedule; modules read this one.
    api::ScheduleConfig schedule;
    api::ScheduleAnalysis analysis;

    std::vector<Train> trains;
    std::array<std::vector<Platform>, 2> platforms; // [dir][pos]
    std::array<Terminal, 2> terms;                  // [Term]
    std::array<TrackList, 2> tracks;                // [dir]
    // Append-only. trips[id].id == id. nextTripId is the next id to issue.
    std::vector<Trip> trips;
    int32_t nextTripId = 0;

    IndexedMinHeap heap{0};
    Rng rng{0};
    Stats stats;
    CalendarState calendar;

    // Current instant only. collectDelta takes both. runUntil clears delays when emit is false.
    std::vector<api::DelayNotice> pendingDelays;
    std::vector<api::Warning> pendingWarnings;

    api::SimTime now = 0;
    std::optional<std::string> internalError;

    // SPEC 14.3. Empty: drawDelay rolls rng. Otherwise the function replaces the roll.
    std::function<int32_t(int train, api::SimTime now)> delayOverride;

    // Last packet sent to the GUI (ALGORITHM 13). Boards are [dir][pos].
    // sentTrainAt is the packet time, kNoTime if that train was never sent.
    std::vector<std::optional<api::TrainUpdate>> sentTrains;
    std::array<std::vector<std::optional<api::BoardUpdate>>, 2> sentBoards;
    std::optional<api::ClockUpdate> sentClock;
    std::vector<api::SimTime> sentTrainAt;
};

constexpr api::Term termOfArrival(api::Dir dir) {
    return dir == api::Dir::I ? api::Term::B : api::Term::A;
}
constexpr api::Term termOfDeparture(api::Dir dir) {
    return dir == api::Dir::I ? api::Term::A : api::Term::B;
}

// ahead is prevOnTrack, behind is nextOnTrack. Out of range returns -1.
inline int ahead(const EngineState& state, int train) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return -1;
    }
    return state.trains[static_cast<std::size_t>(train)].prevOnTrack;
}

inline int behind(const EngineState& state, int train) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return -1;
    }
    return state.trains[static_cast<std::size_t>(train)].nextOnTrack;
}

inline int arrivalDirIndex(api::Term term) {
    return term == api::Term::B ? static_cast<int>(api::Dir::I) : static_cast<int>(api::Dir::II);
}

inline int departureDirIndex(api::Term term) {
    return term == api::Term::A ? static_cast<int>(api::Dir::I) : static_cast<int>(api::Dir::II);
}

inline Platform& platformAt(EngineState& state, int dirIndex, int pos) {
    return state.platforms[static_cast<std::size_t>(dirIndex)][static_cast<std::size_t>(pos)];
}

inline const Platform& platformAt(const EngineState& state, int dirIndex, int pos) {
    return state.platforms[static_cast<std::size_t>(dirIndex)][static_cast<std::size_t>(pos)];
}

// B arrives on platforms[I][N-1] and departs on platforms[II][0]. A is the opposite
// (ALGORITHM 5.4). Precondition: that platform row is non-empty.
inline Platform& arrivalPlatform(EngineState& state, api::Term term) {
    const int dirIndex = arrivalDirIndex(term);
    const int pos =
        static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size()) - 1;
    return platformAt(state, dirIndex, pos);
}

inline const Platform& arrivalPlatform(const EngineState& state, api::Term term) {
    const int dirIndex = arrivalDirIndex(term);
    const int pos =
        static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size()) - 1;
    return platformAt(state, dirIndex, pos);
}

inline Platform& departurePlatform(EngineState& state, api::Term term) {
    return platformAt(state, departureDirIndex(term), 0);
}

inline const Platform& departurePlatform(const EngineState& state, api::Term term) {
    return platformAt(state, departureDirIndex(term), 0);
}

inline Trip* tripOf(EngineState& state, int train) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return nullptr;
    }
    const int id = state.trains[static_cast<std::size_t>(train)].tripId;
    if (id < 0 || id >= static_cast<int>(state.trips.size())) {
        return nullptr;
    }
    return &state.trips[static_cast<std::size_t>(id)];
}

inline const Trip* tripOf(const EngineState& state, int train) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return nullptr;
    }
    const int id = state.trains[static_cast<std::size_t>(train)].tripId;
    if (id < 0 || id >= static_cast<int>(state.trips.size())) {
        return nullptr;
    }
    return &state.trips[static_cast<std::size_t>(id)];
}

} // namespace ubahn::core
