#pragma once

// Manual state for module tests. Placement does not edit the track list:
// set prevOnTrack / nextOnTrack / tracks yourself. Helpers do not insert heap entries.

#include "api/Functions.h"
#include "core/Engine.h"
#include "core/Trips.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ubahn::core {

// SPEC 12. fleetSize stays 0; makeParams sets it.
inline api::LineConfig trainingLineConfig() {
    api::LineConfig cfg;
    cfg.stationNames = {"Северная",    "Полевая", "Заводская", "Университет",
                        "Центральная", "Рынок",   "Парк",      "Южная"};
    const double lengths[] = {1100, 780, 1700, 950, 1500, 860, 1400};
    const int32_t standards[] = {120, 90, 130, 100, 110, 95, 125};
    cfg.spans.resize(7);
    for (int i = 0; i < 7; ++i) {
        cfg.spans[static_cast<std::size_t>(i)].lengthM = lengths[i];
        cfg.spans[static_cast<std::size_t>(i)].tStd = standards[i];
    }
    return cfg;
}

// tStd shorter than lengths leaves the remaining spans at the default (empty optional).
inline api::LineConfig makeLineConfig(const std::vector<double>& lengths,
                                      const std::vector<int32_t>& tStd) {
    api::LineConfig cfg;
    cfg.stationNames.resize(lengths.size() + 1);
    for (std::size_t i = 0; i < cfg.stationNames.size(); ++i) {
        cfg.stationNames[i] = "S" + std::to_string(i);
    }
    cfg.spans.resize(lengths.size());
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        cfg.spans[i].lengthM = lengths[i];
        if (i < tStd.size()) {
            cfg.spans[i].tStd = tStd[i];
        }
    }
    return cfg;
}

// SPEC 14.5 load. N = 20 on purpose: the baselines were recorded on this fixture, not on the
// longer Moscow presets. 400..5000 m spans with the default tStd. Fleet is the minimum for all
// windows.
inline api::SimParams twentyStationParams() {
    api::SimParams params;
    params.line = makeLineConfig({4600, 4100, 3200, 2800, 1950, 1700, 1200, 900, 600, 500, 400, 800,
                                  1050, 1400, 1650, 2400, 3050, 4500, 5000},
                                 {});
    params.schedule = api::defaultSchedule();
    params.period = {api::kOpenTod, api::kOpenTod + 6 * 3600};
    params.line.fleetSize = api::analyze(params.line, params.schedule).minFleetForAllWindows;
    return params;
}

// One window per day, 06:00–24:00, shared delay range SPEC 6.3.
inline api::ScheduleConfig constantSchedule(int32_t headway, int32_t dwell, double probability) {
    api::Window window;
    window.fromTod = api::kOpenTod;
    window.toTod = api::kCloseTod;
    window.headway = headway;
    window.dwell = dwell;
    window.delayProb = probability;

    api::ScheduleConfig cfg;
    cfg.delayMin = 20;
    cfg.delayMax = 60;
    for (api::DayProfile& day : cfg.days) {
        day.windows.push_back(window);
    }
    return cfg;
}

inline api::SimParams makeParams(api::LineConfig line, api::ScheduleConfig schedule, int fleetSize,
                                 api::SimPeriod period = {api::kOpenTod,
                                                          api::kOpenTod + 6 * 3600}) {
    api::SimParams params;
    params.line = std::move(line);
    params.line.fleetSize = fleetSize;
    params.schedule = std::move(schedule);
    params.period = period;
    params.stepSec = 60;
    params.rate = 1;
    return params;
}

inline EngineState makeState(const api::SimParams& params, uint64_t seed) {
    return initEngineState(params, seed);
}

// pos 0 -> WaitingSlot, last station -> AlightingTerminal, otherwise Dwelling.
// Does not push the train onto the track list.
inline void placeAtPlatform(EngineState& state, int train, api::Dir dir, int pos) {
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    unit.dir = dir;
    unit.pos = pos;
    unit.aAct = state.now;
    if (pos <= 0) {
        unit.state = TrainState::WaitingSlot;
    } else if (pos >= state.line.n - 1) {
        unit.state = TrainState::AlightingTerminal;
    } else {
        unit.state = TrainState::Dwelling;
    }
    platformAt(state, static_cast<int>(dir), pos).occupant = train;
}

// Running from x = 0 at `depart` to the span length at `arrive`.
inline void placeOnSpan(EngineState& state, int train, api::Dir dir, int pos, api::SimTime depart,
                        api::SimTime arrive) {
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    unit.state = TrainState::Running;
    unit.dir = dir;
    unit.pos = pos;
    unit.departAct = depart;
    const Span& span = spanOf(state.line, dir, pos);
    unit.seg = Segment{depart, arrive, 0, span.lengthM};
}

// InDeadEndWaiting. dir is the direction that arrived at this terminal.
inline void placeInDeadEnd(EngineState& state, int train, api::Term term) {
    Train& unit = state.trains[static_cast<std::size_t>(train)];
    unit.state = TrainState::InDeadEndWaiting;
    unit.dir = term == api::Term::B ? api::Dir::I : api::Dir::II;
    unit.pos = std::max(state.line.n - 1, 0);
    Resource& deadEnd = state.terms[static_cast<std::size_t>(term)].deadEnd;
    deadEnd.occupant = train;
    deadEnd.occupiedSince = state.now;
}

// Appends a trip with id == index. Does not assign it to a train.
// A stub buildTrip (BE-05) returns an empty plan; the vectors are then sized to N.
inline Trip& addTrip(EngineState& state, api::Dir dir, api::SimTime slot, int32_t dwellPlan) {
    const int32_t id = state.nextTripId;
    Trip trip = buildTrip(state.line, id, dir, slot, dwellPlan);
    if (trip.aPlan.empty() || trip.dPlan.empty()) {
        const int n = std::max(state.line.n, 1);
        trip.aPlan.assign(static_cast<std::size_t>(n), slot);
        trip.dPlan.assign(static_cast<std::size_t>(n), slot);
    }
    trip.id = id;
    trip.dir = dir;
    trip.slot = slot;
    trip.dwellPlan = dwellPlan;
    trip.trainId = -1;
    state.nextTripId = id + 1;
    return appendTrip(state, std::move(trip));
}

// Writes train.next only. The heap is left untouched.
inline void setNext(EngineState& state, int train, NextEvent next) {
    state.trains[static_cast<std::size_t>(train)].next = next;
}

inline void fakeHandleTrain(EngineState&, int, api::SimTime, DirtySet&) {}
inline void fakeHandleDispatcher(EngineState&, api::Term, api::SimTime, DirtySet&) {}
inline void fakeHandleCalendar(EngineState&, api::SimTime, DirtySet&) {}
inline NextEvent fakeComputeNext(const EngineState&, int, api::SimTime) {
    return {};
}
inline NextEvent fakeDispatcherNext(const EngineState&, api::Term, api::SimTime) {
    return {};
}
inline NextEvent fakeCalendarNext(const EngineState&, api::SimTime) {
    return {};
}
inline void fakeApplyReschedule(EngineState&, int, const NextEvent&, api::SimTime) {}
inline std::optional<api::ViewDelta> fakeCollectDelta(EngineState&, api::SimTime, DeltaMode) {
    return std::nullopt;
}
inline void fakeCheckInvariants(const EngineState&) {}

// No-ops. computeNext / dispatcherNext / calendarNext return an invalid event,
// so a cascade that sees an already-invalid next stops.
inline Handlers fakeHandlers() {
    return {&fakeHandleTrain,     &fakeHandleDispatcher, &fakeHandleCalendar,
            &fakeComputeNext,     &fakeDispatcherNext,   &fakeCalendarNext,
            &fakeApplyReschedule, &fakeCollectDelta,     &fakeCheckInvariants};
}

} // namespace ubahn::core
