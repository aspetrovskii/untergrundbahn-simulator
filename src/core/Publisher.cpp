#include "core/Publisher.h"

#include "core/Line.h"
#include "core/Schedule.h"
#include "core/Time.h"

#include <cmath>

namespace ubahn::core {
namespace {

constexpr double kAlongSeekTolerance = 1e-4;

double clamp01(double value) {
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }
    return value;
}

bool isServiceState(TrainState state) {
    switch (state) {
    case TrainState::InDepot:
    case TrainState::LeavingDepot:
    case TrainState::ToDeadEnd:
    case TrainState::CabChange:
    case TrainState::InDeadEndWaiting:
    case TrainState::FromDeadEnd:
    case TrainState::ToDepot:
        return true;
    case TrainState::WaitingSlot:
    case TrainState::Dwelling:
    case TrainState::Running:
    case TrainState::StoppedAtSignal:
    case TrainState::AlightingTerminal:
        return false;
    }
    return false;
}

int terminalPos(const EngineState& state) {
    return state.line.n > 0 ? state.line.n - 1 : 0;
}

// Dir::I leaves A and arrives at B. Dir::II is the opposite. stationOf maps both.
int arrivalStation(const EngineState& state, api::Dir dir) {
    return stationOf(state.line, dir, terminalPos(state));
}

int departureStation(const EngineState& state, api::Dir dir) {
    return stationOf(state.line, dir, 0);
}

// Depot packets ignore Train::dir (API.md 4.4). depotAt is the terminal that holds the train.
int depotStation(const EngineState& state, api::Term term) {
    const api::Dir dir = term == api::Term::B ? api::Dir::II : api::Dir::I;
    return departureStation(state, dir);
}

bool spanAvailable(const Line& line, api::Dir dir, int pos) {
    if (line.n < 2 || line.spans.empty()) {
        return false;
    }
    const int index = dir == api::Dir::I ? pos : line.n - 2 - pos;
    return index >= 0 && index < static_cast<int>(line.spans.size());
}

void fillStill(api::TrainUpdate& view, api::TrackEdge edge, int station, double along) {
    view.edge = edge;
    view.station = station;
    view.along = along;
    view.v = 0;
    view.speedKmh = 0;
}

// Running uses the segment. StoppedAtSignal stands at stopX (ALGORITHM 13).
void fillSpan(const EngineState& state, const Train& train, api::SimTime now, bool stopped,
              api::TrainUpdate& view) {
    view.edge = api::TrackEdge::Span;
    view.station = stationOf(state.line, train.dir, train.pos);
    view.along = 0;
    view.v = 0;
    view.speedKmh = 0;
    if (!spanAvailable(state.line, train.dir, train.pos)) {
        return;
    }
    const Span& span = spanOf(state.line, train.dir, train.pos);
    if (!(span.lengthM > 0)) {
        return;
    }
    if (stopped) {
        view.along = clamp01((span.lengthM - state.line.cfg.signalStopOffsetM) / span.lengthM);
        return;
    }
    const double dt = static_cast<double>(train.seg.t1 - train.seg.t0);
    if (!(dt > 0)) {
        view.along = clamp01(train.seg.x0 / span.lengthM);
        return;
    }
    const double metresPerSec = (train.seg.x1 - train.seg.x0) / dt;
    const double fraction = clamp01(static_cast<double>(now - train.seg.t0) / dt);
    const double metres = train.seg.x0 + (train.seg.x1 - train.seg.x0) * fraction;
    view.along = clamp01(metres / span.lengthM);
    view.v = metresPerSec / span.lengthM;
    view.speedKmh = metresPerSec * 3.6;
}

// Completion is one fixed duration after entry (ALGORITHM 8). v stays 1/duration (ALGORITHM 13).
// Without a scheduled end the packet is the start of the edge, which is the entry instant.
void fillTimed(const Train& train, api::SimTime now, api::TrackEdge edge, int station,
               int32_t durationSec, api::TrainUpdate& view) {
    view.edge = edge;
    view.station = station;
    view.speedKmh = 0;
    view.along = 0;
    view.v = 0;
    if (durationSec <= 0) {
        return;
    }
    const double duration = static_cast<double>(durationSec);
    view.v = 1.0 / duration;
    if (!train.next.valid) {
        return;
    }
    const api::SimTime start = train.next.time - static_cast<api::SimTime>(durationSec);
    view.along = clamp01(static_cast<double>(now - start) / duration);
}

bool sameFlags(const api::TrainFlags& a, const api::TrainFlags& b) {
    return a.headingToDepot == b.headingToDepot && a.catchingUp == b.catchingUp &&
           a.slowed == b.slowed && a.held == b.held && a.offHours == b.offHours;
}

// along is not part of a Play diff: the GUI extrapolates it (ALGORITHM 13).
bool trainDiffers(const api::TrainUpdate& sent, const api::TrainUpdate& view) {
    return sent.edge != view.edge || sent.dir != view.dir || sent.station != view.station ||
           sent.v != view.v || sent.mode != view.mode || sent.lateness != view.lateness ||
           !sameFlags(sent.flags, view.flags);
}

bool alongDiverged(const api::TrainUpdate& sent, api::SimTime sentAt, const api::TrainUpdate& view,
                   api::SimTime now) {
    if (sentAt == api::kNoTime) {
        return true;
    }
    const double elapsed = static_cast<double>(now - sentAt);
    double predicted = sent.along + sent.v * elapsed;
    // The GUI clamps the extrapolated fraction at 1 (API.md 4.4).
    if (predicted > 1.0) {
        predicted = 1.0;
    }
    return std::fabs(view.along - predicted) > kAlongSeekTolerance;
}

bool boardDiffers(const api::BoardUpdate& sent, const api::BoardUpdate& view) {
    return sent.occupied != view.occupied || sent.lastArrival != view.lastArrival ||
           sent.dwellSince != view.dwellSince || sent.dwellShown != view.dwellShown ||
           sent.shift != view.shift;
}

bool sameWindow(const api::Window& a, const api::Window& b) {
    return a.fromTod == b.fromTod && a.toTod == b.toTod && a.headway == b.headway &&
           a.dwell == b.dwell && a.delayProb == b.delayProb;
}

bool clockDiffers(const api::ClockUpdate& sent, const api::ClockUpdate& view) {
    return sent.dayOfWeek != view.dayOfWeek || sent.open != view.open ||
           sent.windowIndex != view.windowIndex || sent.hEff != view.hEff ||
           !sameWindow(sent.window, view.window);
}

bool deltaEmpty(const api::ViewDelta& delta) {
    return delta.trains.empty() && delta.boards.empty() && !delta.clock.has_value() &&
           delta.warnings.empty() && delta.delays.empty();
}

} // namespace

api::TrainMode modeOf(const Train& train) {
    // SPEC 10.1, first match. lateness > 0 is CatchUp even without catchingUp (ALGORITHM 5.3).
    if (train.state == TrainState::StoppedAtSignal || train.flags.held) {
        return api::TrainMode::Stopped;
    }
    if (train.flags.offHours || isServiceState(train.state)) {
        return api::TrainMode::Service;
    }
    if (train.flags.slowed) {
        return api::TrainMode::Slowed;
    }
    if (train.flags.catchingUp || train.lateness > 0) {
        return api::TrainMode::CatchUp;
    }
    return api::TrainMode::OnTime;
}

api::TrainUpdate trainView(const EngineState& state, int trainId, api::SimTime now) {
    const Train& train = state.trains[static_cast<std::size_t>(trainId)];
    api::TrainUpdate view;
    view.id = train.id;
    view.mode = modeOf(train);
    view.lateness = train.lateness;
    view.flags = train.flags;
    view.dir = train.dir;

    const api::LineConfig& cfg = state.line.cfg;
    switch (train.state) {
    case TrainState::Running:
        fillSpan(state, train, now, false, view);
        break;
    case TrainState::StoppedAtSignal:
        fillSpan(state, train, now, true, view);
        break;
    case TrainState::Dwelling:
    case TrainState::WaitingSlot:
    case TrainState::AlightingTerminal:
        fillStill(view, api::TrackEdge::Platform, stationOf(state.line, train.dir, train.pos), 0);
        break;
    case TrainState::ToDeadEnd:
        fillTimed(train, now, api::TrackEdge::ToDeadEnd, arrivalStation(state, train.dir),
                  cfg.toDeadEnd, view);
        break;
    case TrainState::CabChange:
    case TrainState::InDeadEndWaiting:
        fillStill(view, api::TrackEdge::ToDeadEnd, arrivalStation(state, train.dir), 1);
        break;
    case TrainState::FromDeadEnd:
        fillTimed(train, now, api::TrackEdge::FromDeadEnd, departureStation(state, train.dir),
                  cfg.fromDeadEnd, view);
        break;
    case TrainState::ToDepot:
        fillTimed(train, now, api::TrackEdge::ToDepot, arrivalStation(state, train.dir),
                  cfg.depotIn, view);
        break;
    case TrainState::LeavingDepot:
        fillTimed(train, now, api::TrackEdge::FromDepot, departureStation(state, train.dir),
                  cfg.depotOut, view);
        break;
    case TrainState::InDepot:
        fillStill(view, api::TrackEdge::Depot, depotStation(state, train.depotAt), 0);
        break;
    }
    return view;
}

api::BoardUpdate boardView(const EngineState& state, api::Dir dir, int pos) {
    const Platform& platform = platformAt(state, static_cast<int>(dir), pos);
    api::BoardUpdate board;
    board.dir = dir;
    board.station = stationOf(state.line, dir, pos);
    board.lastArrival = platform.lastArrival;
    board.shift = platform.lastShift;
    board.dwellShown = platform.lastDwell;
    const int occupant = platform.occupant;
    const bool known = occupant >= 0 && occupant < static_cast<int>(state.trains.size());
    board.occupied = known;
    board.dwellSince = known ? state.trains[static_cast<std::size_t>(occupant)].aAct : api::kNoTime;
    return board;
}

api::ClockUpdate clockView(const EngineState& state, api::SimTime now) {
    api::ClockUpdate clock;
    clock.dayOfWeek = dowOf(now);
    const std::optional<int> index = windowIndexAt(state.schedule, now);
    clock.open = index.has_value();
    clock.windowIndex = index.value_or(-1);
    clock.hEff = hEffAt(state.analysis, state.schedule, now);
    if (const api::Window* window = windowAt(state.schedule, now)) {
        clock.window = *window;
    }
    return clock;
}

std::optional<api::ViewDelta> collectDelta(EngineState& state, api::SimTime now, DeltaMode mode) {
    api::ViewDelta delta;
    delta.t = now;
    const bool snapshot = mode == DeltaMode::Snapshot;

    const int trainCount = static_cast<int>(state.trains.size());
    for (int id = 0; id < trainCount; ++id) {
        const api::TrainUpdate view = trainView(state, id, now);
        const std::optional<api::TrainUpdate>& sent =
            state.sentTrains[static_cast<std::size_t>(id)];
        bool include = snapshot || !sent.has_value() || trainDiffers(*sent, view);
        if (!include && mode == DeltaMode::Seek) {
            include =
                alongDiverged(*sent, state.sentTrainAt[static_cast<std::size_t>(id)], view, now);
        }
        if (!include) {
            continue;
        }
        delta.trains.push_back(view);
        state.sentTrains[static_cast<std::size_t>(id)] = view;
        state.sentTrainAt[static_cast<std::size_t>(id)] = now;
    }

    for (int dirIndex = 0; dirIndex < 2; ++dirIndex) {
        const api::Dir dir = static_cast<api::Dir>(dirIndex);
        const int posCount =
            static_cast<int>(state.platforms[static_cast<std::size_t>(dirIndex)].size());
        for (int pos = 0; pos < posCount; ++pos) {
            const api::BoardUpdate view = boardView(state, dir, pos);
            const std::optional<api::BoardUpdate>& sent =
                state.sentBoards[static_cast<std::size_t>(dirIndex)][static_cast<std::size_t>(pos)];
            if (!snapshot && sent.has_value() && !boardDiffers(*sent, view)) {
                continue;
            }
            delta.boards.push_back(view);
            state.sentBoards[static_cast<std::size_t>(dirIndex)][static_cast<std::size_t>(pos)] =
                view;
        }
    }

    const api::ClockUpdate clock = clockView(state, now);
    if (snapshot || !state.sentClock.has_value() || clockDiffers(*state.sentClock, clock)) {
        delta.clock = clock;
        state.sentClock = clock;
    }

    delta.delays.swap(state.pendingDelays);
    delta.warnings.swap(state.pendingWarnings);
    if (deltaEmpty(delta)) {
        return std::nullopt;
    }
    return delta;
}

} // namespace ubahn::core
