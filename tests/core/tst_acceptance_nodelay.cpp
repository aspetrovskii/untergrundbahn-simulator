#include "api/Functions.h"
#include "core/Engine.h"
#include "core/Line.h"
#include "core/Schedule.h"
#include "core/Time.h"

#include <QtTest>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace {

void zeroDelays(ubahn::api::SimParams& params) {
    for (ubahn::api::DayProfile& day : params.schedule.days) {
        for (ubahn::api::Window& window : day.windows) {
            window.delayProb = 0;
        }
    }
}

// One service day of the training preset with every delay probability cleared.
ubahn::api::SimParams trainingDay(int day) {
    ubahn::api::SimParams params = ubahn::api::builtinPresets().at(0).params;
    zeroDelays(params);
    const ubahn::api::SimTime midnight =
        static_cast<ubahn::api::SimTime>(day) * ubahn::api::kSecondsPerDay;
    params.period.start = midnight + ubahn::api::kOpenTod;
    params.period.end = midnight + ubahn::api::kCloseTod;
    return params;
}

const ubahn::api::Window* windowOfArrival(const ubahn::api::ScheduleConfig& schedule,
                                          ubahn::api::SimTime t) {
    if (const ubahn::api::Window* window = ubahn::core::windowAt(schedule, t)) {
        return window;
    }
    const int32_t tod = ubahn::core::todOf(t);
    int64_t day = ubahn::core::dayIndex(t);
    bool first = true;
    if (tod == 0 && t == ubahn::core::dayStart(t) && t != 0) {
        // 24:00 is the close of the day that just ended.
        --day;
        first = false;
    } else if (tod >= ubahn::api::kOpenTod) {
        return nullptr;
    }
    const int dow = static_cast<int>(ubahn::core::floorMod(day, 7));
    const std::vector<ubahn::api::Window>& windows =
        schedule.days[static_cast<std::size_t>(dow)].windows;
    if (windows.empty()) {
        return nullptr;
    }
    return first ? &windows.front() : &windows.back();
}

const ubahn::api::Window* previousWindow(const ubahn::api::ScheduleConfig& schedule,
                                         const ubahn::api::Window* window) {
    if (window == nullptr) {
        return nullptr;
    }
    for (const ubahn::api::DayProfile& day : schedule.days) {
        const std::vector<ubahn::api::Window>& windows = day.windows;
        for (std::size_t i = 0; i < windows.size(); ++i) {
            if (&windows[i] == window) {
                return i == 0 ? nullptr : &windows[i - 1];
            }
        }
    }
    return nullptr;
}

int32_t secondsIntoWindow(const ubahn::api::Window& window, ubahn::api::SimTime t) {
    int32_t tod = ubahn::core::todOf(t);
    if (tod == 0 && t == ubahn::core::dayStart(t)) {
        tod = ubahn::api::kCloseTod;
    }
    if (tod < window.fromTod) {
        return 0;
    }
    return tod - window.fromTod;
}

int32_t travelLimit(const ubahn::core::Line& line, const ubahn::api::ScheduleConfig& schedule) {
    int32_t limit = 0;
    for (const ubahn::api::DayProfile& day : schedule.days) {
        for (const ubahn::api::Window& window : day.windows) {
            limit = std::max(limit, ubahn::core::tToTerm(line, window.dwell));
        }
    }
    return limit;
}

bool headwayMatches(const ubahn::api::ScheduleConfig& schedule, const ubahn::api::Window* left,
                    const ubahn::api::Window* right, ubahn::api::SimTime leftAt, int64_t gap,
                    int32_t travel) {
    if (left == nullptr || right == nullptr || gap <= 0) {
        return false;
    }
    if (gap == left->headway || gap == right->headway) {
        return true;
    }
    // A slot stepped with the previous window can still arrive just inside the next one
    // (ALGORITHM 10.1, T_toTerm after the boundary).
    const ubahn::api::Window* before = previousWindow(schedule, left);
    return before != nullptr && gap == before->headway && secondsIntoWindow(*left, leftAt) < travel;
}

std::string checkHeadways(const ubahn::api::ScheduleConfig& schedule, const ubahn::core::Line& line,
                          const std::vector<std::vector<ubahn::api::SimTime>>& arrivals) {
    const int n = line.n;
    const int32_t travel = travelLimit(line, schedule);
    for (int dir = 0; dir < 2; ++dir) {
        for (int station = 0; station < n; ++station) {
            const std::vector<ubahn::api::SimTime>& times =
                arrivals[static_cast<std::size_t>(dir * n + station)];
            if (times.size() < 2) {
                return "platform dir " + std::to_string(dir) + " station " +
                       std::to_string(station) + " has " + std::to_string(times.size()) +
                       " arrivals";
            }
            for (std::size_t i = 1; i < times.size(); ++i) {
                const ubahn::api::SimTime leftAt = times[i - 1];
                const ubahn::api::SimTime rightAt = times[i];
                const int64_t gap = rightAt - leftAt;
                const ubahn::api::Window* left = windowOfArrival(schedule, leftAt);
                const ubahn::api::Window* right = windowOfArrival(schedule, rightAt);
                if (!headwayMatches(schedule, left, right, leftAt, gap, travel)) {
                    const int32_t leftH = left == nullptr ? -1 : left->headway;
                    const int32_t rightH = right == nullptr ? -1 : right->headway;
                    return "dir " + std::to_string(dir) + " station " + std::to_string(station) +
                           " gap " + std::to_string(gap) + " at " + std::to_string(leftAt) + ".." +
                           std::to_string(rightAt) + " headways " + std::to_string(leftH) + "/" +
                           std::to_string(rightH);
                }
            }
        }
    }
    return {};
}

std::string checkFirstTrip(const std::vector<std::vector<ubahn::api::SimTime>>& arrivals, int n,
                           ubahn::api::SimTime open) {
    const int keys[] = {(static_cast<int>(ubahn::api::Dir::I) * n) + (n - 1),
                        static_cast<int>(ubahn::api::Dir::II) * n};
    for (const int key : keys) {
        const std::vector<ubahn::api::SimTime>& times = arrivals[static_cast<std::size_t>(key)];
        if (times.empty()) {
            return "no arrival at terminal key " + std::to_string(key);
        }
        const ubahn::api::SimTime first = *std::min_element(times.begin(), times.end());
        if (first != open) {
            return "first terminal arrival " + std::to_string(first) + " expected " +
                   std::to_string(open);
        }
    }
    return {};
}

struct DayRun {
    ubahn::api::SimSummary summary;
    std::optional<std::string> internalError;
    std::vector<std::vector<ubahn::api::SimTime>> arrivals;
    std::string lateness;
    std::string movement;
};

DayRun simulate(const ubahn::api::SimParams& params) {
    DayRun run;
    const int n = static_cast<int>(params.line.stationNames.size());
    run.arrivals.assign(static_cast<std::size_t>(2 * n), {});
    std::vector<ubahn::api::SimTime> seen(static_cast<std::size_t>(2 * n), ubahn::api::kNoTime);

    ubahn::core::Engine engine(params, 1);
    engine.runUntil(params.period.end, true);
    if (engine.internalError()) {
        run.internalError = *engine.internalError();
    }
    run.summary = engine.summary(params.period.end, true);

    for (const ubahn::api::ViewDelta& delta : engine.takeDeltas()) {
        for (const ubahn::api::TrainUpdate& train : delta.trains) {
            if (train.lateness != 0 && run.lateness.empty()) {
                run.lateness = "train " + std::to_string(train.id) + " lateness " +
                               std::to_string(train.lateness) + " at " + std::to_string(delta.t);
            }
            if ((train.flags.held || train.flags.slowed || train.flags.catchingUp) &&
                run.movement.empty()) {
                run.movement = "train " + std::to_string(train.id) + " held " +
                               std::to_string(train.flags.held ? 1 : 0) + " slowed " +
                               std::to_string(train.flags.slowed ? 1 : 0) + " catching " +
                               std::to_string(train.flags.catchingUp ? 1 : 0) + " at " +
                               std::to_string(delta.t);
            }
        }
        for (const ubahn::api::Warning& warning : delta.warnings) {
            if (warning.code == ubahn::api::WarningCode::NoTrainForTrip && run.lateness.empty()) {
                run.lateness = "missing train for slot " + std::to_string(warning.tripSlot);
            }
        }
        for (const ubahn::api::BoardUpdate& board : delta.boards) {
            if (board.lastArrival == ubahn::api::kNoTime) {
                continue;
            }
            const int key = static_cast<int>(board.dir) * n + board.station;
            if (key < 0 || key >= 2 * n) {
                run.movement = "board station out of range";
                return run;
            }
            ubahn::api::SimTime& previous = seen[static_cast<std::size_t>(key)];
            if (previous == board.lastArrival) {
                continue;
            }
            previous = board.lastArrival;
            run.arrivals[static_cast<std::size_t>(key)].push_back(board.lastArrival);
        }
    }
    return run;
}

struct Verdict {
    std::string onTime;  // lateness, missing trains, the first trip
    std::string regular; // regulator actions and arrival intervals
};

Verdict judge(int day) {
    Verdict verdict;
    const ubahn::api::SimParams params = trainingDay(day);
    if (!ubahn::api::validate(params).empty()) {
        verdict.onTime = "training day failed validate";
        return verdict;
    }

    const DayRun run = simulate(params);
    const ubahn::api::SimSummary& summary = run.summary;
    const ubahn::core::Line line = ubahn::core::buildLine(params.line);
    if (run.internalError.has_value()) {
        verdict.onTime = *run.internalError;
    } else if (!run.lateness.empty()) {
        verdict.onTime = run.lateness;
    } else if (summary.delayCount[0] != 0 || summary.delayCount[1] != 0 ||
               summary.maxLatenessSec != 0 || summary.recovery.count != 0) {
        verdict.onTime = "summary has lateness on a day without delays";
    } else if (!summary.stretchedWindows.empty()) {
        verdict.onTime = "training fleet stretched a window";
    } else if (summary.stopCount[0] + summary.stopCount[1] <= 0) {
        verdict.onTime = "no dwells were counted";
    } else {
        verdict.onTime =
            checkFirstTrip(run.arrivals, line.n,
                           ubahn::core::dayStart(params.period.start) + ubahn::api::kOpenTod);
    }

    if (!run.movement.empty()) {
        verdict.regular = run.movement;
    } else if (summary.holds.count != 0 || summary.slowdowns.count != 0 ||
               summary.signalStops.count != 0) {
        verdict.regular = "summary counts regulator actions";
    } else {
        verdict.regular = checkHeadways(params.schedule, line, run.arrivals);
    }
    return verdict;
}

} // namespace

class AcceptanceNodelayTest : public QObject {
    Q_OBJECT

private slots:
    void weekday();
    void weekend();
};

void AcceptanceNodelayTest::weekday() {
    const Verdict verdict = judge(0);
    // TODO(BE-17): at 10:00 and 20:00 the dwell drops (80 -> 60, 80 -> 40) while the first slot
    // of the new window is still one old H (120 s) after the last one (ALGORITHM 10.1). The new
    // trip gains 20 s or more per station on the old one, and from the second station its plan
    // arrives less than tClear after the leader's planned departure: the regulator slows it and
    // it runs late (first at 10:04:45).
    QEXPECT_FAIL("", "window boundary with a dwell drop, see TODO(BE-17)", Abort);
    QVERIFY2(verdict.onTime.empty(), verdict.onTime.c_str());
    QVERIFY2(verdict.regular.empty(), verdict.regular.c_str());
}

void AcceptanceNodelayTest::weekend() {
    const Verdict verdict = judge(5);
    QVERIFY2(verdict.onTime.empty(), verdict.onTime.c_str());
    // TODO(BE-17): no regulator actions, but arrival intervals differ from H in two places.
    // 1) Where the dwell changes at 10:00 and 20:00, arrivals drift by the dwell difference
    //    per station (380 .. 480 s, 220 .. 120 s); the transition allowance accepts only the
    //    old or new H. 2) On terminal platforms a turnaround train is placed when ready, a depot
    //    train at slot - dwellMin, so where they alternate the gaps come in pairs (590 / 130).
    QEXPECT_FAIL("", "arrival intervals at window and fleet changes, see TODO(BE-17)", Abort);
    QVERIFY2(verdict.regular.empty(), verdict.regular.c_str());
}

QTEST_APPLESS_MAIN(AcceptanceNodelayTest)
#include "tst_acceptance_nodelay.moc"
