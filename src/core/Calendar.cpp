#include "core/Calendar.h"

#include "core/Time.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace ubahn::core {
namespace {

bool windowTouches(const api::ScheduleConfig& schedule, api::SimTime t) {
    const int32_t tod = todOf(t);
    const int day = dowOf(t);
    const std::vector<api::Window>& windows = schedule.days[static_cast<std::size_t>(day)].windows;
    for (const api::Window& window : windows) {
        if (window.fromTod == tod || window.toTod == tod) {
            return true;
        }
    }
    return false;
}

// First matching kind wins when several boundaries share one timestamp (Calendar.h).
// Midnight is both service close and the new day, so the type is ServiceClose.
// 06:00 is both service open and the first window, so the type is ServiceOpen.
EventType typeAt(const api::ScheduleConfig& schedule, api::SimTime t) {
    const int32_t tod = todOf(t);
    if (tod == 0) {
        return EventType::ServiceClose;
    }
    if (tod == api::kOpenTod) {
        return EventType::ServiceOpen;
    }
    if (windowTouches(schedule, t)) {
        return EventType::WindowBoundary;
    }
    return EventType::NightRebalance;
}

void consider(api::SimTime candidate, api::SimTime now, api::SimTime& best, bool& found) {
    if (candidate <= now) {
        return;
    }
    if (!found || candidate < best) {
        best = candidate;
        found = true;
    }
}

void setOffHours(EngineState& state, bool offHours, DirtySet* dirty) {
    for (int id = 0; id < static_cast<int>(state.trains.size()); ++id) {
        Train& train = state.trains[static_cast<std::size_t>(id)];
        if (train.state == TrainState::InDepot) {
            continue;
        }
        train.flags.offHours = offHours;
        if (dirty != nullptr) {
            dirty->push(id);
        }
    }
}

void wakeDispatchers(const EngineState& state, DirtySet& dirty) {
    const int fleet = static_cast<int>(state.trains.size());
    dirty.push(dispatcherOwner(fleet, api::Term::A));
    dirty.push(dispatcherOwner(fleet, api::Term::B));
}

// Front of the deque is issued first (ALGORITHM 10.2). The surplus leaves from the
// back, in the same order, so the trains about to go out stay (ALGORITHM 10.4).
void moveTail(std::deque<int>& from, std::deque<int>& to, int count, EngineState& state,
              api::Term dest) {
    const auto split = from.end() - count;
    for (auto it = split; it != from.end(); ++it) {
        state.trains[static_cast<std::size_t>(*it)].depotAt = dest;
        to.push_back(*it);
    }
    from.erase(split, from.end());
}

std::vector<int> depotSide(const EngineState& state) {
    std::vector<int> side(state.trains.size(), -1);
    const std::deque<int>& depotA = state.terms[static_cast<std::size_t>(api::Term::A)].depot;
    const std::deque<int>& depotB = state.terms[static_cast<std::size_t>(api::Term::B)].depot;
    for (const int id : depotA) {
        if (id >= 0 && id < static_cast<int>(side.size())) {
            side[static_cast<std::size_t>(id)] = static_cast<int>(api::Term::A);
        }
    }
    for (const int id : depotB) {
        if (id >= 0 && id < static_cast<int>(side.size())) {
            side[static_cast<std::size_t>(id)] = static_cast<int>(api::Term::B);
        }
    }
    return side;
}

} // namespace

NextEvent calendarNext(const EngineState& state, api::SimTime now) {
    // Boundaries of this calendar day and the next. 24:00 is the next midnight, so it
    // is not added as its own offset. dayStart / dowOf use floor division (decision R16).
    const api::SimTime origin = dayStart(now);
    api::SimTime best = 0;
    bool found = false;
    for (int ahead = 0; ahead < 2; ++ahead) {
        const api::SimTime midnight =
            origin + static_cast<api::SimTime>(ahead) * api::kSecondsPerDay;
        consider(midnight, now, best, found);
        consider(midnight + kWarmupTod, now, best, found);
        consider(midnight + api::kOpenTod, now, best, found);
        const int day = dowOf(midnight);
        const std::vector<api::Window>& windows =
            state.schedule.days[static_cast<std::size_t>(day)].windows;
        for (const api::Window& window : windows) {
            if (window.fromTod > 0 && window.fromTod < api::kSecondsPerDay) {
                consider(midnight + window.fromTod, now, best, found);
            }
            if (window.toTod > 0 && window.toTod < api::kSecondsPerDay) {
                consider(midnight + window.toTod, now, best, found);
            }
        }
    }

    if (!found) {
        return {};
    }
    NextEvent event;
    event.type = typeAt(state.schedule, best);
    event.time = best;
    event.valid = true;
    return event;
}

void handleCalendar(EngineState& state, api::SimTime now, DirtySet& dirty) {
    // Every boundary that falls on this timestamp runs, whatever type calendarNext
    // chose for the heap (ALGORITHM 11). Close does not push: only open puts trains
    // into dirty (Calendar.h).
    const int32_t tod = todOf(now);
    if (tod == 0) {
        setOffHours(state, true, nullptr);
        wakeDispatchers(state, dirty);
    }
    if (tod == api::kOpenTod) {
        setOffHours(state, false, &dirty);
    }
    if (tod != 0 && windowTouches(state.schedule, now)) {
        wakeDispatchers(state, dirty);
    }
    if (tod == kWarmupTod) {
        const std::vector<int> before = depotSide(state);
        rebalanceDepots(state, now);
        const std::vector<int> after = depotSide(state);
        for (int id = 0; id < static_cast<int>(before.size()); ++id) {
            if (before[static_cast<std::size_t>(id)] != after[static_cast<std::size_t>(id)]) {
                dirty.push(id);
            }
        }
    }
}

void rebalanceDepots(EngineState& state, api::SimTime) {
    std::deque<int>& depotA = state.terms[static_cast<std::size_t>(api::Term::A)].depot;
    std::deque<int>& depotB = state.terms[static_cast<std::size_t>(api::Term::B)].depot;
    const int countA = static_cast<int>(depotA.size());
    const int countB = static_cast<int>(depotB.size());
    // Each transfer changes the difference by 2, so half the excess is enough for |A|-|B| <= 1.
    if (countA > countB + 1) {
        moveTail(depotA, depotB, (countA - countB) / 2, state, api::Term::B);
    } else if (countB > countA + 1) {
        moveTail(depotB, depotA, (countB - countA) / 2, state, api::Term::A);
    }
}

} // namespace ubahn::core
