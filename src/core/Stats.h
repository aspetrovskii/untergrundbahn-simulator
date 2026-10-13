#pragma once

#include "api/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ubahn::core {

// Accumulators for ALGORITHM 12. Hooks are called by TrainEvents; fold is what
// Engine::summary returns (API.md 4.5). The cpp is a stub until BE-15.
class Stats {
public:
    Stats() = default;
    Stats(api::SimPeriod period, int nStations);

    // station is the physical index 0..N-1. Accounting itself decides whether
    // `now` is inside the period and inside 06:00–24:00.
    void onStop(api::Dir dir, int station, api::SimTime now, int32_t drawnDelay);
    void onArrival(api::Dir dir, int station, api::SimTime now);
    void onLateness(int train, int32_t lateness, api::SimTime now);
    void onDeparture(int train, int32_t lateDep, api::SimTime now);
    void onHold(int32_t sec, api::SimTime now);
    void onSlowdown(int32_t sec, api::SimTime now);
    void onSignalStop(int32_t sec, api::SimTime now);
    void setStretchedWindows(std::vector<api::StretchInfo> windows);

    api::SimSummary fold(api::SimTime stoppedAt, bool completed) const;

private:
    struct Accumulator {
        int64_t count = 0;
        int64_t sumSec = 0;
        int32_t maxSec = 0;
    };

    api::SimPeriod period_{};
    std::array<int64_t, 2> delayCount_{};
    std::array<int64_t, 2> stopCount_{};
    std::array<int64_t, 2> delaySum_{};
    // [dir][physical station]. lastArrival_ is kNoTime until the first sample.
    // Headway intervals stay inside one service day (decision R15); Stats keeps
    // its own last-arrival clock because onArrival does not receive the previous one.
    std::array<std::vector<Accumulator>, 2> headway_{};
    std::array<std::vector<api::SimTime>, 2> lastArrival_{};
    int32_t maxLateness_ = 0;
    Accumulator recovery_{};
    Accumulator holds_{};
    Accumulator slowdowns_{};
    Accumulator signalStops_{};
    std::vector<api::StretchInfo> stretched_{};
    // Per train, kNoTime when no lateness episode is open. Grown on use.
    std::vector<api::SimTime> episodeStart_{};
};

} // namespace ubahn::core
