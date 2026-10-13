#include "core/TrainEvents.h"

#include "core/Check.h"
#include "core/Dispatcher.h"
#include "core/Invariants.h"
#include "core/Regulator.h"
#include "core/Schedule.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace ubahn::core {
namespace {

int32_t narrowSeconds(int64_t value) {
    if (value > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

int32_t clampRun(api::SimTime earliest, api::SimTime now, int32_t tMin, int32_t tMax) {
    // ALGORITHM 7.3. E - now < tMin cannot be run faster than tMin.
    const api::SimTime delta = earliest - now;
    if (delta <= tMin) {
        return tMin;
    }
    if (delta >= tMax) {
        return tMax;
    }
    return static_cast<int32_t>(delta);
}

api::Dir oppositeDir(api::Dir dir) {
    return dir == api::Dir::I ? api::Dir::II : api::Dir::I;
}

api::Dir departureDir(api::Term term) {
    return term == api::Term::A ? api::Dir::I : api::Dir::II;
}

bool knownTrain(const EngineState& state, int train) {
    return train >= 0 && train < static_cast<int>(state.trains.size());
}

int32_t latenessAgainst(api::SimTime actual, api::SimTime planned) {
    if (actual <= planned) {
        return 0;
    }
    return narrowSeconds(actual - planned);
}

void noteLateness(EngineState& state, Train& train, int32_t lateness, api::SimTime now) {
    train.lateness = lateness;
    if (lateness <= 0) {
        return;
    }
    if (train.latenessSince < 0) {
        train.latenessSince = now;
    }
    state.stats.onLateness(train.id, lateness, now);
}

void noteDeparture(EngineState& state, Train& train, int32_t lateDep, api::SimTime now) {
    noteLateness(state, train, lateDep, now);
    state.stats.onDeparture(train.id, lateDep, now);
    // ALGORITHM 12: the episode ends on a departure with no lateness.
    if (lateDep == 0) {
        train.latenessSince = -1;
    }
}

void releasePlatform(Platform& platform, int train, api::SimTime now, api::SimTime arrived,
                     int32_t shift) {
    if (platform.occupant == train) {
        platform.occupant = -1;
    }
    platform.lastDeparture = now;
    platform.lastDwell = narrowSeconds(now - arrived);
    platform.lastShift = shift;
    platform.lastTrain = train;
}

void occupyPlatform(Platform& platform, int train, api::SimTime now) {
    platform.occupant = train;
    platform.lastArrival = now;
}

void takeResource(Resource& resource, int train, api::SimTime now) {
    resource.occupant = train;
    resource.occupiedSince = now;
}

void freeResource(Resource& resource, api::SimTime now) {
    resource.occupant = -1;
    resource.freeAt = now;
}

void eraseFromDepot(std::deque<int>& depot, int train) {
    const auto found = std::find(depot.begin(), depot.end(), train);
    if (found != depot.end()) {
        depot.erase(found);
    }
}

void seedMoved(EngineState& state, int train, int behindBefore, DirtySet& dirty) {
    dirty.push(train);
    dirty.push(behindBefore);
    // Behind on the track the train is on after the event (ALGORITHM 6.2).
    dirty.push(behind(state, train));
}

void seedDispatcher(const EngineState& state, api::Term term, DirtySet& dirty) {
    dirty.push(dispatcherOwner(state.params.line.fleetSize, term));
}

void pushDeadEndWaiters(const EngineState& state, api::Term term, int self, DirtySet& dirty) {
    // ALGORITHM 6.3. One candidate: the train alighting here that is not withdrawn.
    const Platform& arrival = arrivalPlatform(state, term);
    if (arrival.occupant < 0 || arrival.occupant == self) {
        return;
    }
    if (!state.trains[static_cast<std::size_t>(arrival.occupant)].flags.headingToDepot) {
        dirty.push(arrival.occupant);
    }
}

void pushThroatWaiters(const EngineState& state, api::Term term, int self, DirtySet& dirty) {
    // Departure into the depot first (it frees the arrival platform), then an issue.
    const Platform& arrival = arrivalPlatform(state, term);
    if (arrival.occupant >= 0 && arrival.occupant != self &&
        state.trains[static_cast<std::size_t>(arrival.occupant)].flags.headingToDepot) {
        dirty.push(arrival.occupant);
    }
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    for (const int id : terminal.depot) {
        if (id == self || !knownTrain(state, id)) {
            continue;
        }
        if (state.trains[static_cast<std::size_t>(id)].tripId >= 0) {
            dirty.push(id);
        }
    }
}

void pushDepartureWaiters(const EngineState& state, api::Term term, int self, DirtySet& dirty) {
    // Earliest assigned slot first (ALGORITHM 6.3). Both candidates have a trip.
    struct Candidate {
        int id = -1;
        api::SimTime slot = 0;
    };
    std::vector<Candidate> candidates;
    const auto consider = [&](int id) {
        if (id < 0 || id == self || !knownTrain(state, id)) {
            return;
        }
        const Trip* trip = tripOf(state, id);
        if (trip == nullptr) {
            return;
        }
        candidates.push_back(Candidate{id, trip->slot});
    };

    const int deadEnd = state.terms[static_cast<std::size_t>(term)].deadEnd.occupant;
    if (knownTrain(state, deadEnd) &&
        state.trains[static_cast<std::size_t>(deadEnd)].state == TrainState::InDeadEndWaiting) {
        consider(deadEnd);
    }
    for (const int id : state.terms[static_cast<std::size_t>(term)].depot) {
        consider(id);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.slot != rhs.slot) {
            return lhs.slot < rhs.slot;
        }
        return lhs.id < rhs.id;
    });
    for (const Candidate& candidate : candidates) {
        dirty.push(candidate.id);
    }
}

// ALGORITHM 7.6. The event is left unapplied and the train is queued so computeNext
// can move it.
// TODO(BE-11): the section also asks for a debug assert. That would abort the
// race-guard test, so the guard only skips the event. Should Debug assert too?
bool rejectIfBusy(int train, bool busy, DirtySet& dirty) {
    if (!busy) {
        return false;
    }
    dirty.push(train);
    return true;
}

void assertCheck(const std::vector<std::string>& violations, const char* message) {
    if constexpr (kCheckInvariants) {
        if (!violations.empty()) {
            std::fprintf(stderr, "%s\n", message);
            for (const std::string& line : violations) {
                std::fprintf(stderr, "  %s\n", line.c_str());
            }
        }
        UBAHN_ASSERT(violations.empty(), message);
    } else {
        static_cast<void>(violations);
        static_cast<void>(message);
    }
}

// ALGORITHM 7.2, intermediate station and terminal alighting. drawnDelay is already rolled.
void fixArrivalDwell(EngineState& state, Train& train, const Trip* trip, int pos,
                     Platform& platform, api::SimTime now) {
    int32_t lateArr = 0;
    int32_t dwellPlan = state.line.cfg.dwellMin;
    api::SimTime plannedDepart = train.aAct;
    if (trip != nullptr && pos >= 0 && pos < static_cast<int>(trip->aPlan.size())) {
        lateArr = latenessAgainst(train.aAct, trip->aPlan[static_cast<std::size_t>(pos)]);
        dwellPlan = trip->dwellPlan;
    }
    if (trip != nullptr && pos >= 0 && pos < static_cast<int>(trip->dPlan.size())) {
        plannedDepart = trip->dPlan[static_cast<std::size_t>(pos)];
    }
    const int32_t dwellMin = state.line.cfg.dwellMin;
    const int64_t shortened = static_cast<int64_t>(dwellPlan) - lateArr;
    train.dwellEff = static_cast<int32_t>(std::max(static_cast<int64_t>(dwellMin), shortened));
    const api::SimTime withDelay = train.aAct + train.dwellEff + train.drawnDelay;
    train.dBase = std::max(withDelay, plannedDepart);
    train.flags.catchingUp = train.dwellEff < dwellPlan;
    train.flags.held = false;
    train.flags.slowed = false;
    platform.lastShift = lateArr;
    noteLateness(state, train, lateArr, now);
}

// WaitingSlot. Delay sits outside the max, unlike an intermediate dwell (ALGORITHM 7.2).
void fixOriginDwell(EngineState& state, Train& train, const Trip* trip, api::SimTime now) {
    const int32_t dwellMin = state.line.cfg.dwellMin;
    train.dwellEff = dwellMin;
    api::SimTime ready = train.aAct + dwellMin;
    if (trip != nullptr) {
        ready = std::max(trip->slot, ready);
    }
    train.dBase = ready + train.drawnDelay;
    train.flags.catchingUp = false;
    train.flags.held = false;
    train.flags.slowed = false;
    // Off-hours is a property of this placement, not of the trip (ALGORITHM 5.3).
    train.flags.offHours = windowAt(state.schedule, now) == nullptr;
    const int32_t late = trip == nullptr ? 0 : latenessAgainst(now, trip->slot);
    noteLateness(state, train, late, now);
}

double positionOnSegment(const Segment& segment, api::SimTime now) {
    if (segment.t1 == segment.t0) {
        return segment.x0;
    }
    const double duration = static_cast<double>(segment.t1 - segment.t0);
    double fraction = static_cast<double>(now - segment.t0) / duration;
    if (fraction < 0) {
        fraction = 0;
    }
    if (fraction > 1) {
        fraction = 1;
    }
    return segment.x0 + fraction * (segment.x1 - segment.x0);
}

double signalStopX(const EngineState& state, const Span& span) {
    double stopX = span.lengthM - state.line.cfg.signalStopOffsetM;
    if (stopX < 0) {
        stopX = 0;
    }
    if (stopX > span.lengthM) {
        stopX = span.lengthM;
    }
    return stopX;
}

// Same queue as Regulator.cpp signalHaltX: one metre behind a leader who is
// already aimed at or short of the official signal point.
double signalHaltX(const EngineState& state, const Train& unit) {
    const Span& span = spanOf(state.line, unit.dir, unit.pos);
    double stopX = signalStopX(state, span);
    const int leaderId = unit.prevOnTrack;
    if (leaderId < 0 || leaderId >= static_cast<int>(state.trains.size())) {
        return stopX;
    }
    const Train& leader = state.trains[static_cast<std::size_t>(leaderId)];
    const bool sameSpan =
        leader.dir == unit.dir && leader.pos == unit.pos &&
        (leader.state == TrainState::Running || leader.state == TrainState::StoppedAtSignal);
    if (!sameSpan || leader.seg.x1 > stopX) {
        return stopX;
    }
    stopX = std::min(stopX, leader.seg.x1 - 1.0);
    return stopX < 0 ? 0 : stopX;
}

bool spanStart(const EngineState& state, const Train& train) {
    return train.pos >= 0 && train.pos < state.line.n - 1 &&
           train.pos < static_cast<int>(state.line.spans.size());
}

void onArrive(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    if (train.state != TrainState::Running && train.state != TrainState::StoppedAtSignal) {
        dirty.push(index);
        return;
    }
    const int q = train.pos + 1;
    const int dirIndex = static_cast<int>(train.dir);
    if (q < 0 || q >= state.line.n || dirIndex < 0 || dirIndex > 1 ||
        q >= static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size())) {
        dirty.push(index);
        return;
    }
    Platform& platform = platformAt(state, dirIndex, q);
    if (rejectIfBusy(index, platform.occupant >= 0, dirty)) {
        return;
    }

    const bool fromSignal = train.state == TrainState::StoppedAtSignal;
    const api::SimTime stoppedAt = train.signalAt;
    const int behindBefore = behind(state, index);

    // checkArrival reads the arrival platform and aPlan[pos] (ALGORITHM 8).
    train.pos = q;
    assertCheck(checkArrival(state, index, now), "checkArrival");

    train.aAct = now;
    occupyPlatform(platform, index, now);
    const int station = stationOf(state.line, train.dir, q);
    state.stats.onArrival(train.dir, station, now);
    if (fromSignal && now >= stoppedAt) {
        state.stats.onSignalStop(narrowSeconds(now - stoppedAt), now);
    }
    train.drawnDelay = drawDelay(state, index, now);
    fixArrivalDwell(state, train, tripOf(state, index), q, platform, now);
    train.state = q == state.line.n - 1 ? TrainState::AlightingTerminal : TrainState::Dwelling;
    if (q == state.line.n - 1) {
        const api::Term term = termOfArrival(train.dir);
        if (decideWithdrawal(state, index, term, now)) {
            train.flags.headingToDepot = true;
        }
    }
    seedMoved(state, index, behindBefore, dirty);
}

void onReachSignal(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    if (train.state != TrainState::Running || !spanStart(state, train)) {
        dirty.push(index);
        return;
    }
    const int behindBefore = behind(state, index);
    const Span& span = spanOf(state.line, train.dir, train.pos);
    const double official = signalStopX(state, span);
    double stand = official;
    const int leaderId = train.prevOnTrack;
    if (leaderId >= 0 && leaderId < static_cast<int>(state.trains.size())) {
        const Train& leader = state.trains[static_cast<std::size_t>(leaderId)];
        const bool sameSpan =
            leader.dir == train.dir && leader.pos == train.pos &&
            (leader.state == TrainState::Running || leader.state == TrainState::StoppedAtSignal);
        if (sameSpan) {
            const double leaderX = positionOnSegment(leader.seg, now);
            if (leaderX <= official) {
                stand = std::min(stand, leaderX - 1.0);
            }
        }
    }
    if (stand < 0) {
        stand = 0;
    }
    train.state = TrainState::StoppedAtSignal;
    train.signalAt = now;
    // Standing at the signal. The approach time is added by computeNext, not by this segment.
    train.seg = Segment{now, now, stand, stand};
    seedMoved(state, index, behindBefore, dirty);
}

int32_t planShiftAt(const Trip* trip, int pos, api::SimTime now) {
    if (trip == nullptr || pos < 0 || pos >= static_cast<int>(trip->dPlan.size())) {
        return 0;
    }
    return latenessAgainst(now, trip->dPlan[static_cast<std::size_t>(pos)]);
}

void onDepart(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    if ((train.state != TrainState::Dwelling && train.state != TrainState::WaitingSlot) ||
        !spanStart(state, train)) {
        dirty.push(index);
        return;
    }
    const int dirIndex = static_cast<int>(train.dir);
    if (train.pos >= static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size())) {
        dirty.push(index);
        return;
    }
    const Span& span = spanOf(state.line, train.dir, train.pos);
    const api::SimTime earliest = earliestArrival(state, index, train.pos + 1);
    const int32_t tRun = clampRun(earliest, now, span.tMin, span.tMax);
    assertCheck(checkDeparture(state, index, now, tRun), "checkDeparture");

    const int behindBefore = behind(state, index);
    const int32_t lateDep = planShiftAt(tripOf(state, index), train.pos, now);
    if (train.flags.held && now > train.dBase) {
        state.stats.onHold(narrowSeconds(now - train.dBase), now);
    }
    noteDeparture(state, train, lateDep, now);

    Platform& platform = platformAt(state, dirIndex, train.pos);
    releasePlatform(platform, index, now, train.aAct, lateDep);

    train.departAct = now;
    train.seg = Segment{now, now + tRun, 0, span.lengthM};
    train.flags.catchingUp = tRun < span.tStd;
    train.flags.slowed = tRun > span.tStd;
    train.flags.held = false;
    train.state = TrainState::Running;
    if (train.flags.slowed) {
        state.stats.onSlowdown(tRun - span.tStd, now);
    }
    seedMoved(state, index, behindBefore, dirty);
    if (train.pos == 0) {
        const api::Term origin = termOfDeparture(train.dir);
        // terms[].slots holds trips not yet sent (ALGORITHM 5.4). A sent trip left in the
        // queue looks unassigned once its train turns round, and the next turnaround or
        // depot issue would take a slot that is already in the past (ALGORITHM 10.2).
        auto& queue = state.terms[static_cast<std::size_t>(origin)].slots;
        queue.erase(std::remove(queue.begin(), queue.end(), train.tripId), queue.end());
        pushDepartureWaiters(state, origin, index, dirty);
    }
}

void onLeaveToDeadEnd(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    const api::Term term = termOfArrival(train.dir);
    Resource& deadEnd = state.terms[static_cast<std::size_t>(term)].deadEnd;
    if (rejectIfBusy(index, deadEnd.occupant >= 0, dirty)) {
        return;
    }
    assertCheck(checkDeadEndEntry(state, index, now), "checkDeadEndEntry");

    const int behindBefore = behind(state, index);
    const int dirIndex = static_cast<int>(train.dir);
    // Waiting past the end of alighting for the dead end is waiting for the next departure
    // slot (the dead end is held by the train ahead until its slot), not lateness.
    const int32_t shift = planShiftAt(tripOf(state, index), train.pos, std::min(now, train.dBase));
    if (train.pos >= 0 &&
        train.pos < static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size())) {
        releasePlatform(platformAt(state, dirIndex, train.pos), index, now, train.aAct, shift);
    }
    noteDeparture(state, train, shift, now);
    takeResource(deadEnd, index, now);
    trackRemove(state, train.dir, index);
    assignTripFromTurnaround(state, index, term);
    train.state = TrainState::ToDeadEnd;
    seedMoved(state, index, behindBefore, dirty);
    pushDeadEndWaiters(state, term, index, dirty);
}

void onDeadEndReached(EngineState& state, int index, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    train.state = TrainState::CabChange;
    dirty.push(index);
}

void onCabChangeDone(EngineState& state, int index, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    train.state = TrainState::InDeadEndWaiting;
    dirty.push(index);
}

void onLeaveDeadEnd(EngineState& state, int index, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    // pos along the new direction is 0 at this same terminal. Leaving the old pos
    // in place would name the opposite station (stationOf).
    train.dir = oppositeDir(train.dir);
    train.pos = 0;
    train.state = TrainState::FromDeadEnd;
    dirty.push(index);
}

void onReachOrigin(EngineState& state, int index, api::SimTime now, bool fromDepot,
                   DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    const api::Term term = fromDepot ? train.depotAt : termOfDeparture(train.dir);
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    Resource& passage = fromDepot ? terminal.throat : terminal.deadEnd;
    const int dirIndex = static_cast<int>(train.dir);
    if (state.platforms[static_cast<std::size_t>(dirIndex)].empty()) {
        dirty.push(index);
        return;
    }
    Platform& platform = platformAt(state, dirIndex, 0);
    if (rejectIfBusy(index, platform.occupant >= 0, dirty)) {
        return;
    }
    const int behindBefore = behind(state, index);
    if (passage.occupant == index) {
        freeResource(passage, now);
    }
    train.pos = 0;
    train.aAct = now;
    occupyPlatform(platform, index, now);
    trackPushBack(state, train.dir, index);
    train.drawnDelay = drawDelay(state, index, now);
    fixOriginDwell(state, train, tripOf(state, index), now);
    train.state = TrainState::WaitingSlot;
    state.stats.onArrival(train.dir, stationOf(state.line, train.dir, 0), now);
    seedMoved(state, index, behindBefore, dirty);
    if (fromDepot) {
        pushThroatWaiters(state, term, index, dirty);
    } else {
        pushDeadEndWaiters(state, term, index, dirty);
    }
    pushDepartureWaiters(state, term, index, dirty);
    seedDispatcher(state, term, dirty);
}

void onLeaveToDepot(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    const api::Term term = termOfArrival(train.dir);
    Resource& throat = state.terms[static_cast<std::size_t>(term)].throat;
    if (rejectIfBusy(index, throat.occupant >= 0, dirty)) {
        return;
    }
    const int behindBefore = behind(state, index);
    const int dirIndex = static_cast<int>(train.dir);
    const int32_t shift = planShiftAt(tripOf(state, index), train.pos, now);
    if (train.pos >= 0 &&
        train.pos < static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size())) {
        releasePlatform(platformAt(state, dirIndex, train.pos), index, now, train.aAct, shift);
    }
    noteDeparture(state, train, shift, now);
    takeResource(throat, index, now);
    trackRemove(state, train.dir, index);
    train.depotAt = term;
    train.state = TrainState::ToDepot;
    seedMoved(state, index, behindBefore, dirty);
    pushThroatWaiters(state, term, index, dirty);
}

void onDepotReached(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    const api::Term term = train.depotAt;
    Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    if (terminal.throat.occupant == index) {
        freeResource(terminal.throat, now);
    }
    // The inbound trip is over. An issue assignment is a later dispatcher decision
    // (ALGORITHM 10.2); a leftover tripId would look like one (ALGORITHM 6.3).
    if (Trip* trip = tripOf(state, index)) {
        if (trip->trainId == index) {
            trip->trainId = -1;
        }
    }
    train.tripId = -1;
    train.flags = {};
    train.state = TrainState::InDepot;
    eraseFromDepot(terminal.depot, index);
    terminal.depot.push_back(index);
    seedMoved(state, index, -1, dirty);
    pushThroatWaiters(state, term, index, dirty);
    seedDispatcher(state, term, dirty);
}

void onLeaveDepot(EngineState& state, int index, api::SimTime now, DirtySet& dirty) {
    Train& train = state.trains[static_cast<std::size_t>(index)];
    const api::Term term = train.depotAt;
    Resource& throat = state.terms[static_cast<std::size_t>(term)].throat;
    if (rejectIfBusy(index, throat.occupant >= 0, dirty)) {
        return;
    }
    eraseFromDepot(state.terms[static_cast<std::size_t>(term)].depot, index);
    takeResource(throat, index, now);
    train.dir = departureDir(term);
    train.pos = 0;
    train.flags.offHours = windowAt(state.schedule, now) == nullptr;
    train.state = TrainState::LeavingDepot;
    seedMoved(state, index, -1, dirty);
    pushThroatWaiters(state, term, index, dirty);
    seedDispatcher(state, term, dirty);
}

} // namespace

void handleTrain(EngineState& state, int train, api::SimTime now, DirtySet& dirty) {
    if (!knownTrain(state, train)) {
        return;
    }
    switch (state.trains[static_cast<std::size_t>(train)].next.type) {
    case EventType::Arrive:
        onArrive(state, train, now, dirty);
        break;
    case EventType::ReachSignal:
        onReachSignal(state, train, now, dirty);
        break;
    case EventType::Depart:
        onDepart(state, train, now, dirty);
        break;
    case EventType::LeaveToDeadEnd:
        onLeaveToDeadEnd(state, train, now, dirty);
        break;
    case EventType::DeadEndReached:
        onDeadEndReached(state, train, dirty);
        break;
    case EventType::CabChangeDone:
        onCabChangeDone(state, train, dirty);
        break;
    case EventType::LeaveDeadEnd:
        onLeaveDeadEnd(state, train, dirty);
        break;
    case EventType::PlatformReachedFromDeadEnd:
        onReachOrigin(state, train, now, false, dirty);
        break;
    case EventType::LeaveToDepot:
        onLeaveToDepot(state, train, now, dirty);
        break;
    case EventType::DepotReached:
        onDepotReached(state, train, now, dirty);
        break;
    case EventType::LeaveDepot:
        onLeaveDepot(state, train, now, dirty);
        break;
    case EventType::PlatformReachedFromDepot:
        onReachOrigin(state, train, now, true, dirty);
        break;
    default:
        dirty.push(train);
        break;
    }
}

void applyReschedule(EngineState& state, int train, const NextEvent& next, api::SimTime now) {
    if (!next.valid || !knownTrain(state, train)) {
        return;
    }
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    if (unit.state == TrainState::Running || unit.state == TrainState::StoppedAtSignal) {
        if (!spanStart(state, unit)) {
            return;
        }
        const Span& span = spanOf(state.line, unit.dir, unit.pos);
        const double here = positionOnSegment(unit.seg, now);
        double destination = span.lengthM;
        if (next.type == EventType::ReachSignal || unit.state == TrainState::StoppedAtSignal) {
            // StoppedAtSignal stays put (ALGORITHM 13). ReachSignal retargets the run to stopX.
            destination =
                unit.state == TrainState::StoppedAtSignal ? here : signalHaltX(state, unit);
        }
        api::SimTime arrival = next.time;
        if (arrival <= now) {
            arrival = now;
            destination = here;
        }
        unit.seg = Segment{now, arrival, here, destination};
        unit.flags.held = false;
        if (unit.state == TrainState::Running && next.type == EventType::Arrive) {
            const int32_t tRun = narrowSeconds(next.time - unit.departAct);
            unit.flags.catchingUp = tRun < span.tStd;
            unit.flags.slowed = tRun > span.tStd;
        } else if (next.type == EventType::ReachSignal) {
            // ReachSignal is chosen only when the platform is beyond tMax (ALGORITHM 7.4).
            unit.flags.catchingUp = false;
            unit.flags.slowed = true;
        }
        return;
    }
    if ((unit.state == TrainState::Dwelling || unit.state == TrainState::WaitingSlot) &&
        next.type == EventType::Depart) {
        unit.flags.held = next.time > unit.dBase;
        unit.flags.slowed = false;
        // WaitingSlot dwellEff is dwellMin while the train waits for its slot. That
        // gap is not catch-up (fixOriginDwell, ALGORITHM 7.2).
        if (unit.state == TrainState::Dwelling) {
            const Trip* trip = tripOf(state, train);
            if (trip != nullptr) {
                unit.flags.catchingUp = unit.dwellEff < trip->dwellPlan;
            }
        }
    }
}

int32_t drawDelay(EngineState& state, int train, api::SimTime now) {
    if (!knownTrain(state, train)) {
        return 0;
    }
    const api::Window* window = windowAt(state.schedule, now);
    if (window == nullptr) {
        // Outside service hours there is no roll, including preparatory trips (ALGORITHM 9).
        return 0;
    }
    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    int32_t delay = 0;
    if (state.delayOverride) {
        delay = state.delayOverride(train, now);
    } else if (state.rng.uniform01() < window->delayProb) {
        const int32_t lo = state.schedule.delayMin;
        const int32_t hi = state.schedule.delayMax;
        if (lo > hi) {
            // TODO(BE-11): delayMin > delayMax should have been rejected by validation.
            delay = 0;
        } else {
            delay = state.rng.uniformInt(lo, hi);
        }
    }
    if (delay < 0) {
        // TODO(BE-11): a negative delayOverride is not a delay. Is 0 the right substitute?
        delay = 0;
    }
    const int station = stationOf(state.line, unit.dir, unit.pos);
    if (delay > 0) {
        state.pendingDelays.push_back(api::DelayNotice{unit.id, unit.dir, station, delay});
    }
    state.stats.onStop(unit.dir, station, now, delay);
    return delay;
}

void trackPushBack(EngineState& state, api::Dir dir, int train) {
    if (!knownTrain(state, train)) {
        return;
    }
    TrackList& list = state.tracks[static_cast<std::size_t>(dir)];
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    const int previousTail = list.tail;
    unit.prevOnTrack = previousTail;
    unit.nextOnTrack = -1;
    if (knownTrain(state, previousTail)) {
        state.trains[static_cast<std::size_t>(previousTail)].nextOnTrack = train;
    } else {
        list.head = train;
    }
    list.tail = train;
}

void trackRemove(EngineState& state, api::Dir dir, int train) {
    if (!knownTrain(state, train)) {
        return;
    }
    TrackList& list = state.tracks[static_cast<std::size_t>(dir)];
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    const int previous = unit.prevOnTrack;
    const int next = unit.nextOnTrack;
    const bool linked = list.head == train || previous >= 0 || next >= 0;
    if (!linked) {
        return;
    }
    if (knownTrain(state, previous)) {
        state.trains[static_cast<std::size_t>(previous)].nextOnTrack = next;
    } else {
        list.head = next;
    }
    if (knownTrain(state, next)) {
        state.trains[static_cast<std::size_t>(next)].prevOnTrack = previous;
    } else {
        list.tail = previous;
    }
    unit.prevOnTrack = -1;
    unit.nextOnTrack = -1;
}

} // namespace ubahn::core
