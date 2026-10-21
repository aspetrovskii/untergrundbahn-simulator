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

void enqueueSlot(Terminal& term, const EngineState& state, int32_t id) {
    if (tripById(state, id) == nullptr) {
        return;
    }
    const api::SimTime slot = (*tripById(state, id)).slot;
    auto at = term.slots.begin();
    for (; at != term.slots.end(); ++at) {
        if (*at == id) {
            return;
        }
        if (tripById(state, *at) != nullptr && (*tripById(state, *at)).slot > slot) {
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
    appendTrip(state, std::move(trip));
    if (state.nextTripId <= id) {
        state.nextTripId = id + 1;
    }
    enqueueSlot(term, state, id);
}

void ensureSlots(EngineState& state, api::Term term, api::SimTime now) {
    // Amortise collection across slot batches instead of dispatcher wakes.
    if (state.nextTripId - state.lastTripSweep >= 128) {
        reclaimTrips(state);
    }
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::Dir dir = departureDir(term);
    const api::SimTime horizon = horizonAt(state, now);
    std::vector<Trip> made = generateSlots(terminal.cursor, state.line, state.schedule,
                                           state.analysis, dir, horizon, state.nextTripId);
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

// A holder already on the departure platform keeps its slot even when it arrived late: the
// platform is occupied, so a depot train cannot depart before it, and moving the holder to a
// later slot would break the slot order on the platform (ALGORITHM 6.3, 10.2).
// TODO(BE-17): ALGORITHM 10.2 read literally (readyAt = aAct > slot - dwellMin) hands this
// slot to the depot; confirm that a train standing on the platform counts as covered.
bool holderCovers(const EngineState& state, int holder, api::Term term, api::SimTime slot) {
    if (holder < 0 || holder >= static_cast<int>(state.trains.size())) {
        return false;
    }
    const Train& train = state.trains[static_cast<std::size_t>(holder)];
    if (issuedFromDepot(train) || train.state == TrainState::WaitingSlot) {
        return true;
    }
    return turnaroundMakesIt(state, holder, term, slot);
}

void bindTrain(EngineState& state, Trip& trip, int train) {
    markTrainView(state, trip.trainId);
    markTrainView(state, train);
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
    if (unit.tripId >= 0 && unit.tripId != trip.id && tripById(state, unit.tripId) != nullptr) {
        Trip& previous = (*tripById(state, unit.tripId));
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
        if (tripById(state, id) == nullptr) {
            break;
        }
        Trip& later = (*tripById(state, id));
        const int holder = later.trainId;
        bindTrain(state, later, train);
        dirty.push(train);
        if (holder < 0) {
            return;
        }
        train = holder;
        ++index;
    }
    // The queue ended before an unassigned slot. The last holder lost its trip and must be
    // recomputed (an issued depot train with no trip has no event, principle 1).
    dirty.push(train);
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

// The first slot of this terminal that `train` can still reach after the turnaround is free,
// and comes within one headway. Withdrawing the train would leave that slot to a depot issue,
// whose throat passage then collides with the withdrawal. A later free slot does not count:
// keeping a surplus train for it only queues trains in the dead end.
bool coversNearestSlot(const EngineState& state, int train, api::Term term, api::SimTime now) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return false;
    }
    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    const api::SimTime departAt =
        std::max(now, unit.dBase) + tTurn(state.line.cfg) + state.line.cfg.dwellMin;
    const int32_t step = hEffAt(state.analysis, state.schedule, now);
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::Dir dir = departureDir(term);
    const Trip* nearest = nullptr;
    for (const int32_t id : terminal.slots) {
        if (tripById(state, id) == nullptr) {
            continue;
        }
        const Trip& trip = (*tripById(state, id));
        if (trip.dir != dir || trip.slot < departAt) {
            continue;
        }
        if (nearest == nullptr || trip.slot < nearest->slot) {
            nearest = &trip;
        }
    }
    return nearest != nullptr && nearest->trainId < 0 && step > 0 &&
           nearest->slot < departAt + step;
}

// A future slot of this terminal before today's close that `train` could still take.
bool couldTakeBeforeClose(const EngineState& state, int train, api::Term term, api::SimTime now) {
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    const api::SimTime close = dayStart(now) + api::kCloseTod;
    const api::Dir dir = departureDir(term);
    for (const int32_t id : terminal.slots) {
        if (tripById(state, id) == nullptr) {
            continue;
        }
        const Trip& trip = (*tripById(state, id));
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
        if (tripById(state, id) == nullptr) {
            continue;
        }
        const Trip& trip = (*tripById(state, id));
        if (holderCovers(state, trip.trainId, term, trip.slot)) {
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
        if (tripById(state, id) == nullptr) {
            continue;
        }
        const api::SimTime slot = (*tripById(state, id)).slot;
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
        if (tripById(state, id) == nullptr) {
            continue;
        }
        Trip& trip = (*tripById(state, id));
        if (trip.slot > limit) {
            break;
        }
        const int holder = trip.trainId;
        if (holderCovers(state, holder, term, trip.slot)) {
            continue;
        }
        if (terminal.depot.empty()) {
            if (!trip.shortageReported) {
                trip.shortageReported = true;
                state.pendingWarnings.push_back(noTrainWarning(term, trip.slot));
            }
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
        if (tripById(state, id) == nullptr) {
            continue;
        }
        const Trip& trip = (*tripById(state, id));
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
    bindTrain(state, (*tripById(state, best)), train);
}

bool decideWithdrawal(const EngineState& state, int train, api::Term term, api::SimTime now) {
    // After close there is no window, and no further slot of this service day (ALGORITHM 10.3).
    if (windowAt(state.schedule, now) == nullptr) {
        return true;
    }
    if (countInService(state) > trainsNeeded(state, now) &&
        !coversNearestSlot(state, train, term, now)) {
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
            if (tripById(state, id) == nullptr) {
                continue;
            }
            const Trip& trip = (*tripById(state, id));
            // Assigned trips keep their plan. Reclaim cancelled plans after both queues change.
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
    reclaimTrips(state);
    return {};
}

} // namespace ubahn::core
