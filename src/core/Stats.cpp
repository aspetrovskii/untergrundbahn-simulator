#include "core/Stats.h"

#include "core/Time.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace ubahn::core {
namespace {

// Closed period, plus service hours [06:00, 24:00). 24:00 is the next midnight
// (tod 0), so it is outside the window. SPEC 6.2, SPEC 11, ALGORITHM 12.
// Events at period.end are included: runUntil processes times <= T (ALGORITHM 6.1).
bool counts(const api::SimPeriod& period, api::SimTime now) {
    if (now < period.start || now > period.end) {
        return false;
    }
    const int32_t tod = todOf(now);
    return tod >= api::kOpenTod && tod < api::kCloseTod;
}

void addSample(int64_t& count, int64_t& sumSec, int32_t& maxSec, int64_t sample) {
    ++count;
    sumSec += sample;
    if (sample > maxSec) {
        maxSec = sample > std::numeric_limits<int32_t>::max() ? std::numeric_limits<int32_t>::max()
                                                              : static_cast<int32_t>(sample);
    }
}

api::HeadwayStat headwayStat(int64_t count, int64_t sumSec, int32_t maxSec) {
    api::HeadwayStat stat;
    stat.count = count;
    if (count > 0) {
        stat.meanSec = static_cast<double>(sumSec) / static_cast<double>(count);
        stat.maxSec = maxSec;
    }
    return stat;
}

api::EpisodeStat episodeStat(int64_t count, int64_t sumSec, int32_t maxSec) {
    api::EpisodeStat stat;
    stat.count = count;
    if (count > 0) {
        stat.meanSec = static_cast<double>(sumSec) / static_cast<double>(count);
        stat.maxSec = maxSec;
    }
    return stat;
}

api::AddedTimeStat addedTime(int64_t count, int64_t totalSec) {
    api::AddedTimeStat stat;
    stat.count = count;
    stat.totalSec = totalSec;
    return stat;
}

bool stationRow(int dir, int station, int rowCount) {
    return dir >= 0 && dir < 2 && station >= 0 && station < rowCount;
}

} // namespace

Stats::Stats(api::SimPeriod period, int nStations) : period_(period) {
    const auto stations = static_cast<std::size_t>(nStations > 0 ? nStations : 0);
    for (auto& row : headway_) {
        row.assign(stations, {});
    }
    for (auto& row : lastArrival_) {
        row.assign(stations, api::kNoTime);
    }
}

void Stats::onStop(api::Dir dir, int, api::SimTime now, int32_t drawnDelay) {
    if (!counts(period_, now)) {
        return;
    }
    const int index = static_cast<int>(dir);
    if (index < 0 || index > 1) {
        return;
    }
    // Every dwell roll in the window is a stop. Only a positive draw is a delay
    // (SPEC 11, ALGORITHM 12), so the share and the mean use one set of rolls.
    ++stopCount_[static_cast<std::size_t>(index)];
    if (drawnDelay > 0) {
        ++delayCount_[static_cast<std::size_t>(index)];
        delaySum_[static_cast<std::size_t>(index)] += drawnDelay;
    }
}

void Stats::onArrival(api::Dir dir, int station, api::SimTime now) {
    if (!counts(period_, now)) {
        return;
    }
    const int index = static_cast<int>(dir);
    if (index < 0 || index > 1) {
        return;
    }
    auto& previousRow = lastArrival_[static_cast<std::size_t>(index)];
    if (!stationRow(index, station, static_cast<int>(previousRow.size()))) {
        return;
    }
    api::SimTime& previous = previousRow[static_cast<std::size_t>(station)];
    // Decision R15: one service day is this calendar day from 06:00 to 24:00.
    // The first arrival after opening does not emit an interval, so the gap from
    // the previous evening (or from before the period) is not a sample (SPEC 11).
    if (previous != api::kNoTime && dayIndex(previous) == dayIndex(now) && now >= previous) {
        Accumulator& acc =
            headway_[static_cast<std::size_t>(index)][static_cast<std::size_t>(station)];
        addSample(acc.count, acc.sumSec, acc.maxSec, now - previous);
    }
    if (previous == api::kNoTime || now >= previous) {
        previous = now;
    }
}

void Stats::onLateness(int train, int32_t lateness, api::SimTime now) {
    if (!counts(period_, now) || train < 0) {
        return;
    }
    if (lateness > maxLateness_) {
        maxLateness_ = lateness;
    }
    // An episode opens on the first lateness > 0. A later zero here does not
    // close it: only a departure with lateDep == 0 does (ALGORITHM 12).
    if (lateness <= 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(train);
    if (episodeStart_.size() <= index) {
        episodeStart_.resize(index + 1, api::kNoTime);
    }
    if (episodeStart_[index] == api::kNoTime) {
        episodeStart_[index] = now;
    }
}

void Stats::onDeparture(int train, int32_t lateDep, api::SimTime now) {
    if (!counts(period_, now) || train < 0 || lateDep != 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(train);
    if (index >= episodeStart_.size()) {
        return;
    }
    const api::SimTime start = episodeStart_[index];
    if (start == api::kNoTime || now < start) {
        return;
    }
    episodeStart_[index] = api::kNoTime;
    Accumulator& acc = recovery_;
    addSample(acc.count, acc.sumSec, acc.maxSec, now - start);
}

void Stats::onHold(int32_t sec, api::SimTime now) {
    if (!counts(period_, now) || sec <= 0) {
        return;
    }
    addSample(holds_.count, holds_.sumSec, holds_.maxSec, sec);
}

void Stats::onSlowdown(int32_t sec, api::SimTime now) {
    if (!counts(period_, now) || sec <= 0) {
        return;
    }
    addSample(slowdowns_.count, slowdowns_.sumSec, slowdowns_.maxSec, sec);
}

void Stats::onSignalStop(int32_t sec, api::SimTime now) {
    if (!counts(period_, now) || sec <= 0) {
        return;
    }
    addSample(signalStops_.count, signalStops_.sumSec, signalStops_.maxSec, sec);
}

void Stats::setStretchedWindows(std::vector<api::StretchInfo> windows) {
    stretched_ = std::move(windows);
}

api::SimSummary Stats::fold(api::SimTime stoppedAt, bool completed) const {
    api::SimSummary summary;
    summary.period = period_;
    summary.stoppedAt = stoppedAt;
    summary.completed = completed;
    summary.delayCount = delayCount_;
    summary.stopCount = stopCount_;

    const int64_t delayTotal = delayCount_[0] + delayCount_[1];
    const int64_t stopTotal = stopCount_[0] + stopCount_[1];
    const int64_t delaySum = delaySum_[0] + delaySum_[1];
    if (delayTotal > 0) {
        summary.delayMeanSec = static_cast<double>(delaySum) / static_cast<double>(delayTotal);
    }
    if (stopTotal > 0) {
        summary.delayShare = static_cast<double>(delayTotal) / static_cast<double>(stopTotal);
    }

    int64_t totalCount = 0;
    int64_t totalSum = 0;
    int32_t totalMax = 0;
    for (int dir = 0; dir < 2; ++dir) {
        int64_t dirCount = 0;
        int64_t dirSum = 0;
        int32_t dirMax = 0;
        const auto& row = headway_[static_cast<std::size_t>(dir)];
        auto& out = summary.headwayByStation[static_cast<std::size_t>(dir)];
        out.resize(row.size());
        for (std::size_t station = 0; station < row.size(); ++station) {
            const Accumulator& acc = row[station];
            out[station] = headwayStat(acc.count, acc.sumSec, acc.maxSec);
            dirCount += acc.count;
            dirSum += acc.sumSec;
            if (acc.count > 0 && acc.maxSec > dirMax) {
                dirMax = acc.maxSec;
            }
        }
        summary.headwayByDir[static_cast<std::size_t>(dir)] = headwayStat(dirCount, dirSum, dirMax);
        totalCount += dirCount;
        totalSum += dirSum;
        if (dirMax > totalMax) {
            totalMax = dirMax;
        }
    }
    summary.headwayTotal = headwayStat(totalCount, totalSum, totalMax);

    summary.maxLatenessSec = maxLateness_;
    summary.recovery = episodeStat(recovery_.count, recovery_.sumSec, recovery_.maxSec);
    summary.holds = addedTime(holds_.count, holds_.sumSec);
    summary.slowdowns = addedTime(slowdowns_.count, slowdowns_.sumSec);
    summary.signalStops = addedTime(signalStops_.count, signalStops_.sumSec);
    summary.stretchedWindows = stretched_;
    return summary;
}

} // namespace ubahn::core
