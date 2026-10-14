#include "core/Invariants.h"

#include "core/Schedule.h"

#include <string>
#include <vector>

namespace ubahn::core {
namespace {

// kNoTime is INT64_MIN. Adding a gap to it overflows, so a missing sample drops
// the constraint (Invariants.h, EngineState.h).
bool gapViolated(api::SimTime value, api::SimTime sample, int64_t gap) {
    if (sample == api::kNoTime) {
        return false;
    }
    if (value < sample) {
        return true;
    }
    return value - sample < gap;
}

bool knownDir(api::Dir dir) {
    return dir == api::Dir::I || dir == api::Dir::II;
}

int dirIndexOf(api::Dir dir) {
    return dir == api::Dir::II ? 1 : 0;
}

bool onPlatform(TrainState state) {
    return state == TrainState::WaitingSlot || state == TrainState::Dwelling ||
           state == TrainState::AlightingTerminal;
}

// Dead end is held from LeaveToDeadEnd until PlatformReachedFromDeadEnd (ALGORITHM 7.5).
// Direction flips at LeaveDeadEnd, so FromDeadEnd already faces the departure path.
bool inDeadEnd(TrainState state) {
    return state == TrainState::ToDeadEnd || state == TrainState::CabChange ||
           state == TrainState::InDeadEndWaiting || state == TrainState::FromDeadEnd;
}

bool inThroat(TrainState state) {
    return state == TrainState::LeavingDepot || state == TrainState::ToDepot;
}

// Entered at WaitingSlot, left when the train leaves the arrival platform (ALGORITHM 5.5).
bool onTrack(TrainState state) {
    return state == TrainState::WaitingSlot || state == TrainState::Dwelling ||
           state == TrainState::Running || state == TrainState::StoppedAtSignal ||
           state == TrainState::AlightingTerminal;
}

api::Term deadEndTerm(const Train& train) {
    if (train.state == TrainState::FromDeadEnd) {
        return termOfDeparture(train.dir);
    }
    return termOfArrival(train.dir);
}

api::Term throatTerm(const Train& train) {
    if (train.state == TrainState::LeavingDepot) {
        return train.depotAt;
    }
    return termOfArrival(train.dir);
}

// Higher rank is closer to the destination (the head). A train on the span that
// starts at pos has already left that station, so it is ahead of a dwell there.
struct Progress {
    int pos = 0;
    int stage = 0;
    double along = 0;
};

int compareProgress(const Progress& ahead, const Progress& behind) {
    if (ahead.pos != behind.pos) {
        return ahead.pos < behind.pos ? -1 : 1;
    }
    if (ahead.stage != behind.stage) {
        return ahead.stage < behind.stage ? -1 : 1;
    }
    if (ahead.along < behind.along - 1e-6) {
        return -1;
    }
    if (ahead.along > behind.along + 1e-6) {
        return 1;
    }
    return 0;
}

double positionOnSpan(const EngineState& state, const Train& train) {
    const Segment& seg = train.seg;
    if (seg.t1 <= seg.t0 || state.now <= seg.t0) {
        return seg.x0;
    }
    if (state.now >= seg.t1) {
        return seg.x1;
    }
    const double fraction =
        static_cast<double>(state.now - seg.t0) / static_cast<double>(seg.t1 - seg.t0);
    return seg.x0 + (seg.x1 - seg.x0) * fraction;
}

Progress progressOf(const EngineState& state, const Train& train) {
    Progress progress;
    progress.pos = train.pos;
    if (train.state == TrainState::Running || train.state == TrainState::StoppedAtSignal) {
        progress.stage = 1;
        progress.along = positionOnSpan(state, train);
    }
    return progress;
}

std::string joinIds(const std::vector<int>& ids) {
    std::string text;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += std::to_string(ids[i]);
    }
    return text;
}

bool spanAvailable(const Line& line, api::Dir dir, int pos) {
    if (!knownDir(dir) || pos < 0 || line.n < 2) {
        return false;
    }
    const int index = dir == api::Dir::I ? pos : line.n - 2 - pos;
    return index >= 0 && index < static_cast<int>(line.spans.size());
}

// InDepot with no trip is the only train that must stay out of the heap
// (Invariants.h, ALGORITHM 1 principle 1).
bool heapExempt(const Train& train) {
    return train.state == TrainState::InDepot && train.tripId < 0;
}

bool heapKeyMatches(const EventKey& key, int train, const NextEvent& next) {
    return key.time == next.time && key.priority == priorityOf(next.type) &&
           key.owner == static_cast<int32_t>(train);
}

void checkOccupancy(const EngineState& state, std::vector<std::string>& out) {
    const int trainCount = static_cast<int>(state.trains.size());
    const int dirCount = 2;
    std::vector<std::vector<int>> deadEnds(static_cast<std::size_t>(dirCount));
    std::vector<std::vector<int>> throats(static_cast<std::size_t>(dirCount));
    int platformSlots = 0;
    for (int dir = 0; dir < dirCount; ++dir) {
        const int slots = static_cast<int>(state.platforms[static_cast<std::size_t>(dir)].size());
        if (slots > platformSlots) {
            platformSlots = slots;
        }
    }
    std::vector<std::vector<int>> platformTrains(
        static_cast<std::size_t>(dirCount * platformSlots));

    for (int id = 0; id < trainCount; ++id) {
        const Train& train = state.trains[static_cast<std::size_t>(id)];
        if (!knownDir(train.dir) && (onPlatform(train.state) || inDeadEnd(train.state) ||
                                     inThroat(train.state) || onTrack(train.state))) {
            out.push_back("C9: train " + std::to_string(id) + " has an invalid direction");
            continue;
        }
        if (onPlatform(train.state)) {
            const int dir = dirIndexOf(train.dir);
            const int slots =
                static_cast<int>(state.platforms[static_cast<std::size_t>(dir)].size());
            if (train.pos < 0 || train.pos >= slots) {
                out.push_back("C9: train " + std::to_string(id) +
                              " platform index is out of range");
            } else {
                platformTrains[static_cast<std::size_t>(dir * platformSlots + train.pos)].push_back(
                    id);
            }
        }
        if (inDeadEnd(train.state)) {
            const int term = static_cast<int>(deadEndTerm(train));
            if (term < 0 || term >= dirCount) {
                out.push_back("C9: train " + std::to_string(id) + " dead-end terminal is invalid");
            } else {
                deadEnds[static_cast<std::size_t>(term)].push_back(id);
            }
        }
        if (inThroat(train.state)) {
            const int term = static_cast<int>(throatTerm(train));
            if (term < 0 || term >= dirCount) {
                out.push_back("C9: train " + std::to_string(id) + " throat terminal is invalid");
            } else {
                throats[static_cast<std::size_t>(term)].push_back(id);
            }
        }
    }

    for (int dir = 0; dir < dirCount; ++dir) {
        const int slots = static_cast<int>(state.platforms[static_cast<std::size_t>(dir)].size());
        for (int pos = 0; pos < slots; ++pos) {
            const std::vector<int>& ids =
                platformTrains[static_cast<std::size_t>(dir * platformSlots + pos)];
            if (ids.size() > 1) {
                out.push_back("C9: platform dir " + std::to_string(dir) + " pos " +
                              std::to_string(pos) + " holds trains " + joinIds(ids));
            }
        }
    }
    for (int term = 0; term < dirCount; ++term) {
        if (deadEnds[static_cast<std::size_t>(term)].size() > 1) {
            out.push_back("C9: dead end " + std::to_string(term) + " holds trains " +
                          joinIds(deadEnds[static_cast<std::size_t>(term)]));
        }
        if (throats[static_cast<std::size_t>(term)].size() > 1) {
            out.push_back("C9: throat " + std::to_string(term) + " holds trains " +
                          joinIds(throats[static_cast<std::size_t>(term)]));
        }
    }
}

void checkTrackOrder(const EngineState& state, std::vector<std::string>& out) {
    const int trainCount = static_cast<int>(state.trains.size());
    std::vector<int> listedAt(static_cast<std::size_t>(trainCount), -1);

    for (int dir = 0; dir < 2; ++dir) {
        const TrackList& list = state.tracks[static_cast<std::size_t>(dir)];
        if (list.head < 0 && list.tail >= 0) {
            out.push_back("C10: track dir " + std::to_string(dir) +
                          " tail is set but head is empty");
        }
        int previous = -1;
        int current = list.head;
        bool haveRank = false;
        Progress previousRank;
        bool broken = false;
        int steps = 0;
        while (current != -1) {
            if (current < 0 || current >= trainCount) {
                out.push_back("C10: track dir " + std::to_string(dir) + " has an invalid train id");
                broken = true;
                break;
            }
            if (listedAt[static_cast<std::size_t>(current)] >= 0 || steps > trainCount) {
                out.push_back("C10: track dir " + std::to_string(dir) + " has a cycle at train " +
                              std::to_string(current));
                broken = true;
                break;
            }
            listedAt[static_cast<std::size_t>(current)] = dir;
            const Train& train = state.trains[static_cast<std::size_t>(current)];
            if (train.prevOnTrack != previous) {
                out.push_back("C10: track dir " + std::to_string(dir) + " link at train " +
                              std::to_string(current) + " does not point back");
            }
            if (!knownDir(train.dir) || dirIndexOf(train.dir) != dir) {
                out.push_back("C10: train " + std::to_string(current) + " is listed on track dir " +
                              std::to_string(dir) + " but faces another direction");
            } else if (!onTrack(train.state)) {
                out.push_back("C10: train " + std::to_string(current) + " is listed on track dir " +
                              std::to_string(dir) + " but is not on the path");
            } else {
                const Progress rank = progressOf(state, train);
                if (haveRank && compareProgress(previousRank, rank) <= 0) {
                    out.push_back("C10: track dir " + std::to_string(dir) +
                                  " order does not match positions at train " +
                                  std::to_string(current));
                }
                previousRank = rank;
                haveRank = true;
            }
            previous = current;
            current = train.nextOnTrack;
            ++steps;
        }
        if (!broken && list.tail != previous) {
            out.push_back("C10: track dir " + std::to_string(dir) +
                          " tail does not match the list");
        }
    }

    for (int id = 0; id < trainCount; ++id) {
        const Train& train = state.trains[static_cast<std::size_t>(id)];
        if (!onTrack(train.state) || !knownDir(train.dir)) {
            continue;
        }
        const int dir = dirIndexOf(train.dir);
        if (listedAt[static_cast<std::size_t>(id)] != dir) {
            out.push_back("C10: train " + std::to_string(id) + " is on dir " + std::to_string(dir) +
                          " but not in that track list");
        }
    }
}

void checkHeap(const EngineState& state, std::vector<std::string>& out) {
    const int trainCount = static_cast<int>(state.trains.size());
    for (int id = 0; id < trainCount; ++id) {
        const Train& train = state.trains[static_cast<std::size_t>(id)];
        const bool present = state.heap.contains(id);
        if (heapExempt(train)) {
            if (present) {
                out.push_back("principle 1: extra heap entry for train " + std::to_string(id) +
                              " (InDepot, no trip)");
            }
            if (train.next.valid) {
                out.push_back("principle 1: train " + std::to_string(id) +
                              " is InDepot without a trip but next is valid");
            }
            continue;
        }
        if (!present) {
            out.push_back("principle 1: train " + std::to_string(id) +
                          " is missing its heap entry");
            continue;
        }
        if (!train.next.valid) {
            out.push_back("principle 1: train " + std::to_string(id) +
                          " has a heap entry but next is invalid");
            continue;
        }
        // keyOf requires contains(owner) (IndexedMinHeap.h).
        if (!heapKeyMatches(state.heap.keyOf(id), id, train.next)) {
            out.push_back("principle 1: heap key for train " + std::to_string(id) +
                          " does not match next");
        }
    }
}

} // namespace

std::vector<std::string> checkState(const EngineState& state) {
    std::vector<std::string> out;
    checkOccupancy(state, out);
    checkTrackOrder(state, out);
    checkHeap(state, out);
    return out;
}

std::vector<std::string> checkArrival(const EngineState& state, int train, api::SimTime arrival) {
    std::vector<std::string> out;
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        out.push_back("train index out of range");
        return out;
    }
    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    // Caller sets pos to the arrival platform before writing lastArrival (ALGORITHM 8).
    if (!knownDir(unit.dir)) {
        out.push_back("C6: train " + std::to_string(train) + " has an invalid direction");
    } else {
        const int dir = dirIndexOf(unit.dir);
        const std::vector<Platform>& row = state.platforms[static_cast<std::size_t>(dir)];
        if (unit.pos < 0 || unit.pos >= static_cast<int>(row.size())) {
            out.push_back("C6: train " + std::to_string(train) +
                          " arrival platform is out of range");
        } else {
            const Platform& platform = row[static_cast<std::size_t>(unit.pos)];
            const int32_t headwayMin = state.line.cfg.headwayMin;
            const int32_t tClear = state.line.cfg.tClear;
            if (gapViolated(arrival, platform.lastArrival, headwayMin)) {
                out.push_back("C6: train " + std::to_string(train) + " arrival " +
                              std::to_string(arrival) + " is earlier than lastArrival " +
                              std::to_string(platform.lastArrival) + " + headwayMin " +
                              std::to_string(headwayMin));
            }
            if (gapViolated(arrival, platform.lastDeparture, tClear)) {
                out.push_back("C7: train " + std::to_string(train) + " arrival " +
                              std::to_string(arrival) + " is earlier than lastDeparture " +
                              std::to_string(platform.lastDeparture) + " + tClear " +
                              std::to_string(tClear));
            }
        }
    }

    const Trip* trip = tripOf(state, train);
    if (trip != nullptr && unit.pos >= 0 && unit.pos < static_cast<int>(trip->aPlan.size()) &&
        arrival < trip->aPlan[static_cast<std::size_t>(unit.pos)]) {
        out.push_back("C8: train " + std::to_string(train) + " arrival " + std::to_string(arrival) +
                      " is earlier than aPlan " +
                      std::to_string(trip->aPlan[static_cast<std::size_t>(unit.pos)]));
    }
    return out;
}

std::vector<std::string> checkDeparture(const EngineState& state, int train, api::SimTime depart,
                                        int32_t tRun) {
    std::vector<std::string> out;
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        out.push_back("train index out of range");
        return out;
    }
    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    // tRun is the running time only. Signal time is outside C4 (SPEC 5, ALGORITHM 7.4).
    if (!spanAvailable(state.line, unit.dir, unit.pos)) {
        out.push_back("C3: train " + std::to_string(train) + " has no span at pos " +
                      std::to_string(unit.pos));
    } else {
        const Span& span = spanOf(state.line, unit.dir, unit.pos);
        if (tRun < span.tMin) {
            out.push_back("C3: train " + std::to_string(train) + " tRun " + std::to_string(tRun) +
                          " is below tMin " + std::to_string(span.tMin));
        }
        if (tRun > span.tMax) {
            out.push_back("C4: train " + std::to_string(train) + " tRun " + std::to_string(tRun) +
                          " is above tMax " + std::to_string(span.tMax));
        }
    }

    const int32_t dwellMin = state.line.cfg.dwellMin;
    if (depart < unit.aAct || depart - unit.aAct < dwellMin) {
        out.push_back("C5: train " + std::to_string(train) + " dwell " +
                      std::to_string(depart - unit.aAct) + " is below dwellMin " +
                      std::to_string(dwellMin));
    }

    const Trip* trip = tripOf(state, train);
    if (trip != nullptr && unit.pos >= 0 && unit.pos < static_cast<int>(trip->dPlan.size()) &&
        depart < trip->dPlan[static_cast<std::size_t>(unit.pos)]) {
        out.push_back("C8: train " + std::to_string(train) + " departure " +
                      std::to_string(depart) + " is earlier than dPlan " +
                      std::to_string(trip->dPlan[static_cast<std::size_t>(unit.pos)]));
    }
    return out;
}

std::vector<std::string> checkDeadEndEntry(const EngineState& state, int train,
                                           api::SimTime entry) {
    std::vector<std::string> out;
    if (train < 0 || train >= static_cast<int>(state.trains.size())) {
        out.push_back("train index out of range");
        return out;
    }
    const Train& unit = state.trains[static_cast<std::size_t>(train)];
    // LeaveToDeadEnd, before dir flips. The arrival terminal owns this dead end.
    if (!knownDir(unit.dir)) {
        out.push_back("C12: train " + std::to_string(train) + " has an invalid direction");
        return out;
    }
    const int term = static_cast<int>(termOfArrival(unit.dir));
    const Resource& deadEnd = state.terms[static_cast<std::size_t>(term)].deadEnd;
    const int32_t turn = tTurn(state.line.cfg);
    if (deadEnd.occupant >= 0) {
        out.push_back("C12: dead end " + std::to_string(term) + " is still occupied by train " +
                      std::to_string(deadEnd.occupant));
    }
    // occupiedSince is the previous entry; freeAt is the previous release (Invariants.h).
    if (gapViolated(entry, deadEnd.occupiedSince, turn)) {
        out.push_back("C12: dead-end entry interval of train " + std::to_string(train) +
                      " is shorter than T_turn " + std::to_string(turn));
    }
    if (deadEnd.freeAt != api::kNoTime &&
        gapViolated(deadEnd.freeAt, deadEnd.occupiedSince, turn)) {
        out.push_back("C12: dead-end occupancy before train " + std::to_string(train) +
                      " is shorter than T_turn " + std::to_string(turn));
    }
    return out;
}

} // namespace ubahn::core
