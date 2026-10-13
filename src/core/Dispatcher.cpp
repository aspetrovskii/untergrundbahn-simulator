#include "core/Dispatcher.h"

#include "api/Functions.h"
#include "core/Regulator.h"
#include "core/Schedule.h"
#include "core/Time.h"
#include "core/Trips.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ubahn::core {
namespace {

api::Dir departureDir(api::Term term) {
    return term == api::Term::A ? api::Dir::I : api::Dir::II;
}

api::SimTime leadTime(const EngineState& state) {
    // ALGORITHM 10.2. depotOut + dwellMin + margin, margin = kDispatcherMargin.
    return static_cast<api::SimTime>(state.line.cfg.depotOut) + state.line.cfg.dwellMin +
           kDispatcherMargin;
}

api::SimTime horizonAt(const EngineState& state, api::SimTime now) {
    return now + static_cast<api::SimTime>(kSlotHorizonCycles) * state.analysis.tCycle;
}

std::string formatClock(api::SimTime t) {
    const int32_t tod = todOf(t);
    const int32_t hours = tod / 3600;
    const int32_t minutes = (tod % 3600) / 60;
    const int32_t seconds = tod % 60;
    std::string text = std::to_string(hours);
    text += ':';
    if (minutes < 10) {
        text += '0';
    }
    text += std::to_string(minutes);
    text += ':';
    if (seconds < 10) {
        text += '0';
    }
    text += std::to_string(seconds);
    return text;
}

api::Warning noTrainWarning(api::Term term, api::SimTime slot) {
    api::Warning warning;
    warning.code = api::WarningCode::NoTrainForTrip;
    warning.terminal = static_cast<int>(term);
    warning.tripSlot = slot;
    warning.message = "Конечная ";
    warning.message += term == api::Term::A ? "A" : "B";
    warning.message += ": нет поезда к нитке, отправление в ";
    warning.message += formatClock(slot);
    return warning;
}

// ALGORITHM 5.1. Used only when buildTrip is still the BE-05 stub (empty plan).
void fillPlan(Trip& trip, const Line& line) {
    const int n = std::max(line.n, 1);
    trip.aPlan.assign(static_cast<std::size_t>(n), trip.slot);
    trip.dPlan.assign(static_cast<std::size_t>(n), trip.slot);
    if (line.n < 2) {
        return;
    }
    trip.dPlan[0] = trip.slot;
    for (int pos = 0; pos <= line.n - 2; ++pos) {
        const api::SimTime arrival =
            trip.dPlan[static_cast<std::size_t>(pos)] + spanOf(line, trip.dir, pos).tStd;
        trip.aPlan[static_cast<std::size_t>(pos + 1)] = arrival;
        trip.dPlan[static_cast<std::size_t>(pos + 1)] = arrival + trip.dwellPlan;
    }
}

bool planMissing(const Trip& trip, const Line& line) {
    const std::size_t n = static_cast<std::size_t>(std::max(line.n, 1));
    return trip.aPlan.size() != n || trip.dPlan.size() != n;
}

const std::vector<api::Window>* dayWindows(const api::ScheduleConfig& schedule, int64_t day) {
    const int dow = static_cast<int>(floorMod(day, 7));
    return &schedule.days[static_cast<std::size_t>(dow)].windows;
}

// ALGORITHM 10.1, BE-05 details. dayStart + open/close minus tToTerm of the edge window.
api::SimTime slotBound(const Line& line, const api::ScheduleConfig& schedule, int64_t day,
                       bool first) {
    const std::vector<api::Window>* windows = dayWindows(schedule, day);
    if (windows == nullptr || windows->empty()) {
        return 0;
    }
    const int32_t dwell = first ? windows->front().dwell : windows->back().dwell;
    const api::SimTime midnight = day * static_cast<api::SimTime>(api::kSecondsPerDay);
    const api::SimTime anchor = first ? midnight + api::kOpenTod : midnight + api::kCloseTod;
    return anchor - tToTerm(line, dwell);
}

// TODO(BE-12): generateSlots (BE-05) still returns an empty vector and does not move the
// cursor. This copies ALGORITHM 10.1 / the BE-05 card so the horizon can be filled. A live
// generateSlots either returns trips or advances the cursor, and then this is not called.
std::vector<Trip> materialiseLocal(SlotCursor& cursor, const Line& line,
                                   const api::ScheduleConfig& schedule,
                                   const api::ScheduleAnalysis& analysis, api::Dir dir,
                                   api::SimTime horizon, int32_t& nextTripId) {
    std::vector<Trip> made;
    for (int guard = 0; guard < 100000 && cursor.nextSlotTime <= horizon; ++guard) {
        const int64_t day = dayIndex(cursor.nextSlotTime);
        const std::vector<api::Window>* windows = dayWindows(schedule, day);
        if (windows == nullptr || windows->empty()) {
            break;
        }
        const api::SimTime first = slotBound(line, schedule, day, true);
        const api::SimTime last = slotBound(line, schedule, day, false);
        if (cursor.nextSlotTime > last) {
            const std::vector<api::Window>* nextWindows = dayWindows(schedule, day + 1);
            if (nextWindows == nullptr || nextWindows->empty()) {
                break;
            }
            cursor.nextSlotTime = slotBound(line, schedule, day + 1, true);
            continue;
        }
        api::SimTime slot = cursor.nextSlotTime;
        if (slot < first) {
            slot = first;
        }
        // BE-05: the horizon is not exceeded. Leave the cursor on the snapped slot.
        if (slot > horizon) {
            cursor.nextSlotTime = slot;
            break;
        }
        const api::SimTime open =
            day * static_cast<api::SimTime>(api::kSecondsPerDay) + api::kOpenTod;
        const api::SimTime windowTime = std::max(slot, open);
        const api::Window* window = windowAt(schedule, windowTime);
        if (window == nullptr) {
            break;
        }
        const int32_t step = hEffAt(analysis, schedule, windowTime);
        if (step <= 0) {
            // A zero step would spin. The schedule is invalid; stop materialising.
            break;
        }
        Trip trip = buildTrip(line, nextTripId, dir, slot, window->dwell);
        trip.id = nextTripId;
        trip.dir = dir;
        trip.slot = slot;
        trip.dwellPlan = window->dwell;
        trip.trainId = -1;
        if (planMissing(trip, line)) {
            fillPlan(trip, line);
        }
        ++nextTripId;
        cursor.nextSlotTime = slot + step;
        made.push_back(std::move(trip));
    }
    return made;
}

void enqueueSlot(Terminal& term, const EngineState& state, int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
        return;
    }
    const api::SimTime slot = state.trips[static_cast<std::size_t>(id)].slot;
    auto at = term.slots.begin();
    for (; at != term.slots.end(); ++at) {
        if (*at == id) {
            return;
        }
        if (*at >= 0 && static_cast<std::size_t>(*at) < state.trips.size() &&
            state.trips[static_cast<std::size_t>(*at)].slot > slot) {
            break;
        }
    }
    term.slots.insert(at, id);
}

void storeTrip(EngineState& state, Terminal& term, Trip trip) {
    const int32_t id = trip.id;
    if (id < 0) {
        return;
    }
    if (id == static_cast<int32_t>(state.trips.size())) {
        state.trips.push_back(std::move(trip));
    }
    if (state.nextTripId <= id) {
        state.nextTripId = id + 1;
    }
    enqueueSlot(term, state, id);
}

void ensureSlots(EngineState& state, api::Term term, api::SimTime now) {
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::Dir dir = departureDir(term);
    const api::SimTime horizon = horizonAt(state, now);
    const api::SimTime cursorBefore = terminal.cursor.nextSlotTime;
    const int32_t idBefore = state.nextTripId;
    std::vector<Trip> made = generateSlots(terminal.cursor, state.line, state.schedule,
                                           state.analysis, dir, horizon, state.nextTripId);
    // Empty and untouched means the BE-05 stub, not "nothing left to generate".
    if (made.empty() && terminal.cursor.nextSlotTime == cursorBefore &&
        state.nextTripId == idBefore && cursorBefore <= horizon) {
        made = materialiseLocal(terminal.cursor, state.line, state.schedule, state.analysis, dir,
                                horizon, state.nextTripId);
    }
    for (Trip& trip : made) {
        storeTrip(state, terminal, std::move(trip));
    }
}

bool issuedFromDepot(const Train& train) {
    return train.state == TrainState::InDepot || train.state == TrainState::LeavingDepot;
}

// ALGORITHM 10.2. readyAt is the regulator's estimate. For a dead-end occupant it is
// defined from resourceFreeAt; while either function is still the BE-10 stub (returns 0),
// the later of the two is the conservative "not ready yet" reading.
api::SimTime readiness(const EngineState& state, int train, api::Term term) {
    api::SimTime ready = readyAt(state, train);
    if (term != api::Term::A && term != api::Term::B) {
        return ready;
    }
    const Resource& deadEnd = state.terms[static_cast<std::size_t>(term)].deadEnd;
    if (deadEnd.occupant == train) {
        ready = std::max(ready, resourceFreeAt(state, deadEnd));
    }
    return ready;
}

bool turnaroundMakesIt(const EngineState& state, int train, api::Term term, api::SimTime slot) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return false;
    }
    return readiness(state, train, term) <= slot - state.line.cfg.dwellMin;
}

void bindTrain(EngineState& state, Trip& trip, int train) {
    if (trip.trainId >= 0 && trip.trainId != train &&
        trip.trainId < static_cast<int>(state.trains.size())) {
        Train& previous = state.trains[static_cast<std::size_t>(trip.trainId)];
        if (previous.tripId == trip.id) {
            previous.tripId = -1;
        }
    }
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        trip.trainId = -1;
        return;
    }
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    if (unit.tripId >= 0 && unit.tripId != trip.id &&
        unit.tripId < static_cast<int32_t>(state.trips.size())) {
        Trip& previous = state.trips[static_cast<std::size_t>(unit.tripId)];
        if (previous.trainId == train) {
            previous.trainId = -1;
        }
    }
    unit.tripId = trip.id;
    trip.trainId = train;
}

// The displaced turnaround train takes the next slot, and that slot's holder takes the one
// after, until the first unassigned slot absorbs the chain (ALGORITHM 10.2, SPEC 8.3).
void shiftOntoFollowing(EngineState& state, Terminal& term, std::size_t index, int train,
                        DirtySet& dirty) {
    while (train >= 0 && index < term.slots.size()) {
        const int32_t id = term.slots[index];
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            break;
        }
        Trip& later = state.trips[static_cast<std::size_t>(id)];
        const int holder = later.trainId;
        bindTrain(state, later, train);
        dirty.push(train);
        if (holder < 0) {
            return;
        }
        train = holder;
        ++index;
    }
}

bool sameStretch(const api::Warning& lhs, const api::Warning& rhs) {
    return lhs.dayOfWeek == rhs.dayOfWeek && lhs.windowIndex == rhs.windowIndex &&
           lhs.effectiveHeadway == rhs.effectiveHeadway && lhs.plannedHeadway == rhs.plannedHeadway;
}

int countInService(const EngineState& state) {
    int count = 0;
    for (const Train& train : state.trains) {
        if (train.state != TrainState::InDepot && !train.flags.headingToDepot) {
            ++count;
        }
    }
    return count;
}

// nReq of the window at `now`, using H_eff (SPEC 6.4, ALGORITHM 10.3).
int32_t trainsNeeded(const EngineState& state, api::SimTime now) {
    const api::Window* window = windowAt(state.schedule, now);
    if (window == nullptr) {
        return 0;
    }
    const int32_t cycle = tCycle(state.line, window->dwell);
    int32_t step = hEffAt(state.analysis, state.schedule, now);
    if (step <= 0) {
        step = window->headway;
    }
    return nReq(cycle, step);
}

// A future slot of this terminal before today's close that `train` could still take.
bool couldTakeBeforeClose(const EngineState& state, int train, api::Term term, api::SimTime now) {
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::SimTime close = dayStart(now) + api::kCloseTod;
    const api::Dir dir = departureDir(term);
    for (const int32_t id : terminal.slots) {
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            continue;
        }
        const Trip& trip = state.trips[static_cast<std::size_t>(id)];
        if (trip.dir != dir || trip.slot <= now) {
            continue;
        }
        if (trip.trainId >= 0 && trip.trainId != train) {
            continue;
        }
        if (trip.slot + tToTerm(state.line, trip.dwellPlan) > close) {
            continue;
        }
        if (turnaroundMakesIt(state, train, term, trip.slot)) {
            return true;
        }
    }
    return false;
}

api::SimTime earliestUncovered(const EngineState& state, api::Term term) {
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    bool found = false;
    api::SimTime earliest = terminal.cursor.nextSlotTime;
    for (const int32_t id : terminal.slots) {
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            continue;
        }
        const Trip& trip = state.trips[static_cast<std::size_t>(id)];
        bool covered = false;
        if (trip.trainId >= 0 && trip.trainId < static_cast<int>(state.trains.size())) {
            const Train& holder = state.trains[static_cast<std::size_t>(trip.trainId)];
            if (issuedFromDepot(holder) ||
                turnaroundMakesIt(state, trip.trainId, term, trip.slot)) {
                covered = true;
            }
        }
        if (covered) {
            continue;
        }
        if (!found || trip.slot < earliest) {
            earliest = trip.slot;
            found = true;
        }
    }
    if (found) {
        return earliest;
    }
    api::SimTime lastSlot = terminal.cursor.nextSlotTime;
    bool any = false;
    for (const int32_t id : terminal.slots) {
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            continue;
        }
        const api::SimTime slot = state.trips[static_cast<std::size_t>(id)].slot;
        if (!any || slot > lastSlot) {
            lastSlot = slot;
            any = true;
        }
    }
    // A cursor behind the queue is stale (the BE-05 stub leaves it at 0). The next
    // uncreated slot counts only when the cursor is actually past the queue.
    if (!any || terminal.cursor.nextSlotTime > lastSlot) {
        return terminal.cursor.nextSlotTime;
    }
    return lastSlot;
}

void wakeDispatcher(EngineState& state, api::Term term) {
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const NextEvent wake = dispatcherNext(state, term, state.now);
    terminal.next = wake;
    const int owner = dispatcherOwner(static_cast<int>(state.trains.size()), term);
    state.heap.set(owner, EventKey{wake.time, priorityOf(EventType::DispatcherWake), owner});
}

} // namespace

void handleDispatcher(EngineState& state, api::Term term, api::SimTime now, DirtySet& dirty) {
    ensureSlots(state, term, now);
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::SimTime limit = now + leadTime(state);
    for (std::size_t i = 0; i < terminal.slots.size(); ++i) {
        const int32_t id = terminal.slots[i];
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            continue;
        }
        Trip& trip = state.trips[static_cast<std::size_t>(id)];
        if (trip.slot > limit) {
            break;
        }
        const int holder = trip.trainId;
        if (holder >= 0 && holder < static_cast<int>(state.trains.size()) &&
            issuedFromDepot(state.trains[static_cast<std::size_t>(holder)])) {
            continue;
        }
        if (holder >= 0 && turnaroundMakesIt(state, holder, term, trip.slot)) {
            continue;
        }
        if (terminal.depot.empty()) {
            state.pendingWarnings.push_back(noTrainWarning(term, trip.slot));
            continue;
        }
        const int depotTrain = terminal.depot.front();
        terminal.depot.pop_front();
        bindTrain(state, trip, depotTrain);
        dirty.push(depotTrain);
        if (holder >= 0) {
            dirty.push(holder);
            shiftOntoFollowing(state, terminal, i + 1, holder, dirty);
        }
    }
}

NextEvent dispatcherNext(const EngineState& state, api::Term term, api::SimTime now) {
    const api::SimTime uncovered = earliestUncovered(state, term);
    api::SimTime wake = uncovered - leadTime(state);
    // Same instant must not queue the dispatcher again (ALGORITHM 6.1).
    if (wake <= now) {
        wake = now + 1;
    }
    NextEvent next;
    next.type = EventType::DispatcherWake;
    next.time = wake;
    next.valid = true;
    return next;
}

void assignTripFromTurnaround(EngineState& state, int train, api::Term term) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return;
    }
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    int32_t best = -1;
    api::SimTime bestSlot = 0;
    for (const int32_t id : terminal.slots) {
        if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
            continue;
        }
        const Trip& trip = state.trips[static_cast<std::size_t>(id)];
        if (trip.trainId >= 0) {
            continue;
        }
        if (best < 0 || trip.slot < bestSlot) {
            best = id;
            bestSlot = trip.slot;
        }
    }
    if (best < 0) {
        return;
    }
    bindTrain(state, state.trips[static_cast<std::size_t>(best)], train);
}

bool decideWithdrawal(const EngineState& state, int train, api::Term term, api::SimTime now) {
    // After close there is no window, and no further slot of this service day (ALGORITHM 10.3).
    if (windowAt(state.schedule, now) == nullptr) {
        return true;
    }
    if (countInService(state) > trainsNeeded(state, now)) {
        return true;
    }
    return !couldTakeBeforeClose(state, train, term, now);
}

std::vector<api::ValidationIssue> applyScheduleChange(EngineState& state,
                                                      const api::ScheduleConfig& schedule,
                                                      api::SimTime boundary) {
    const std::vector<api::Warning> previous = stretchWarnings(state.analysis, state.schedule);
    const std::vector<api::ValidationIssue> issues =
        api::validateSchedule(state.line.cfg, schedule);
    if (!issues.empty()) {
        return issues;
    }
    state.schedule = schedule;
    state.analysis = analyzeLine(state.line, state.schedule);
    for (const api::Warning& warning : stretchWarnings(state.analysis, state.schedule)) {
        bool known = false;
        for (const api::Warning& old : previous) {
            if (sameStretch(old, warning)) {
                known = true;
                break;
            }
        }
        if (!known) {
            state.pendingWarnings.push_back(warning);
        }
    }

    const api::SimTime at = std::max(state.now, boundary);
    for (const api::Term term : {api::Term::A, api::Term::B}) {
        Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
        std::deque<int32_t> kept;
        bool haveAssigned = false;
        api::SimTime lastAssigned = 0;
        for (const int32_t id : terminal.slots) {
            if (id < 0 || static_cast<std::size_t>(id) >= state.trips.size()) {
                continue;
            }
            const Trip& trip = state.trips[static_cast<std::size_t>(id)];
            // Assigned trips keep their plan. Unassigned ones after the boundary are dropped
            // from the queue only: trips is append-only (EngineState).
            if (trip.trainId < 0 && trip.slot > boundary) {
                continue;
            }
            kept.push_back(id);
            if (trip.trainId >= 0 && (!haveAssigned || trip.slot > lastAssigned)) {
                lastAssigned = trip.slot;
                haveAssigned = true;
            }
        }
        terminal.slots = std::move(kept);
        int32_t step = hEffAt(state.analysis, state.schedule, boundary);
        if (step < 0) {
            step = 0;
        }
        terminal.cursor.nextSlotTime = boundary;
        if (haveAssigned) {
            terminal.cursor.nextSlotTime = std::max(boundary, lastAssigned + step);
        }
        ensureSlots(state, term, at);
        wakeDispatcher(state, term);
    }
    return {};
}

} // namespace ubahn::core
