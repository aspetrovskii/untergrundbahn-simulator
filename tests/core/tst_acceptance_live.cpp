#include "api/Functions.h"
#include "core/Engine.h"

#include <QtTest>
#include <optional>
#include <string>
#include <vector>

namespace {

ubahn::api::SimParams quietDay() {
    ubahn::api::SimParams params = ubahn::api::builtinPresets().at(0).params;
    for (ubahn::api::DayProfile& day : params.schedule.days) {
        for (ubahn::api::Window& window : day.windows) {
            window.delayProb = 0;
        }
    }
    params.period.start = ubahn::api::kOpenTod;
    params.period.end = ubahn::api::kCloseTod;
    return params;
}

ubahn::api::ScheduleConfig scaleHeadway(ubahn::api::ScheduleConfig schedule, int32_t headway) {
    for (ubahn::api::DayProfile& day : schedule.days) {
        for (ubahn::api::Window& window : day.windows) {
            window.headway = headway;
            window.delayProb = 0;
        }
    }
    return schedule;
}

ubahn::api::ScheduleConfig twoWindows() {
    ubahn::api::ScheduleConfig schedule;
    schedule.delayMin = 20;
    schedule.delayMax = 60;
    const ubahn::api::Window morning{6 * 3600, 15 * 3600, 180, 50, 0};
    const ubahn::api::Window evening{15 * 3600, ubahn::api::kCloseTod, 240, 40, 0};
    for (ubahn::api::DayProfile& day : schedule.days) {
        day.windows = {morning, evening};
    }
    return schedule;
}

bool isTerminal(ubahn::api::Dir dir, int station, int stations) {
    return (dir == ubahn::api::Dir::I && station == stations - 1) ||
           (dir == ubahn::api::Dir::II && station == 0);
}

bool onTheWay(const ubahn::api::TrainUpdate& train, int stations) {
    if (train.edge == ubahn::api::TrackEdge::Span) {
        return true;
    }
    return train.edge == ubahn::api::TrackEdge::Platform &&
           !isTerminal(train.dir, train.station, stations);
}

struct TerminalHit {
    ubahn::api::SimTime t = 0;
    ubahn::api::Dir dir = ubahn::api::Dir::I;
    int station = 0;
};

std::optional<TerminalHit> firstTerminal(const std::vector<ubahn::api::ViewDelta>& deltas, int id,
                                         int stations, ubahn::api::SimTime after) {
    for (const ubahn::api::ViewDelta& delta : deltas) {
        if (delta.t <= after) {
            continue;
        }
        for (const ubahn::api::TrainUpdate& train : delta.trains) {
            if (train.id == id && train.edge == ubahn::api::TrackEdge::Platform &&
                isTerminal(train.dir, train.station, stations)) {
                return TerminalHit{delta.t, train.dir, train.station};
            }
        }
    }
    return std::nullopt;
}

std::string plansKept(const ubahn::api::ScheduleConfig& next) {
    const ubahn::api::SimParams params = quietDay();
    if (!ubahn::api::validate(params).empty()) {
        return "base params failed validate";
    }
    const int stations = static_cast<int>(params.line.stationNames.size());
    const ubahn::api::SimTime boundary = 12 * 3600;

    ubahn::core::Engine control(params, 1);
    ubahn::core::Engine live(params, 1);
    control.runUntil(boundary, false);
    live.runUntil(boundary, false);
    if (control.internalError() || live.internalError()) {
        return "internal error before the schedule change";
    }

    const std::vector<ubahn::api::ValidationIssue> issues = live.applySchedule(next);
    if (!issues.empty()) {
        return "applySchedule rejected a valid profile: " + issues.front().message;
    }

    const ubahn::api::ViewDelta snapshot = live.snapshotDelta();
    std::vector<int> underway;
    for (const ubahn::api::TrainUpdate& train : snapshot.trains) {
        if (onTheWay(train, stations)) {
            underway.push_back(train.id);
        }
    }
    if (underway.empty()) {
        return "no train was on a trip at noon";
    }

    control.runUntil(boundary + 3600, true);
    live.runUntil(boundary + 3600, true);
    const std::vector<ubahn::api::ViewDelta> controlDeltas = control.takeDeltas();
    const std::vector<ubahn::api::ViewDelta> liveDeltas = live.takeDeltas();

    for (const int id : underway) {
        const std::optional<TerminalHit> left =
            firstTerminal(controlDeltas, id, stations, boundary);
        const std::optional<TerminalHit> right = firstTerminal(liveDeltas, id, stations, boundary);
        if (!left.has_value() || !right.has_value()) {
            return "train " + std::to_string(id) + " did not reach the terminal";
        }
        if (left->t != right->t || left->dir != right->dir || left->station != right->station) {
            return "train " + std::to_string(id) + " terminal arrival moved from " +
                   std::to_string(left->t) + " to " + std::to_string(right->t);
        }
    }

    control.runUntil(params.period.end, false);
    live.runUntil(params.period.end, false);
    if (control.internalError() || live.internalError()) {
        return "internal error after the schedule change";
    }
    return {};
}

} // namespace

class AcceptanceLiveTest : public QObject {
    Q_OBJECT

private slots:
    void smallerHeadwayKeepsAssignedPlans();
    void largerHeadwayKeepsAssignedPlans();
    void differentWindowsKeepAssignedPlans();
};

void AcceptanceLiveTest::smallerHeadwayKeepsAssignedPlans() {
    const std::string error = plansKept(scaleHeadway(quietDay().schedule, 120));
    QVERIFY2(error.empty(), error.c_str());
}

void AcceptanceLiveTest::largerHeadwayKeepsAssignedPlans() {
    const std::string error = plansKept(scaleHeadway(quietDay().schedule, 600));
    QVERIFY2(error.empty(), error.c_str());
}

void AcceptanceLiveTest::differentWindowsKeepAssignedPlans() {
    const std::string error = plansKept(twoWindows());
    QVERIFY2(error.empty(), error.c_str());
}

QTEST_APPLESS_MAIN(AcceptanceLiveTest)
#include "tst_acceptance_live.moc"
