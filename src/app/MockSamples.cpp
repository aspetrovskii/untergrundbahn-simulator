#include "MockSamples.h"

#include <memory>

namespace ubahn::app::mock {
namespace {

constexpr api::SimTime kStart = 7 * 3600;
constexpr api::SimTime kEnd = 10 * 3600;

api::TrainUpdate train(int id, api::TrainMode mode, api::TrackEdge edge, api::Dir dir, int station,
                       double along, double speedKmh) {
    api::TrainUpdate value;
    value.id = id;
    value.mode = mode;
    value.edge = edge;
    value.dir = dir;
    value.station = station;
    value.along = along;
    value.speedKmh = speedKmh;
    return value;
}

api::ViewChunkPtr initialChunk() {
    auto chunk = std::make_shared<api::ViewChunk>();
    chunk->seq = 0;
    chunk->kind = api::ChunkKind::Initial;
    chunk->from = kStart;
    chunk->to = kStart;

    api::ViewDelta delta;
    delta.t = kStart;
    delta.trains = {
        train(0, api::TrainMode::Service, api::TrackEdge::Depot, api::Dir::I, 0, 0.0, 0.0),
        train(1, api::TrainMode::Service, api::TrackEdge::Depot, api::Dir::II, 7, 0.0, 0.0),
        train(2, api::TrainMode::OnTime, api::TrackEdge::Span, api::Dir::I, 1, 0.35, 42.0),
        train(3, api::TrainMode::OnTime, api::TrackEdge::Platform, api::Dir::I, 2, 0.0, 0.0),
        train(4, api::TrainMode::Service, api::TrackEdge::ToDeadEnd, api::Dir::I, 7, 0.4, 0.0),
        train(5, api::TrainMode::Service, api::TrackEdge::FromDeadEnd, api::Dir::I, 0, 0.6, 0.0),
        train(6, api::TrainMode::Service, api::TrackEdge::ToDepot, api::Dir::II, 0, 0.2, 0.0),
        train(7, api::TrainMode::Service, api::TrackEdge::FromDepot, api::Dir::II, 7, 0.8, 0.0),
    };

    for (int station = 0; station < 8; ++station) {
        for (api::Dir dir : {api::Dir::I, api::Dir::II}) {
            api::BoardUpdate board;
            board.dir = dir;
            board.station = station;
            delta.boards.push_back(board);
        }
    }

    api::ClockUpdate clock;
    clock.dayOfWeek = 0;
    clock.open = true;
    clock.windowIndex = 1;
    clock.window = {7 * 3600, 10 * 3600, 120, 80, 0.55};
    clock.hEff = 120;
    delta.clock = clock;
    chunk->deltas.push_back(delta);
    return chunk;
}

api::ViewChunkPtr firstPlayChunk() {
    auto chunk = std::make_shared<api::ViewChunk>();
    chunk->seq = 1;
    chunk->kind = api::ChunkKind::Play;
    chunk->from = kStart;
    chunk->to = kStart + 60;

    api::ViewDelta delta;
    delta.t = kStart + 31;

    auto departing =
        train(3, api::TrainMode::OnTime, api::TrackEdge::Span, api::Dir::I, 2, 0.0, 47.1);
    departing.v = 1.0 / 130.0;
    auto delayed =
        train(7, api::TrainMode::OnTime, api::TrackEdge::Platform, api::Dir::II, 5, 0.0, 0.0);
    delta.trains = {departing, delayed};

    delta.boards = {
        {api::Dir::I, 2, kStart - 49, false, api::kNoTime, 80, 0},
        {api::Dir::II, 5, kStart + 31, true, kStart + 31, 0, 0},
    };
    delta.delays.push_back({7, api::Dir::II, 5, 37});
    chunk->deltas.push_back(delta);
    return chunk;
}

api::ViewChunkPtr secondPlayChunk() {
    auto chunk = std::make_shared<api::ViewChunk>();
    chunk->seq = 2;
    chunk->kind = api::ChunkKind::Play;
    chunk->from = kStart + 60;
    chunk->to = kStart + 120;

    api::ViewDelta delta;
    delta.t = kStart + 90;
    auto slowed = train(2, api::TrainMode::Slowed, api::TrackEdge::Span, api::Dir::I, 1, 0.7, 31.0);
    slowed.lateness = 25;
    slowed.flags.slowed = true;
    delta.trains.push_back(slowed);

    api::Warning warning;
    warning.code = api::WarningCode::IntervalStretched;
    warning.dayOfWeek = 0;
    warning.windowIndex = 1;
    warning.plannedHeadway = 120;
    warning.effectiveHeadway = 150;
    warning.requiredTrains = 14;
    warning.message = "Интервал растянут со 120 до 150 с: требуется 14 поездов";
    delta.warnings.push_back(warning);
    chunk->deltas.push_back(delta);
    return chunk;
}

api::ViewChunkPtr seekChunk() {
    auto chunk = std::make_shared<api::ViewChunk>();
    chunk->seq = 3;
    chunk->kind = api::ChunkKind::Seek;
    chunk->from = 9 * 3600 + 30 * 60;
    chunk->to = chunk->from;

    api::ViewDelta delta;
    delta.t = chunk->to;
    delta.trains.push_back(
        train(3, api::TrainMode::CatchUp, api::TrackEdge::Span, api::Dir::I, 5, 0.5, 58.0));
    delta.trains.front().lateness = 18;
    delta.trains.front().flags.catchingUp = true;
    chunk->deltas.push_back(delta);
    return chunk;
}

} // namespace

api::RunInfo runInfo() {
    api::RunInfo info;
    info.seed = 42;
    info.period = {kStart, kEnd};
    info.warmupFrom = 3 * 3600;
    info.analysis.tCycle = 1440;
    info.analysis.tTurn = 120;
    info.analysis.minFleetForAllWindows = 12;
    return info;
}

std::vector<api::ViewChunkPtr> viewChunks() {
    return {initialChunk(), firstPlayChunk(), secondPlayChunk(), seekChunk()};
}

api::SimSummary summary() {
    api::SimSummary value;
    value.period = {kStart, kEnd};
    value.stoppedAt = kEnd;
    value.completed = true;
    value.delayCount = {12, 10};
    value.stopCount = {48, 47};
    value.delayMeanSec = 38.5;
    value.delayShare = 22.0 / 95.0;
    value.headwayTotal = {80, 123.4, 170};
    value.headwayByDir = {{{40, 122.0, 165}, {40, 124.8, 170}}};
    value.maxLatenessSec = 95;
    value.recovery = {6, 310.0, 540};
    value.holds = {4, 85};
    value.slowdowns = {7, 140};
    value.signalStops = {2, 35};
    value.stretchedWindows.push_back({0, 1, 120, 150});
    return value;
}

} // namespace ubahn::app::mock
