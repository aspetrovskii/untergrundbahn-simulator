#include "core/Regulator.h"

#include "core/Time.h"

#include <algorithm>
#include <cstdint>

namespace ubahn::core {
namespace {

// kNoTime is "no sample". laterTime skips it so a missing input cannot win the max
// and cannot be shifted (ALGORITHM 7.1, EngineState.h).
api::SimTime laterTime(api::SimTime lhs, api::SimTime rhs) {
    if (lhs == api::kNoTime) {
        return rhs;
    }
    if (rhs == api::kNoTime) {
        return lhs;
    }
    return std::max(lhs, rhs);
}

// value + delta, or kNoTime when value is not a sample. delta may be negative.
api::SimTime shiftTime(api::SimTime value, int64_t delta) {
    if (value == api::kNoTime) {
        return api::kNoTime;
    }
    return value + delta;
}

bool isPlatformDeparture(EventType type) {
    return type == EventType::Depart || type == EventType::LeaveToDeadEnd ||
           type == EventType::LeaveToDepot;
}

const Train* trainAt(const EngineState& state, int train) {
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        return nullptr;
    }
    return &state.trains[static_cast<std::size_t>(train)];
}

bool platformInRange(const EngineState& state, api::Dir dir, int pos) {
    if (pos < 0) {
        return false;
    }
    const auto& row = state.platforms[static_cast<std::size_t>(dir)];
    return pos < static_cast<int>(row.size());
}

NextEvent noEvent() {
    return {};
}

NextEvent eventAt(EventType type, api::SimTime time) {
    NextEvent next;
    next.type = type;
    next.time = time;
    next.valid = true;
    return next;
}

// Time along the current segment when the train reaches `target` metres.
// Rounded up so the stop is not scheduled before the point (ALGORITHM 2, 7.4).
api::SimTime timeToReach(const Segment& seg, double target) {
    constexpr double kEps = 1e-6;
    if (target <= seg.x0 + kEps) {
        return seg.t0;
    }
    if (seg.x1 <= seg.x0 + kEps || seg.t1 == seg.t0) {
        // TODO(BE-10): the segment does not move toward the signal point. Is an immediate
        // stop at t0 acceptable, or should the caller keep the previous Arrive?
        return seg.t0;
    }
    if (target >= seg.x1 - kEps) {
        return seg.t1;
    }
    const double fraction = (target - seg.x0) / (seg.x1 - seg.x0);
    const double elapsed = static_cast<double>(seg.t1 - seg.t0) * fraction;
    return seg.t0 + ceilEps(elapsed);
}

double signalStopX(const Span& span, double offset) {
    if (offset >= span.lengthM) {
        // TODO(BE-10): signalStopOffsetM is not shorter than the span. Stopping at the
        // span entrance keeps the train off the platform (C9).
        return 0;
    }
    return span.lengthM - offset;
}

// Start event still in `next` → start + duration. Already scheduled result → keep it,
// so a later pass cannot pull the maneuver earlier. Does not read `now`.
api::SimTime fixedManeuver(const Train& unit, EventType startedBy, EventType produced,
                           int32_t duration) {
    if (unit.next.valid && unit.next.type == startedBy) {
        return unit.next.time + duration;
    }
    if (unit.next.valid && unit.next.type == produced) {
        return unit.next.time;
    }
    // TODO(BE-10): maneuver state without the starting event or the event it produces.
    // No event, rather than a time derived from `now` (ALGORITHM 7.4).
    return api::kNoTime;
}

// Arrival at a departure platform (pos 0) cannot break C6 or C7.
api::SimTime arrivalNotBeforePlatform(const EngineState& state, api::Dir dir,
                                      api::SimTime arrival) {
    if (!platformInRange(state, dir, 0)) {
        return arrival;
    }
    const Platform& platform = platformAt(state, static_cast<int>(dir), 0);
    const api::LineConfig& cfg = state.line.cfg;
    arrival = laterTime(arrival, shiftTime(platformFreeAt(state, dir, 0), cfg.tClear));
    arrival = laterTime(arrival, shiftTime(platform.lastArrival, cfg.headwayMin));
    return arrival;
}

struct DwellBase {
    api::SimTime time = 0;
    bool catchingUp = false;
    bool ok = false;
};

// ALGORITHM 7.2 / 7.5. Recomputed from aAct, the trip and drawnDelay. Train::dBase is
// what handleTrain stores; reading a stale 0 would depart before the plan (C8).
DwellBase dwellBase(const EngineState& state, const Train& unit) {
    DwellBase base;
    const Trip* trip = tripOf(state, unit.id);
    if (trip == nullptr) {
        return base;
    }
    const api::LineConfig& cfg = state.line.cfg;
    if (unit.state == TrainState::WaitingSlot) {
        // dBase = max(slot, aAct + dwellMin) + drawnDelay. Waiting for the slot is not
        // catch-up (ALGORITHM 5.3).
        const api::SimTime ready = unit.aAct + cfg.dwellMin;
        const api::SimTime beforeDelay = std::max(trip->slot, ready);
        base.time = beforeDelay + unit.drawnDelay;
        // TODO(BE-10): ALGORITHM 7.2 writes catchingUp = dwellEff < dwellPlan, but the
        // WaitingSlot formula never defines dwellEff. Treated as catch-up only when the
        // train misses the slot and the dwell is the minimum. Is that the intended flag?
        base.catchingUp = beforeDelay > trip->slot && cfg.dwellMin < trip->dwellPlan;
        base.ok = true;
        return base;
    }

    const int pos = unit.pos;
    if (pos < 0 || pos >= static_cast<int>(trip->aPlan.size()) ||
        pos >= static_cast<int>(trip->dPlan.size())) {
        return base;
    }
    const int64_t lateArr = unit.aAct - trip->aPlan[static_cast<std::size_t>(pos)];
    const int64_t dwellEff = std::max(static_cast<int64_t>(cfg.dwellMin),
                                      static_cast<int64_t>(trip->dwellPlan) - lateArr);
    const api::SimTime withDwell = unit.aAct + dwellEff + unit.drawnDelay;
    base.time = std::max(withDwell, trip->dPlan[static_cast<std::size_t>(pos)]);
    base.catchingUp = dwellEff < trip->dwellPlan;
    base.ok = true;
    return base;
}

api::SimTime scheduledArrival(const Train& unit, const Span& span) {
    if (unit.next.valid && unit.next.type == EventType::Arrive) {
        return unit.next.time;
    }
    // A Depart (or ReachSignal) still sitting in `next` is not the platform arrival.
    // The segment end is, when the segment runs to the platform (ALGORITHM 7.3, 7.4).
    constexpr double kEps = 1e-6;
    if (unit.seg.x1 >= span.lengthM - kEps && unit.seg.t1 > unit.seg.t0) {
        return unit.seg.t1;
    }
    if (unit.next.valid && unit.next.type == EventType::ReachSignal) {
        return unit.next.time;
    }
    return unit.departAct + span.tStd;
}

NextEvent onSpan(const EngineState& state, const Train& unit) {
    const int q = unit.pos + 1;
    if (unit.pos < 0 || q >= state.line.n) {
        return noEvent();
    }
    const Span& span = spanOf(state.line, unit.dir, unit.pos);
    const api::SimTime earliest = earliestArrival(state, unit.id, q);
    const api::SimTime planned = scheduledArrival(unit, span);

    if (unit.state == TrainState::StoppedAtSignal) {
        const api::SimTime approach = unit.signalAt + state.line.cfg.signalApproach;
        const api::SimTime at = earliest == api::kNoTime ? approach : std::max(earliest, approach);
        return eventAt(EventType::Arrive, at);
    }

    // Running. Do not speed up, and do not read `now` (ALGORITHM 7.4).
    if (earliest == api::kNoTime || earliest <= planned) {
        return eventAt(EventType::Arrive, planned);
    }
    const api::SimTime limit = unit.departAct + span.tMax;
    if (earliest <= limit) {
        return eventAt(EventType::Arrive, earliest);
    }
    const double stopX = signalStopX(span, state.line.cfg.signalStopOffsetM);
    return eventAt(EventType::ReachSignal, timeToReach(unit.seg, stopX));
}

NextEvent alighting(const EngineState& state, const Train& unit) {
    const DwellBase base = dwellBase(state, unit);
    if (!base.ok) {
        return noEvent();
    }
    const api::Term term = termOfArrival(unit.dir);
    const Terminal& terminal = state.terms[static_cast<std::size_t>(term)];
    if (unit.flags.headingToDepot) {
        const api::SimTime start = laterTime(base.time, resourceFreeAt(state, terminal.throat));
        return eventAt(EventType::LeaveToDepot, start);
    }
    const api::SimTime start = laterTime(base.time, resourceFreeAt(state, terminal.deadEnd));
    return eventAt(EventType::LeaveToDeadEnd, start);
}

NextEvent inDeadEndWaiting(const EngineState& state, const Train& unit) {
    if (!unit.next.valid ||
        (unit.next.type != EventType::CabChangeDone && unit.next.type != EventType::LeaveDeadEnd)) {
        // TODO(BE-10): InDeadEndWaiting without CabChangeDone or a previous LeaveDeadEnd.
        // Leaving without cabDone could enter the departure platform early (C12).
        return noEvent();
    }
    const api::LineConfig& cfg = state.line.cfg;
    const api::Term term = termOfArrival(unit.dir);
    const api::Dir depDir = static_cast<api::Dir>(departureDirIndex(term));
    api::SimTime leave = unit.next.time;
    if (platformInRange(state, depDir, 0)) {
        const Platform& dep = departurePlatform(state, term);
        leave = laterTime(leave, shiftTime(platformFreeAt(state, depDir, 0),
                                           static_cast<int64_t>(cfg.tClear) - cfg.fromDeadEnd));
        leave = laterTime(leave, shiftTime(dep.lastArrival,
                                           static_cast<int64_t>(cfg.headwayMin) - cfg.fromDeadEnd));
    }
    return eventAt(EventType::LeaveDeadEnd, leave);
}

NextEvent inDepot(const EngineState& state, const Train& unit) {
    const Trip* trip = tripOf(state, unit.id);
    if (trip == nullptr) {
        return noEvent();
    }
    const api::LineConfig& cfg = state.line.cfg;
    const api::Term term = unit.depotAt;
    const api::Dir depDir = static_cast<api::Dir>(departureDirIndex(term));
    // max(throat, platform + tClear - depotOut, lastArrival + headwayMin - depotOut,
    //     slot - depotOut - dwellMin). ALGORITHM 7.5.
    api::SimTime leave = trip->slot - cfg.depotOut - cfg.dwellMin;
    leave =
        laterTime(leave, resourceFreeAt(state, state.terms[static_cast<std::size_t>(term)].throat));
    if (platformInRange(state, depDir, 0)) {
        const Platform& dep = departurePlatform(state, term);
        leave = laterTime(leave, shiftTime(platformFreeAt(state, depDir, 0),
                                           static_cast<int64_t>(cfg.tClear) - cfg.depotOut));
        leave = laterTime(
            leave, shiftTime(dep.lastArrival, static_cast<int64_t>(cfg.headwayMin) - cfg.depotOut));
    }
    return eventAt(EventType::LeaveDepot, leave);
}

NextEvent maneuver(const EngineState& state, const Train& unit) {
    const api::LineConfig& cfg = state.line.cfg;
    switch (unit.state) {
    case TrainState::ToDeadEnd: {
        const api::SimTime at = fixedManeuver(unit, EventType::LeaveToDeadEnd,
                                              EventType::DeadEndReached, cfg.toDeadEnd);
        return at == api::kNoTime ? noEvent() : eventAt(EventType::DeadEndReached, at);
    }
    case TrainState::CabChange: {
        const api::SimTime at =
            fixedManeuver(unit, EventType::DeadEndReached, EventType::CabChangeDone, cfg.cabChange);
        return at == api::kNoTime ? noEvent() : eventAt(EventType::CabChangeDone, at);
    }
    case TrainState::FromDeadEnd: {
        // dir is already the outbound direction (ALGORITHM 8, LeaveDeadEnd).
        const api::SimTime base = fixedManeuver(
            unit, EventType::LeaveDeadEnd, EventType::PlatformReachedFromDeadEnd, cfg.fromDeadEnd);
        if (base == api::kNoTime) {
            return noEvent();
        }
        return eventAt(EventType::PlatformReachedFromDeadEnd,
                       arrivalNotBeforePlatform(state, unit.dir, base));
    }
    case TrainState::ToDepot: {
        const api::SimTime at =
            fixedManeuver(unit, EventType::LeaveToDepot, EventType::DepotReached, cfg.depotIn);
        return at == api::kNoTime ? noEvent() : eventAt(EventType::DepotReached, at);
    }
    case TrainState::LeavingDepot: {
        const api::SimTime base = fixedManeuver(unit, EventType::LeaveDepot,
                                                EventType::PlatformReachedFromDepot, cfg.depotOut);
        if (base == api::kNoTime) {
            return noEvent();
        }
        const api::Dir dir = static_cast<api::Dir>(departureDirIndex(unit.depotAt));
        return eventAt(EventType::PlatformReachedFromDepot,
                       arrivalNotBeforePlatform(state, dir, base));
    }
    default:
        return noEvent();
    }
}

} // namespace

api::SimTime platformFreeAt(const EngineState& state, api::Dir dir, int pos) {
    if (!platformInRange(state, dir, pos)) {
        return api::kNoTime;
    }
    const Platform& platform = platformAt(state, static_cast<int>(dir), pos);
    if (platform.occupant < 0) {
        return platform.lastDeparture;
    }
    const Train* unit = trainAt(state, platform.occupant);
    if (unit != nullptr && unit->next.valid && isPlatformDeparture(unit->next.type)) {
        return unit->next.time;
    }
    // TODO(BE-10): the platform is occupied, but the next event is not a departure.
    // ALGORITHM 7.1 defines departTimeOf only then. kNoTime keeps the follower's
    // estimate monotone: a later departure can only postpone it.
    return api::kNoTime;
}

api::SimTime projectedArrival(const EngineState& state, int train) {
    const Train* unit = trainAt(state, train);
    if (unit == nullptr || !unit->next.valid) {
        // TODO(BE-10): leader on a span with no scheduled event. No sample, so the
        // follower's earliest arrival can only move later once an event exists.
        return api::kNoTime;
    }
    if (unit->next.type == EventType::Arrive) {
        return unit->next.time;
    }
    if (unit->next.type == EventType::ReachSignal) {
        return unit->next.time + state.line.cfg.signalApproach;
    }
    // TODO(BE-10): leader on a span whose next event is neither Arrive nor ReachSignal.
    return api::kNoTime;
}

api::SimTime projectedDepart(const EngineState& state, int train, int q) {
    const Trip* trip = tripOf(state, train);
    if (trip == nullptr || q < 0 || q >= static_cast<int>(trip->dPlan.size()) ||
        q >= static_cast<int>(trip->aPlan.size())) {
        return api::kNoTime;
    }
    const api::SimTime plannedDepart = trip->dPlan[static_cast<std::size_t>(q)];
    if (q == 0) {
        // TODO(BE-10): aPlan[0] is unused (ALGORITHM 5.1). Is a projection onto the
        // origin platform required? The planned departure is the slot.
        return plannedDepart;
    }
    const api::SimTime arrival = projectedArrival(state, train);
    if (arrival == api::kNoTime) {
        return plannedDepart;
    }
    const int64_t late = arrival - trip->aPlan[static_cast<std::size_t>(q)];
    const int64_t dwell = std::max(static_cast<int64_t>(state.line.cfg.dwellMin),
                                   static_cast<int64_t>(trip->dwellPlan) - late);
    return std::max(plannedDepart, arrival + dwell);
}

api::SimTime earliestArrival(const EngineState& state, int train, int q) {
    const Train* unit = trainAt(state, train);
    if (unit == nullptr || !platformInRange(state, unit->dir, q)) {
        return api::kNoTime;
    }
    const Platform& platform = platformAt(state, static_cast<int>(unit->dir), q);
    api::SimTime lastArr = platform.lastArrival;
    api::SimTime freeAt = platformFreeAt(state, unit->dir, q);

    const int leader = ahead(state, train);
    if (leader >= 0) {
        const Train& lead = state.trains[static_cast<std::size_t>(leader)];
        const bool approaching =
            (lead.state == TrainState::Running || lead.state == TrainState::StoppedAtSignal) &&
            lead.pos + 1 == q;
        if (approaching) {
            // TODO(BE-10): ALGORITHM 7.1 replaces lastArr and freeAt with the projection.
            // A missing projection would drop a real platform sample and break C6/C7, so
            // a sample is applied only when it exists, and lastArr keeps the later time.
            lastArr = laterTime(lastArr, projectedArrival(state, leader));
            freeAt = laterTime(freeAt, projectedDepart(state, leader, q));
        }
    }

    api::SimTime earliest = api::kNoTime;
    const Trip* trip = tripOf(state, train);
    if (trip != nullptr && q > 0 && q < static_cast<int>(trip->aPlan.size())) {
        earliest = laterTime(earliest, trip->aPlan[static_cast<std::size_t>(q)]);
    } else if (q == 0) {
        // TODO(BE-10): aPlan[0] is unused. C8 is not applied to an arrival at the origin.
    }
    const api::LineConfig& cfg = state.line.cfg;
    earliest = laterTime(earliest, shiftTime(lastArr, cfg.headwayMin));
    earliest = laterTime(earliest, shiftTime(freeAt, cfg.tClear));
    return earliest;
}

api::SimTime resourceFreeAt(const EngineState& state, const Resource& resource) {
    if (resource.occupant < 0) {
        return resource.freeAt;
    }
    const Train* unit = trainAt(state, resource.occupant);
    if (unit == nullptr || !unit->next.valid) {
        // TODO(BE-10): occupied resource without a scheduled event. kNoTime is the low
        // estimate; recognising the occupant later can only move this later.
        return api::kNoTime;
    }
    const api::LineConfig& cfg = state.line.cfg;
    switch (unit->state) {
    case TrainState::ToDeadEnd:
        return unit->next.time + cfg.cabChange + cfg.fromDeadEnd;
    case TrainState::CabChange:
    case TrainState::InDeadEndWaiting:
        return unit->next.time + cfg.fromDeadEnd;
    case TrainState::FromDeadEnd:
    case TrainState::ToDepot:
    case TrainState::LeavingDepot:
        return unit->next.time;
    default:
        // TODO(BE-10): resource occupant is in a state ALGORITHM 7.5 does not list.
        return api::kNoTime;
    }
}

DwellDecision dwellDecision(const EngineState& state, int train) {
    const Train* unit = trainAt(state, train);
    if (unit == nullptr) {
        return {};
    }
    const DwellBase base = dwellBase(state, *unit);
    if (!base.ok) {
        return {};
    }
    DwellDecision decision;
    decision.depart = base.time;
    decision.catchingUp = base.catchingUp;
    if (unit->state == TrainState::AlightingTerminal || unit->pos >= state.line.n - 1 ||
        unit->pos < 0) {
        return decision;
    }
    const Span& span = spanOf(state.line, unit->dir, unit->pos);
    const api::SimTime earliest = earliestArrival(state, train, unit->pos + 1);
    if (earliest == api::kNoTime) {
        return decision;
    }
    const int64_t run = std::max(static_cast<int64_t>(span.tMin), earliest - base.time);
    if (run > span.tMax) {
        decision.depart = earliest - span.tMax;
        decision.held = true;
    }
    return decision;
}

api::SimTime readyAt(const EngineState& state, int train) {
    const Train* unit = trainAt(state, train);
    if (unit == nullptr) {
        return api::kNoTime;
    }
    for (const Terminal& term : state.terms) {
        if (term.deadEnd.occupant == train) {
            return resourceFreeAt(state, term.deadEnd);
        }
    }
    if (unit->state == TrainState::WaitingSlot) {
        return unit->aAct;
    }
    // TODO(BE-10): readyAt is specified for a dead-end occupant (ALGORITHM 10.2) and,
    // here, for a train already on the departure platform. Other states return kNoTime
    // so the estimate can only move later once the train enters the dead end.
    return api::kNoTime;
}

NextEvent computeNext(const EngineState& state, int train, api::SimTime /*now*/) {
    // `now` is unused on purpose. A result that moved with it would change on every
    // cascade pass, and the cascade would not settle (ALGORITHM 7.4).
    const Train* unit = trainAt(state, train);
    if (unit == nullptr) {
        return noEvent();
    }
    switch (unit->state) {
    case TrainState::InDepot:
        return inDepot(state, *unit);
    case TrainState::WaitingSlot:
    case TrainState::Dwelling: {
        if (!dwellBase(state, *unit).ok) {
            return noEvent();
        }
        return eventAt(EventType::Depart, dwellDecision(state, train).depart);
    }
    case TrainState::Running:
    case TrainState::StoppedAtSignal:
        return onSpan(state, *unit);
    case TrainState::AlightingTerminal:
        return alighting(state, *unit);
    case TrainState::InDeadEndWaiting:
        return inDeadEndWaiting(state, *unit);
    case TrainState::ToDeadEnd:
    case TrainState::CabChange:
    case TrainState::FromDeadEnd:
    case TrainState::ToDepot:
    case TrainState::LeavingDepot:
        return maneuver(state, *unit);
    }
    return noEvent();
}

} // namespace ubahn::core
