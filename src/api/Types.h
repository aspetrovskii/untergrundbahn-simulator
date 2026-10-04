#pragma once

// Contract data types shared by backend and frontend. No Qt here: src/core includes this file.

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ubahn::api {

// Seconds since Monday 00:00 of week zero.
using SimTime = int64_t;

constexpr SimTime kNoTime = std::numeric_limits<SimTime>::min();
constexpr int32_t kSecondsPerDay = 86400;
constexpr int32_t kOpenTod = 6 * 3600;
constexpr int32_t kCloseTod = 24 * 3600;

// I: A -> B (stations 0 -> N-1), II: B -> A.
enum class Dir : uint8_t { I = 0, II = 1 };
// A = station 0, B = station N-1.
enum class Term : uint8_t { A = 0, B = 1 };

// SPEC 10.1; when several apply, the first in this order wins.
enum class TrainMode : uint8_t { Stopped = 0, Service = 1, Slowed = 2, CatchUp = 3, OnTime = 4 };

enum class TrackEdge : uint8_t {
    Span,
    Platform,
    ToDeadEnd,
    FromDeadEnd,
    ToDepot,
    FromDepot,
    Depot
};

// ---- Input parameters (SPEC 9.1) ----

struct SpanInput {
    double lengthM = 0;          // 400..5000
    std::optional<int32_t> tStd; // empty: defaultStdRunTime(lengthM); otherwise >= physMinRunTime
};

struct LineConfig {
    std::vector<std::string> stationNames; // N = 7..20, UTF-8
    std::vector<SpanInput> spans;          // N-1; spans[i] joins stations i and i+1, both tracks
    int32_t dwellMin = 40;                 // C5, 30..180
    int32_t headwayMin = 65;               // C6, 30..300
    int32_t tClear = 25;                   // C7, 10..120
    double kSlow = 1.3;                    // C4, 1.0..2.0
    int32_t toDeadEnd = 45;                // 10..600
    int32_t cabChange = 30;                // 10..600
    int32_t fromDeadEnd = 45;              // 10..600
    int32_t depotOut = 90;                 // 10..600
    int32_t depotIn = 90;                  // 10..600
    double signalStopOffsetM = 200;        // 50..300, < shortest span
    int32_t signalApproach = 20;           // 10..60
    int fleetSize = 0;                     // k, 2..200
};

struct Window {
    int32_t fromTod = 0; // within [kOpenTod, kCloseTod]
    int32_t toTod = 0;
    int32_t headway = 0; // planned H
    int32_t dwell = 0;   // planned dwell
    double delayProb = 0;
};

// Ordered, no gaps or overlaps, covering 6:00-24:00.
struct DayProfile {
    std::vector<Window> windows;
};

struct ScheduleConfig {
    std::array<DayProfile, 7> days; // Monday..Sunday
    int32_t delayMin = 20;          // 0 <= delayMin <= delayMax <= 600
    int32_t delayMax = 60;
};

struct SimPeriod {
    SimTime start = 0;
    SimTime end = 0; // > start, no upper limit
};

struct SimParams {
    LineConfig line;
    ScheduleConfig schedule;
    SimPeriod period;
    int32_t stepSec = 60;         // playback step, 1..240
    double rate = 1.0;            // steps per real second; 0 starts paused
    std::optional<uint64_t> seed; // empty: backend picks one and reports it in RunInfo
};

struct Preset {
    std::string id; // "training-8", "minimal-7", "long-20"
    std::string name;
    SimParams params;
};

// ---- Validation and schedule analysis ----

enum class IssueCode : uint16_t {
    ValueOutOfRange,
    StationCountOutOfRange,
    SpanCountMismatch,
    StepOutOfRange,
    TStdBelowPhysMin,
    WindowsOverlap,
    WindowsGap,
    WindowOutOfHours,
    HeadwayBelowDwellPlusClear, // C11
    HeadwayBelowTurnaround,     // C12
    HeadwayBelowMin,            // C6
    DwellBelowMin,              // C5
    SignalOffsetTooLong,
    ProbabilityOutOfRange,
    DelayRangeInvalid,
    PeriodInvalid,
};

struct ValidationIssue {
    std::string field; // e.g. "line.spans[2].tStd", "schedule.days[5].windows[1].headway"
    IssueCode code = IssueCode::ValueOutOfRange;
    std::string message; // ready-to-show Russian text
};

struct WindowAnalysis {
    int32_t nReq = 0;
    int32_t hEff = 0;
    bool stretched = false;
};

struct ScheduleAnalysis {
    int32_t tCycle = 0;
    int32_t tTurn = 0;
    std::array<std::vector<WindowAnalysis>, 7> days; // parallel to ScheduleConfig::days[d].windows
    int32_t minFleetForAllWindows = 0;
};

// ---- State updates (ALGORITHM 13) ----

struct TrainFlags {
    bool headingToDepot = false;
    bool catchingUp = false;
    bool slowed = false;
    bool held = false;
    bool offHours = false;
};

struct TrainUpdate {
    int id = 0;
    TrainMode mode = TrainMode::OnTime;
    int32_t lateness = 0;
    TrainFlags flags;
    TrackEdge edge = TrackEdge::Depot;
    Dir dir = Dir::I;
    int station = 0;  // physical station; meaning per edge in docs/API.md 4.4
    double along = 0; // 0..1 along the edge
    double v = 0;     // d(along)/dt, 1/s, >= 0
    double speedKmh = 0;
};

struct BoardUpdate {
    Dir dir = Dir::I;
    int station = 0;
    SimTime lastArrival = kNoTime;
    bool occupied = false;
    SimTime dwellSince = kNoTime; // valid if occupied
    int32_t dwellShown = 0;       // valid if !occupied
    int32_t shift = 0;
};

struct ClockUpdate {
    int dayOfWeek = 0;
    bool open = false;
    int windowIndex = -1; // -1 when closed
    Window window;
    int32_t hEff = 0;
};

enum class WarningCode : uint16_t { IntervalStretched, NoTrainForTrip };

struct Warning {
    WarningCode code = WarningCode::IntervalStretched;
    int dayOfWeek = -1;           // IntervalStretched
    int windowIndex = -1;         // IntervalStretched
    int32_t plannedHeadway = 0;   // IntervalStretched
    int32_t effectiveHeadway = 0; // IntervalStretched
    int32_t requiredTrains = 0;   // IntervalStretched
    int terminal = -1;            // NoTrainForTrip: Term
    SimTime tripSlot = 0;         // NoTrainForTrip
    std::string message;          // ready-to-show Russian text
};

struct DelayNotice {
    int trainId = 0;
    Dir dir = Dir::I;
    int station = 0;
    int32_t seconds = 0;
};

struct ViewDelta {
    SimTime t = 0;
    std::vector<TrainUpdate> trains;
    std::vector<BoardUpdate> boards;
    std::optional<ClockUpdate> clock;
    std::vector<Warning> warnings;
    std::vector<DelayNotice> delays;
};

enum class ChunkKind : uint8_t { Initial, Play, Seek };

struct ViewChunk {
    uint64_t seq = 0;
    ChunkKind kind = ChunkKind::Play;
    SimTime from = 0;
    SimTime to = 0;
    std::vector<ViewDelta> deltas; // ascending t; from < t <= to (Initial and Seek: t == to)
};

using ViewChunkPtr = std::shared_ptr<const ViewChunk>;

// ---- Run info and summary (SPEC 11) ----

struct RunInfo {
    uint64_t seed = 0;
    SimPeriod period;
    SimTime warmupFrom = 0;
    ScheduleAnalysis analysis;
    std::vector<Warning> warnings;
};

struct HeadwayStat {
    int64_t count = 0;
    double meanSec = 0;
    int32_t maxSec = 0;
};

struct EpisodeStat {
    int64_t count = 0;
    double meanSec = 0;
    int32_t maxSec = 0;
};

struct AddedTimeStat {
    int64_t count = 0;
    int64_t totalSec = 0;
};

struct StretchInfo {
    int dayOfWeek = 0;
    int windowIndex = 0;
    int32_t planned = 0;
    int32_t effective = 0;
};

struct SimSummary {
    SimPeriod period;
    SimTime stoppedAt = 0;
    bool completed = false; // true: reached period.end; false: stop()
    std::array<int64_t, 2> delayCount{};
    std::array<int64_t, 2> stopCount{};
    double delayMeanSec = 0;
    double delayShare = 0;
    HeadwayStat headwayTotal;
    std::array<HeadwayStat, 2> headwayByDir;
    std::array<std::vector<HeadwayStat>, 2> headwayByStation; // [Dir][station]
    int32_t maxLatenessSec = 0;
    EpisodeStat recovery;
    AddedTimeStat holds;
    AddedTimeStat slowdowns;
    AddedTimeStat signalStops;
    std::vector<StretchInfo> stretchedWindows;
};

// ---- Runtime errors ----

enum class ErrorCode : uint16_t { InvalidParams, InvalidState, InternalError };

struct RuntimeError {
    ErrorCode code = ErrorCode::InternalError;
    std::string message;
    std::vector<ValidationIssue> issues;
};

} // namespace ubahn::api
