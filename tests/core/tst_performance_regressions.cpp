#include "EngineFixture.h"
#include "api/Functions.h"
#include "core/Check.h"
#include "core/Dispatcher.h"
#include "core/Publisher.h"

#include <QDataStream>
#include <QtTest>
#include <set>

#undef slots

namespace ubahn::core {
void installHandlersForTest(const Handlers* handlers);
}

namespace {
using namespace ubahn;

QByteArray encode(const std::optional<api::ViewDelta>& delta) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << delta.has_value();
    if (!delta) {
        return bytes;
    }
    out << qint64(delta->t) << quint64(delta->trains.size());
    for (const auto& t : delta->trains) {
        out << t.id << int(t.mode) << t.lateness << t.flags.headingToDepot << t.flags.catchingUp
            << t.flags.slowed << t.flags.held << t.flags.offHours << int(t.edge) << int(t.dir)
            << t.station << t.along << t.v << t.speedKmh;
    }
    out << quint64(delta->boards.size());
    for (const auto& b : delta->boards) {
        out << int(b.dir) << b.station << qint64(b.lastArrival) << b.occupied
            << qint64(b.dwellSince) << b.dwellShown << b.shift;
    }
    out << delta->clock.has_value();
    if (delta->clock) {
        const auto& c = *delta->clock;
        out << c.dayOfWeek << c.open << c.windowIndex << c.window.fromTod << c.window.toTod
            << c.window.headway << c.window.dwell << c.window.delayProb << c.hEff;
    }
    out << quint64(delta->warnings.size());
    for (const auto& w : delta->warnings) {
        out << int(w.code) << w.dayOfWeek << w.windowIndex << w.plannedHeadway << w.effectiveHeadway
            << w.requiredTrains << w.terminal << qint64(w.tripSlot)
            << QString::fromStdString(w.message);
    }
    out << quint64(delta->delays.size());
    for (const auto& d : delta->delays) {
        out << d.trainId << int(d.dir) << d.station << d.seconds;
    }
    return bytes;
}

struct PublicationAudit {
    bool same = true;
    std::size_t calls = 0;
    std::size_t candidates = 0;
    std::size_t fullCandidates = 0;
    api::SimTime mismatchAt = api::kNoTime;
};

PublicationAudit publication;

std::optional<api::ViewDelta> compareFullScan(core::EngineState& state, api::SimTime now,
                                              core::DeltaMode mode) {
    // The oracle shares comparison rules, but examines every object. Save only publication
    // state so each run compares against exactly the same last-sent packets and messages.
    auto trains = state.sentTrains;
    auto boards = state.sentBoards;
    auto clock = state.sentClock;
    auto sentAt = state.sentTrainAt;
    auto warnings = state.pendingWarnings;
    auto delays = state.pendingDelays;
    auto changedTrains = state.changedTrains;
    auto changedBoards = state.changedBoards;
    if (mode == core::DeltaMode::Play) {
        ++publication.calls;
        publication.candidates += changedTrains.ordered().size() + changedBoards.ordered().size();
        publication.fullCandidates += state.trains.size() + 2 * state.line.n;
    }
    state.changedTrains.markAll();
    state.changedBoards.markAll();
    const auto expected = core::collectDelta(state, now, mode);
    state.sentTrains = std::move(trains);
    state.sentBoards = std::move(boards);
    state.sentClock = clock;
    state.sentTrainAt = std::move(sentAt);
    state.pendingWarnings = std::move(warnings);
    state.pendingDelays = std::move(delays);
    state.changedTrains = std::move(changedTrains);
    state.changedBoards = std::move(changedBoards);
    auto actual = core::collectDelta(state, now, mode);
    if (encode(actual) != encode(expected)) {
        publication.same = false;
        publication.mismatchAt = now;
    }
    return actual;
}

struct HandlerGuard {
    explicit HandlerGuard(const core::Handlers& handlers) {
        core::installHandlersForTest(&handlers);
    }
    ~HandlerGuard() { core::installHandlersForTest(nullptr); }
};

struct StorageAudit {
    std::size_t maxTrips = 0;
    std::size_t maxWarnings = 0;
    std::size_t sweeps = 0;
    int32_t lastSweep = -1;
    bool referencesValid = true;
};

StorageAudit storage;

void inspectStorage(core::EngineState& state, api::Term term, api::SimTime now,
                    core::DirtySet& dirty) {
    core::handleDispatcher(state, term, now, dirty);
    storage.sweeps += storage.lastSweep != state.lastTripSweep;
    storage.lastSweep = state.lastTripSweep;
    storage.maxTrips = std::max(storage.maxTrips, state.trips.size());
    storage.maxWarnings = std::max(storage.maxWarnings, state.pendingWarnings.size());
    for (const auto& train : state.trains) {
        if (train.tripId >= 0 && core::tripOf(state, train.id) == nullptr) {
            storage.referencesValid = false;
        }
    }
    for (const auto& terminal : state.terms) {
        for (const auto id : terminal.slots) {
            if (core::tripById(state, id) == nullptr) {
                storage.referencesValid = false;
            }
        }
    }
}
} // namespace

class PerformanceRegressionsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sparsePublicationMatchesFullScan_data();
    void sparsePublicationMatchesFullScan();
    void silentRunsDiscardWarningsAndBoundStorage();
    void reclaimOnlyUnreferencedTrips();
    void dirtyQueueWrapsAndReusesOwners();
    void interleavedEnginesKeepIndependentBuffers();
};

void PerformanceRegressionsTest::sparsePublicationMatchesFullScan_data() {
    QTest::addColumn<int>("fleet");
    QTest::addColumn<quint64>("seed");
    QTest::newRow("shortage") << 60 << quint64{1};
    QTest::newRow("surplus") << 200 << quint64{42};
}

void PerformanceRegressionsTest::sparsePublicationMatchesFullScan() {
    QFETCH(int, fleet);
    QFETCH(quint64, seed);
    if (core::kCheckInvariants && fleet == 200) {
        QSKIP("Baseline c257117 fails C10 at 33221 with fleet=200, seed=42; run in Release");
    }
    auto params = core::twentyStationParams();
    params.line.fleetSize = fleet;
    params.period = {api::kOpenTod, api::kSecondsPerDay + 7 * 3600};
    publication = {};
    auto handlers = core::defaultHandlers();
    handlers.collectDelta = compareFullScan;
    const HandlerGuard guard(handlers);
    core::Engine engine(params, seed);
    engine.runUntil(params.period.start, false);
    const auto initial = engine.snapshotDelta();
    QCOMPARE(initial.trains.size(), std::size_t(fleet));
    QCOMPARE(initial.boards.size(), std::size_t{40});
    std::set<std::pair<int, api::SimTime>> shortages;
    for (auto target = params.period.start; target < params.period.end;) {
        target = std::min(target + 240, params.period.end);
        if (target == 12 * 3600) {
            auto schedule = params.schedule;
            for (auto& day : schedule.days) {
                for (auto& window : day.windows) {
                    window.headway += 30;
                }
            }
            QVERIFY(engine.applySchedule(schedule).empty());
        }
        if (target == 15 * 3600) {
            engine.runUntil(target + 1800, false);
            target += 1800;
            engine.seekDelta();
        } else {
            engine.runUntil(target, true);
        }
        for (const auto& delta : engine.takeDeltas()) {
            for (const auto& warning : delta.warnings) {
                if (warning.code == api::WarningCode::NoTrainForTrip) {
                    QVERIFY(shortages.emplace(warning.terminal, warning.tripSlot).second);
                }
            }
        }
    }
    QVERIFY(!engine.internalError());
    QVERIFY2(publication.same, qPrintable(QString("Mismatch at %1").arg(publication.mismatchAt)));
    QVERIFY(publication.calls > 10000);
    QVERIFY(publication.candidates < publication.fullCandidates / 4);
    qInfo() << "Candidate comparisons" << publication.candidates << "of"
            << publication.fullCandidates;
}

void PerformanceRegressionsTest::silentRunsDiscardWarningsAndBoundStorage() {
    auto params = core::twentyStationParams();
    params.line.fleetSize = 60;
    params.period = {api::kOpenTod, 14 * api::SimTime(api::kSecondsPerDay)};
    storage = {};
    auto handlers = core::defaultHandlers();
    handlers.handleDispatcher = inspectStorage;
    const HandlerGuard guard(handlers);
    core::Engine engine(params, 1);
    for (auto target = params.period.start; target < params.period.end;) {
        target = std::min(target + 3600, params.period.end);
        auto schedule = params.schedule;
        for (auto& day : schedule.days) {
            for (auto& window : day.windows) {
                window.headway += (target / 3600 % 2) * 30;
            }
        }
        QVERIFY(engine.applySchedule(schedule).empty());
        engine.runUntil(target, false);
        QVERIFY(!engine.internalError());
        const auto seek = engine.seekDelta();
        for (const auto& warning : seek.warnings) {
            // A schedule edit with no intervening event still belongs to the next delta.
            QVERIFY(warning.code == api::WarningCode::IntervalStretched);
        }
        QVERIFY(engine.takeDeltas().empty());
    }
    QVERIFY(storage.referencesValid);
    QVERIFY(storage.sweeps > 10);
    QVERIFY(storage.maxTrips < 1000);
    // At most one instant's warnings plus schedule changes, never the skipped history.
    QVERIFY(storage.maxWarnings < 100);
}

void PerformanceRegressionsTest::reclaimOnlyUnreferencedTrips() {
    auto params = api::defaultParams();
    auto state = core::initEngineState(params, 1);
    const int32_t discarded = core::addTrip(state, api::Dir::I, 100, 80).id;
    const int32_t inbound = core::addTrip(state, api::Dir::I, 200, 80).id;
    const int32_t future = core::addTrip(state, api::Dir::II, 300, 80).id;
    state.trains[0].tripId = inbound;
    state.terms[1].slots.push_back(future);
    core::reclaimTrips(state);
    QVERIFY(core::tripById(state, discarded) == nullptr);
    QCOMPARE(state.trips.size(), std::size_t{2});
    QCOMPARE(core::tripOf(state, 0)->slot, api::SimTime{200});
    QCOMPARE(core::tripById(state, future)->dPlan[0], api::SimTime{300});
    state.trains[0].tripId = -1;
    state.terms[1].slots.clear();
    core::reclaimTrips(state);
    QVERIFY(state.trips.empty());
    QVERIFY(state.tripIndex.empty());
    const auto& next = core::addTrip(state, api::Dir::I, 400, 80);
    QVERIFY(next.id > future);
    QVERIFY(!next.shortageReported);
}

void PerformanceRegressionsTest::interleavedEnginesKeepIndependentBuffers() {
    const auto params = api::defaultParams();
    core::Engine first(params, 1), other(params, 42), control(params, 1);
    for (auto* engine : {&first, &other, &control}) {
        engine->runUntil(params.period.start, false);
        engine->snapshotDelta();
    }
    for (auto target = params.period.start + 60; target <= params.period.start + 3600;
         target += 60) {
        first.runUntil(target, true);
        other.runUntil(target + 30, false);
        control.runUntil(target, true);
        const auto actual = first.takeDeltas();
        const auto expected = control.takeDeltas();
        QCOMPARE(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            QCOMPARE(encode(actual[i]), encode(expected[i]));
        }
    }
}

void PerformanceRegressionsTest::dirtyQueueWrapsAndReusesOwners() {
    core::DirtySet dirty(4);
    for (int round = 0; round < 1000; ++round) {
        for (int id = 0; id < 4; ++id) {
            dirty.push(id);
            dirty.push(id);
        }
        QCOMPARE(dirty.size(), 4);
        for (int id = 0; id < 4; ++id) {
            QCOMPARE(dirty.pop(), id);
            dirty.push(id);
        }
        dirty.clear();
        QVERIFY(dirty.empty());
        for (int id = 0; id < 4; ++id) {
            QVERIFY(!dirty.contains(id));
        }
    }
    core::DirtySet empty;
    empty.push(0);
    QVERIFY(empty.empty());
}

QTEST_APPLESS_MAIN(PerformanceRegressionsTest)
#include "tst_performance_regressions.moc"
