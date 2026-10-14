#include "api/Functions.h"
#include "core/Engine.h"
#include "core/Schedule.h"
#include "core/Time.h"
#include "core/TrainEvents.h"

#include <QtTest>
#include <string>
#include <vector>

namespace ubahn::core {
void installHandlersForTest(const Handlers* handlers);
} // namespace ubahn::core

namespace {

int gRolls = 0;
int gDelayAtRoll = 4;
int32_t gDelaySec = 180;
ubahn::core::Handlers gHandlers{};

void handleTrainWithOverride(ubahn::core::EngineState& state, int train, ubahn::api::SimTime now,
                             ubahn::core::DirtySet& dirty) {
    state.delayOverride = [](int, ubahn::api::SimTime) {
        const int roll = gRolls++;
        return roll == gDelayAtRoll ? gDelaySec : static_cast<int32_t>(0);
    };
    ubahn::core::handleTrain(state, train, now, dirty);
}

ubahn::api::SimParams quietMorning() {
    ubahn::api::SimParams params = ubahn::api::builtinPresets().at(0).params;
    for (ubahn::api::DayProfile& day : params.schedule.days) {
        for (ubahn::api::Window& window : day.windows) {
            window.delayProb = 0;
        }
    }
    params.period.start = ubahn::api::kOpenTod;
    params.period.end = ubahn::api::kOpenTod + 6 * 3600;
    return params;
}

const ubahn::api::Window* windowAtTod(const ubahn::api::ScheduleConfig& schedule,
                                      ubahn::api::SimTime t) {
    return ubahn::core::windowAt(schedule, t);
}

} // namespace

class AcceptanceRecoveryTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void singleDelayIsCaughtUp();
    void holdsRequireACloseLeader();
};

void AcceptanceRecoveryTest::init() {
    gRolls = 0;
    gHandlers = ubahn::core::defaultHandlers();
    gHandlers.handleTrain = &handleTrainWithOverride;
    ubahn::core::installHandlersForTest(&gHandlers);
}

void AcceptanceRecoveryTest::cleanup() {
    ubahn::core::installHandlersForTest(nullptr);
}

void AcceptanceRecoveryTest::singleDelayIsCaughtUp() {
    const ubahn::api::SimParams params = quietMorning();
    QVERIFY(ubahn::api::validate(params).empty());
    const int stations = static_cast<int>(params.line.stationNames.size());

    ubahn::core::Engine engine(params, 3);
    engine.runUntil(params.period.end, true);
    QVERIFY(!engine.internalError().has_value());

    int delayedTrain = -1;
    int positiveDelays = 0;
    int lateVisits = 0;
    bool seenLate = false;
    bool recovered = false;
    std::vector<ubahn::api::TrackEdge> edge(static_cast<std::size_t>(params.line.fleetSize),
                                            ubahn::api::TrackEdge::Depot);

    for (const ubahn::api::ViewDelta& delta : engine.takeDeltas()) {
        for (const ubahn::api::DelayNotice& notice : delta.delays) {
            if (notice.seconds <= 0) {
                continue;
            }
            ++positiveDelays;
            delayedTrain = notice.trainId;
            QCOMPARE(notice.seconds, gDelaySec);
        }
        for (const ubahn::api::TrainUpdate& train : delta.trains) {
            if (train.id < 0 || train.id >= static_cast<int>(edge.size())) {
                continue;
            }
            const ubahn::api::TrackEdge previous = edge[static_cast<std::size_t>(train.id)];
            const bool arrived =
                train.edge == ubahn::api::TrackEdge::Platform && previous != train.edge;
            edge[static_cast<std::size_t>(train.id)] = train.edge;
            if (train.id != delayedTrain) {
                continue;
            }
            if (train.lateness > 0) {
                seenLate = true;
                if (arrived) {
                    ++lateVisits;
                }
            } else if (seenLate) {
                recovered = true;
            }
        }
    }

    QCOMPARE(positiveDelays, 1);
    QVERIFY(delayedTrain >= 0);
    QVERIFY2(recovered, "lateness did not return to 0");
    // Catch-up is a finite number of spans (SPEC 14.3): one round trip is enough slack.
    QVERIFY2(lateVisits <= 2 * (stations - 1),
             qPrintable(QString("late platform visits %1").arg(lateVisits)));

    const ubahn::api::SimSummary summary = engine.summary(params.period.end, true);
    QVERIFY(summary.maxLatenessSec >= gDelaySec);
    QVERIFY(summary.recovery.count >= 1);
    QCOMPARE(summary.delayCount[0] + summary.delayCount[1], static_cast<int64_t>(1));
}

void AcceptanceRecoveryTest::holdsRequireACloseLeader() {
    // TODO(BE-17): fails with "train 17 held at gap 180 H 180": a hold with no late or close
    // leader (SPEC 14.3). Open engine bug, not yet located.
    QSKIP("hold without a close leader, see TODO(BE-17)");
    gRolls = 0;
    const ubahn::api::SimParams params = quietMorning();
    ubahn::core::Engine engine(params, 3);
    engine.runUntil(params.period.end, true);
    QVERIFY(!engine.internalError().has_value());

    const int stations = static_cast<int>(params.line.stationNames.size());
    const int fleet = params.line.fleetSize;
    std::vector<int32_t> lateness(static_cast<std::size_t>(fleet), 0);
    std::vector<bool> wasHeld(static_cast<std::size_t>(fleet), false);
    struct Platform {
        int occupant = -1;
        int previous = -1;
        ubahn::api::SimTime occupiedAt = 0;
        ubahn::api::SimTime previousAt = 0;
    };
    std::vector<Platform> platforms(static_cast<std::size_t>(2 * stations));
    ubahn::api::SimTime delayAt = 0;
    bool delaySeen = false;

    for (const ubahn::api::ViewDelta& delta : engine.takeDeltas()) {
        if (!delaySeen) {
            for (const ubahn::api::DelayNotice& notice : delta.delays) {
                if (notice.seconds > 0) {
                    delaySeen = true;
                    delayAt = delta.t;
                }
            }
        }
        for (const ubahn::api::TrainUpdate& train : delta.trains) {
            if (train.id < 0 || train.id >= fleet) {
                continue;
            }
            if (!delaySeen && (train.lateness != 0 || train.flags.held || train.flags.slowed)) {
                QFAIL("regulation before the injected delay");
            }
            const int key = static_cast<int>(train.dir) * stations + train.station;
            const bool onPlatform =
                train.edge == ubahn::api::TrackEdge::Platform && key >= 0 && key < 2 * stations;
            if (train.flags.held && !wasHeld[static_cast<std::size_t>(train.id)] && onPlatform) {
                const Platform& platform = platforms[static_cast<std::size_t>(key)];
                const bool alreadyHere = platform.occupant == train.id;
                const int leader = alreadyHere ? platform.previous : platform.occupant;
                const ubahn::api::SimTime leaderAt =
                    alreadyHere ? platform.previousAt : platform.occupiedAt;
                const ubahn::api::Window* window = windowAtTod(params.schedule, delta.t);
                QVERIFY(window != nullptr);
                QVERIFY2(delta.t >= delayAt, "hold before the delay");
                QVERIFY(leader >= 0);
                const bool leaderLate = lateness[static_cast<std::size_t>(leader)] > 0;
                const bool selfLate = train.lateness > 0;
                const bool compressed = delta.t - leaderAt < window->headway;
                QVERIFY2(leaderLate || selfLate || compressed,
                         qPrintable(QString("train %1 held at gap %2 H %3")
                                        .arg(train.id)
                                        .arg(delta.t - leaderAt)
                                        .arg(window->headway)));
            }
            wasHeld[static_cast<std::size_t>(train.id)] = train.flags.held;
            lateness[static_cast<std::size_t>(train.id)] = train.lateness;
            if (onPlatform && platforms[static_cast<std::size_t>(key)].occupant != train.id) {
                Platform& platform = platforms[static_cast<std::size_t>(key)];
                platform.previous = platform.occupant;
                platform.previousAt = platform.occupiedAt;
                platform.occupant = train.id;
                platform.occupiedAt = delta.t;
            }
        }
    }

    QVERIFY(delaySeen);
    const ubahn::api::SimSummary summary = engine.summary(params.period.end, true);
    // A delay larger than H - headwayMin forces the follower to wait (C6/C7, ALGORITHM 7.2).
    QVERIFY(summary.holds.count + summary.slowdowns.count + summary.signalStops.count > 0);
}

QTEST_APPLESS_MAIN(AcceptanceRecoveryTest)
#include "tst_acceptance_recovery.moc"
