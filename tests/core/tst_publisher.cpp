#include "EngineFixture.h"
#include "core/Publisher.h"
#include "core/Schedule.h"
#include "core/Time.h"

#include <QtTest>
#include <cmath>
#include <optional>

namespace {

constexpr int kFleet = 4;

ubahn::core::EngineState trainingState() {
    const ubahn::api::SimParams params = ubahn::core::makeParams(
        ubahn::core::trainingLineConfig(), ubahn::core::constantSchedule(120, 80, 0), kFleet);
    return ubahn::core::makeState(params, 1);
}

ubahn::api::ScheduleConfig twoWindowSchedule() {
    ubahn::api::Window morning;
    morning.fromTod = ubahn::api::kOpenTod;
    morning.toTod = 12 * 3600;
    morning.headway = 120;
    morning.dwell = 80;
    morning.delayProb = 0;

    ubahn::api::Window evening = morning;
    evening.fromTod = 12 * 3600;
    evening.toTod = ubahn::api::kCloseTod;
    evening.headway = 180;

    ubahn::api::ScheduleConfig cfg;
    cfg.delayMin = 20;
    cfg.delayMax = 60;
    for (ubahn::api::DayProfile& day : cfg.days) {
        day.windows.push_back(morning);
        day.windows.push_back(evening);
    }
    return cfg;
}

ubahn::core::EngineState clockState() {
    const ubahn::api::SimParams params =
        ubahn::core::makeParams(ubahn::core::trainingLineConfig(), twoWindowSchedule(), kFleet);
    return ubahn::core::makeState(params, 1);
}

const ubahn::api::TrainUpdate* findTrain(const ubahn::api::ViewDelta& delta, int id) {
    for (const ubahn::api::TrainUpdate& train : delta.trains) {
        if (train.id == id) {
            return &train;
        }
    }
    return nullptr;
}

const ubahn::api::BoardUpdate* findBoard(const ubahn::api::ViewDelta& delta, ubahn::api::Dir dir,
                                         int station) {
    for (const ubahn::api::BoardUpdate& board : delta.boards) {
        if (board.dir == dir && board.station == station) {
            return &board;
        }
    }
    return nullptr;
}

void expectStill(const ubahn::api::TrainUpdate& view, ubahn::api::TrackEdge edge, int station,
                 double along) {
    QCOMPARE(static_cast<int>(view.edge), static_cast<int>(edge));
    QCOMPARE(view.station, station);
    QCOMPARE(view.along, along);
    QCOMPARE(view.v, 0.0);
    QCOMPARE(view.speedKmh, 0.0);
}

} // namespace

class PublisherTest : public QObject {
    Q_OBJECT

private slots:
    void modePriority_data();
    void modePriority();
    void stateGeometry();
    void speedOnSpan();
    void spanStationOnPathII();
    void snapshotPublishesEverything();
    void repeatedPlayIsEmpty();
    void modeChangeEntersPlay();
    void seekCatchesAlong();
    void playIgnoresAlongOnly();
    void clockTracksDayWindowOpen();
    void messagesAreTakenAndCleared();
    void boardChangeEntersPlay();
    void depotMoveChangesStation();
};

void PublisherTest::modePriority_data() {
    QTest::addColumn<int>("state");
    QTest::addColumn<bool>("held");
    QTest::addColumn<bool>("offHours");
    QTest::addColumn<bool>("slowed");
    QTest::addColumn<bool>("catchingUp");
    QTest::addColumn<int>("lateness");
    QTest::addColumn<int>("mode");

    const int stopped = static_cast<int>(ubahn::api::TrainMode::Stopped);
    const int service = static_cast<int>(ubahn::api::TrainMode::Service);
    const int slowed = static_cast<int>(ubahn::api::TrainMode::Slowed);
    const int catchUp = static_cast<int>(ubahn::api::TrainMode::CatchUp);
    const int onTime = static_cast<int>(ubahn::api::TrainMode::OnTime);

    const int atSignal = static_cast<int>(ubahn::core::TrainState::StoppedAtSignal);
    const int dwelling = static_cast<int>(ubahn::core::TrainState::Dwelling);
    const int waiting = static_cast<int>(ubahn::core::TrainState::WaitingSlot);
    const int running = static_cast<int>(ubahn::core::TrainState::Running);
    const int alighting = static_cast<int>(ubahn::core::TrainState::AlightingTerminal);

    QTest::newRow("signal") << atSignal << false << false << false << false << 0 << stopped;
    QTest::newRow("held") << dwelling << true << false << false << false << 0 << stopped;
    QTest::newRow("held beats the rest")
        << waiting << true << true << true << true << 30 << stopped;
    QTest::newRow("depot") << static_cast<int>(ubahn::core::TrainState::InDepot) << false << false
                           << false << false << 0 << service;
    QTest::newRow("leaving depot") << static_cast<int>(ubahn::core::TrainState::LeavingDepot)
                                   << false << false << false << false << 0 << service;
    QTest::newRow("to dead end") << static_cast<int>(ubahn::core::TrainState::ToDeadEnd) << false
                                 << false << false << false << 0 << service;
    QTest::newRow("cab change") << static_cast<int>(ubahn::core::TrainState::CabChange) << false
                                << false << false << false << 0 << service;
    QTest::newRow("dead end wait") << static_cast<int>(ubahn::core::TrainState::InDeadEndWaiting)
                                   << false << false << false << false << 0 << service;
    QTest::newRow("from dead end") << static_cast<int>(ubahn::core::TrainState::FromDeadEnd)
                                   << false << false << false << false << 0 << service;
    QTest::newRow("to depot") << static_cast<int>(ubahn::core::TrainState::ToDepot) << false
                              << false << false << false << 0 << service;
    QTest::newRow("off hours beats slow and catch-up")
        << running << false << true << true << true << 10 << service;
    QTest::newRow("slowed beats catch-up")
        << running << false << false << true << true << 10 << slowed;
    QTest::newRow("catch-up flag") << running << false << false << false << true << 0 << catchUp;
    QTest::newRow("lateness without catch-up")
        << waiting << false << false << false << false << 1 << catchUp;
    QTest::newRow("alighting late")
        << alighting << false << false << false << false << 5 << catchUp;
    QTest::newRow("on time") << running << false << false << false << false << 0 << onTime;
    QTest::newRow("dwell on time") << dwelling << false << false << false << false << 0 << onTime;
}

void PublisherTest::modePriority() {
    QFETCH(int, state);
    QFETCH(bool, held);
    QFETCH(bool, offHours);
    QFETCH(bool, slowed);
    QFETCH(bool, catchingUp);
    QFETCH(int, lateness);
    QFETCH(int, mode);

    ubahn::core::Train train;
    train.state = static_cast<ubahn::core::TrainState>(state);
    train.flags.held = held;
    train.flags.offHours = offHours;
    train.flags.slowed = slowed;
    train.flags.catchingUp = catchingUp;
    train.lateness = lateness;
    QCOMPARE(static_cast<int>(ubahn::core::modeOf(train)), mode);
}

void PublisherTest::stateGeometry() {
    ubahn::core::EngineState state = trainingState();
    const int n = state.line.n;
    const ubahn::api::SimTime now = 10 * 3600;
    ubahn::core::Train& train = state.trains[0];

    // InDepot follows depotAt, not dir. A mismatched dir must not move the dot.
    train.dir = ubahn::api::Dir::II;
    train.depotAt = ubahn::api::Term::A;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Depot, 0, 0);

    train.depotAt = ubahn::api::Term::B;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Depot, n - 1, 0);

    train.dir = ubahn::api::Dir::I;
    train.pos = 3;
    train.state = ubahn::core::TrainState::Dwelling;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Platform, 3, 0);

    train.pos = 0;
    train.state = ubahn::core::TrainState::WaitingSlot;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Platform, 0, 0);

    train.pos = n - 1;
    train.state = ubahn::core::TrainState::AlightingTerminal;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Platform, n - 1, 0);

    train.dir = ubahn::api::Dir::II;
    train.pos = 2;
    train.state = ubahn::core::TrainState::Dwelling;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::Platform,
                ubahn::core::stationOf(state.line, ubahn::api::Dir::II, 2), 0);

    const int32_t toDeadEnd = state.line.cfg.toDeadEnd;
    train.state = ubahn::core::TrainState::ToDeadEnd;
    train.dir = ubahn::api::Dir::I;
    train.pos = 0;
    train.next = {};
    ubahn::api::TrainUpdate deadEnd = ubahn::core::trainView(state, 0, now);
    QCOMPARE(static_cast<int>(deadEnd.edge), static_cast<int>(ubahn::api::TrackEdge::ToDeadEnd));
    QCOMPARE(deadEnd.station, n - 1);
    QCOMPARE(deadEnd.along, 0.0);
    QCOMPARE(deadEnd.v, 1.0 / static_cast<double>(toDeadEnd));
    QCOMPARE(deadEnd.speedKmh, 0.0);

    train.next.valid = true;
    train.next.time = now + toDeadEnd;
    QCOMPARE(ubahn::core::trainView(state, 0, now).along, 0.0);
    const ubahn::api::SimTime third = now + toDeadEnd / 3;
    QCOMPARE(ubahn::core::trainView(state, 0, third).along,
             static_cast<double>(toDeadEnd / 3) / static_cast<double>(toDeadEnd));
    QCOMPARE(ubahn::core::trainView(state, 0, third).v, 1.0 / static_cast<double>(toDeadEnd));

    train.state = ubahn::core::TrainState::CabChange;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::ToDeadEnd, n - 1, 1);
    train.state = ubahn::core::TrainState::InDeadEndWaiting;
    expectStill(ubahn::core::trainView(state, 0, now), ubahn::api::TrackEdge::ToDeadEnd, n - 1, 1);

    const int32_t fromDeadEnd = state.line.cfg.fromDeadEnd;
    train.state = ubahn::core::TrainState::FromDeadEnd;
    train.dir = ubahn::api::Dir::II;
    train.pos = n - 1;
    train.next = {};
    ubahn::api::TrainUpdate fromDead = ubahn::core::trainView(state, 0, now);
    QCOMPARE(static_cast<int>(fromDead.edge), static_cast<int>(ubahn::api::TrackEdge::FromDeadEnd));
    QCOMPARE(fromDead.station, ubahn::core::stationOf(state.line, ubahn::api::Dir::II, 0));
    QCOMPARE(fromDead.station, n - 1);
    QCOMPARE(fromDead.along, 0.0);
    QCOMPARE(fromDead.v, 1.0 / static_cast<double>(fromDeadEnd));
    QCOMPARE(fromDead.speedKmh, 0.0);

    const int32_t depotIn = state.line.cfg.depotIn;
    train.state = ubahn::core::TrainState::ToDepot;
    train.dir = ubahn::api::Dir::I;
    train.next = {};
    ubahn::api::TrainUpdate toDepot = ubahn::core::trainView(state, 0, now);
    QCOMPARE(static_cast<int>(toDepot.edge), static_cast<int>(ubahn::api::TrackEdge::ToDepot));
    QCOMPARE(toDepot.station, n - 1);
    QCOMPARE(toDepot.along, 0.0);
    QCOMPARE(toDepot.v, 1.0 / static_cast<double>(depotIn));
    QCOMPARE(toDepot.speedKmh, 0.0);

    const int32_t depotOut = state.line.cfg.depotOut;
    train.state = ubahn::core::TrainState::LeavingDepot;
    train.dir = ubahn::api::Dir::I;
    train.next = {};
    ubahn::api::TrainUpdate leaving = ubahn::core::trainView(state, 0, now);
    QCOMPARE(static_cast<int>(leaving.edge), static_cast<int>(ubahn::api::TrackEdge::FromDepot));
    QCOMPARE(leaving.station, 0);
    QCOMPARE(leaving.along, 0.0);
    QCOMPARE(leaving.v, 1.0 / static_cast<double>(depotOut));
    QCOMPARE(leaving.speedKmh, 0.0);

    train.dir = ubahn::api::Dir::I;
    train.pos = 2;
    train.state = ubahn::core::TrainState::StoppedAtSignal;
    const ubahn::core::Span& span = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 2);
    const double stopAlong = (span.lengthM - state.line.cfg.signalStopOffsetM) / span.lengthM;
    ubahn::api::TrainUpdate signal = ubahn::core::trainView(state, 0, now);
    QCOMPARE(static_cast<int>(signal.edge), static_cast<int>(ubahn::api::TrackEdge::Span));
    QCOMPARE(signal.station, 2);
    QCOMPARE(signal.along, stopAlong);
    QCOMPARE(signal.v, 0.0);
    QCOMPARE(signal.speedKmh, 0.0);
}

void PublisherTest::speedOnSpan() {
    ubahn::core::EngineState state = trainingState();
    constexpr ubahn::api::SimTime depart = 25231;
    constexpr ubahn::api::SimTime run = 130;
    ubahn::core::placeOnSpan(state, 0, ubahn::api::Dir::I, 2, depart, depart + run);

    const ubahn::api::TrainUpdate start = ubahn::core::trainView(state, 0, depart);
    QCOMPARE(static_cast<int>(start.edge), static_cast<int>(ubahn::api::TrackEdge::Span));
    QCOMPARE(start.station, 2);
    QCOMPARE(start.along, 0.0);
    QCOMPARE(start.v, 1.0 / static_cast<double>(run));
    QCOMPARE(start.speedKmh, 1700.0 / static_cast<double>(run) * 3.6);
    QCOMPARE(start.mode, ubahn::api::TrainMode::OnTime);

    const ubahn::api::TrainUpdate middle = ubahn::core::trainView(state, 0, depart + run / 2);
    QCOMPARE(middle.along, 0.5);
    QCOMPARE(middle.v, start.v);
    QCOMPARE(middle.speedKmh, start.speedKmh);
}

void PublisherTest::spanStationOnPathII() {
    ubahn::core::EngineState state = trainingState();
    // Path II span between stations 3 and 2. The packet names the greater end.
    constexpr int pos = 4;
    ubahn::core::placeOnSpan(state, 0, ubahn::api::Dir::II, pos, 1000, 1130);
    const ubahn::api::TrainUpdate view = ubahn::core::trainView(state, 0, 1000);
    QCOMPARE(static_cast<int>(view.edge), static_cast<int>(ubahn::api::TrackEdge::Span));
    QCOMPARE(view.station, 3);
    QCOMPARE(view.station, ubahn::core::stationOf(state.line, ubahn::api::Dir::II, pos));
    QVERIFY(view.station > ubahn::core::stationOf(state.line, ubahn::api::Dir::II, pos + 1));
    QCOMPARE(ubahn::core::spanOf(state.line, ubahn::api::Dir::II, pos).lengthM, 1700.0);
}

void PublisherTest::snapshotPublishesEverything() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 10 * 3600;
    ubahn::core::placeOnSpan(state, 1, ubahn::api::Dir::I, 2, t, t + 130);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::II, 1);

    const std::optional<ubahn::api::ViewDelta> delta =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Snapshot);
    QVERIFY(delta.has_value());
    QCOMPARE(delta->t, t);
    QCOMPARE(static_cast<int>(delta->trains.size()), kFleet);
    QCOMPARE(static_cast<int>(delta->boards.size()), 2 * state.line.n);
    QVERIFY(delta->clock.has_value());
    QCOMPARE(delta->clock->dayOfWeek, ubahn::core::dowOf(t));
    QVERIFY(delta->clock->open);

    for (int id = 0; id < kFleet; ++id) {
        QVERIFY(findTrain(*delta, id) != nullptr);
    }
    for (int dirIndex = 0; dirIndex < 2; ++dirIndex) {
        const auto dir = static_cast<ubahn::api::Dir>(dirIndex);
        for (int pos = 0; pos < state.line.n; ++pos) {
            const int station = ubahn::core::stationOf(state.line, dir, pos);
            QVERIFY(findBoard(*delta, dir, station) != nullptr);
        }
    }

    const std::optional<ubahn::api::ViewDelta> again =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Snapshot);
    QVERIFY(again.has_value());
    QCOMPARE(static_cast<int>(again->trains.size()), kFleet);
    QCOMPARE(static_cast<int>(again->boards.size()), 2 * state.line.n);
    QVERIFY(again->clock.has_value());
}

void PublisherTest::repeatedPlayIsEmpty() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 10 * 3600;
    QVERIFY(ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());
    QVERIFY(!ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());
}

void PublisherTest::modeChangeEntersPlay() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 10 * 3600;
    state.now = t;
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 2);

    const std::optional<ubahn::api::ViewDelta> first =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(first.has_value());
    const ubahn::api::TrainUpdate* before = findTrain(*first, 0);
    QVERIFY(before != nullptr);
    QCOMPARE(before->mode, ubahn::api::TrainMode::OnTime);

    state.trains[0].lateness = 15;
    const std::optional<ubahn::api::ViewDelta> delta =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(delta.has_value());
    QCOMPARE(static_cast<int>(delta->trains.size()), 1);
    QCOMPARE(delta->trains[0].id, 0);
    QCOMPARE(delta->trains[0].mode, ubahn::api::TrainMode::CatchUp);
    QVERIFY(!delta->trains[0].flags.catchingUp);
    QCOMPARE(delta->trains[0].lateness, 15);
    QCOMPARE(static_cast<int>(delta->trains[0].edge),
             static_cast<int>(ubahn::api::TrackEdge::Platform));
    QCOMPARE(delta->trains[0].station, 2);
    QCOMPARE(delta->trains[0].along, 0.0);
    QVERIFY(delta->boards.empty());
    QVERIFY(!delta->clock.has_value());

    state.trains[1].flags.headingToDepot = true;
    const std::optional<ubahn::api::ViewDelta> icon =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(icon.has_value());
    QCOMPARE(static_cast<int>(icon->trains.size()), 1);
    QCOMPARE(icon->trains[0].id, 1);
    QCOMPARE(icon->trains[0].mode, ubahn::api::TrainMode::Service);
    QVERIFY(icon->trains[0].flags.headingToDepot);
}

void PublisherTest::seekCatchesAlong() {
    ubahn::core::EngineState state = trainingState();
    constexpr int trainId = 1;
    constexpr ubahn::api::SimTime depart = 1000;
    constexpr ubahn::api::SimTime arrive = 1130;
    ubahn::core::placeOnSpan(state, trainId, ubahn::api::Dir::I, 2, depart, arrive);
    QVERIFY(ubahn::core::collectDelta(state, depart, ubahn::core::DeltaMode::Play).has_value());

    constexpr ubahn::api::SimTime target = depart + 65;
    const std::optional<ubahn::api::ViewDelta> smooth =
        ubahn::core::collectDelta(state, target, ubahn::core::DeltaMode::Seek);
    if (smooth.has_value()) {
        QVERIFY(findTrain(*smooth, trainId) == nullptr);
    }

    // Same slope as 1700 m / 130 s, but the train is not where that slope predicts.
    state.trains[static_cast<std::size_t>(trainId)].seg =
        ubahn::core::Segment{target, target + 130, 200.0, 1900.0};
    const ubahn::api::TrainUpdate jumped = ubahn::core::trainView(state, trainId, target);
    QCOMPARE(jumped.v, 1.0 / 130.0);

    const std::optional<ubahn::api::ViewDelta> delta =
        ubahn::core::collectDelta(state, target, ubahn::core::DeltaMode::Seek);
    QVERIFY(delta.has_value());
    QCOMPARE(static_cast<int>(delta->trains.size()), 1);
    QCOMPARE(delta->trains[0].id, trainId);
    QCOMPARE(delta->trains[0].along, 200.0 / 1700.0);
    QCOMPARE(delta->trains[0].v, jumped.v);
    QCOMPARE(delta->trains[0].station, 2);
}

void PublisherTest::playIgnoresAlongOnly() {
    ubahn::core::EngineState state = trainingState();
    constexpr int trainId = 1;
    constexpr ubahn::api::SimTime depart = 1000;
    ubahn::core::placeOnSpan(state, trainId, ubahn::api::Dir::I, 2, depart, depart + 130);
    QVERIFY(ubahn::core::collectDelta(state, depart, ubahn::core::DeltaMode::Play).has_value());

    constexpr ubahn::api::SimTime target = depart + 65;
    state.trains[static_cast<std::size_t>(trainId)].seg =
        ubahn::core::Segment{target, target + 130, 200.0, 1900.0};
    QVERIFY(!ubahn::core::collectDelta(state, target, ubahn::core::DeltaMode::Play).has_value());
}

void PublisherTest::clockTracksDayWindowOpen() {
    ubahn::core::EngineState state = clockState();
    const ubahn::api::SimTime mondayNight = 3 * 3600;
    const ubahn::api::SimTime tuesdayNight = ubahn::api::kSecondsPerDay + mondayNight;
    const ubahn::api::SimTime tuesdayOpen = ubahn::api::kSecondsPerDay + ubahn::api::kOpenTod;
    const ubahn::api::SimTime tuesdayNoon = ubahn::api::kSecondsPerDay + 12 * 3600;

    const std::optional<ubahn::api::ViewDelta> first =
        ubahn::core::collectDelta(state, mondayNight, ubahn::core::DeltaMode::Play);
    QVERIFY(first.has_value());
    QVERIFY(first->clock.has_value());
    QCOMPARE(first->clock->dayOfWeek, 0);
    QVERIFY(!first->clock->open);
    QCOMPARE(first->clock->windowIndex, -1);

    const std::optional<ubahn::api::ViewDelta> nextDay =
        ubahn::core::collectDelta(state, tuesdayNight, ubahn::core::DeltaMode::Play);
    QVERIFY(nextDay.has_value());
    QVERIFY(nextDay->trains.empty());
    QVERIFY(nextDay->boards.empty());
    QVERIFY(nextDay->clock.has_value());
    QCOMPARE(nextDay->clock->dayOfWeek, 1);
    QVERIFY(!nextDay->clock->open);
    QCOMPARE(nextDay->clock->windowIndex, -1);
    QCOMPARE(nextDay->clock->hEff, 0);

    const std::optional<ubahn::api::ViewDelta> opened =
        ubahn::core::collectDelta(state, tuesdayOpen, ubahn::core::DeltaMode::Play);
    QVERIFY(opened.has_value());
    QVERIFY(opened->trains.empty());
    QVERIFY(opened->clock.has_value());
    QCOMPARE(opened->clock->dayOfWeek, 1);
    QVERIFY(opened->clock->open);
    QCOMPARE(opened->clock->windowIndex, 0);
    QCOMPARE(opened->clock->window.headway, 120);
    QCOMPARE(opened->clock->hEff, ubahn::core::hEffAt(state.analysis, state.schedule, tuesdayOpen));

    const std::optional<ubahn::api::ViewDelta> window =
        ubahn::core::collectDelta(state, tuesdayNoon, ubahn::core::DeltaMode::Play);
    QVERIFY(window.has_value());
    QVERIFY(window->trains.empty());
    QVERIFY(window->clock.has_value());
    QCOMPARE(window->clock->dayOfWeek, 1);
    QVERIFY(window->clock->open);
    QCOMPARE(window->clock->windowIndex, 1);
    QCOMPARE(window->clock->window.headway, 180);

    QVERIFY(
        !ubahn::core::collectDelta(state, tuesdayNoon, ubahn::core::DeltaMode::Play).has_value());
}

void PublisherTest::messagesAreTakenAndCleared() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 10 * 3600;
    QVERIFY(ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());

    ubahn::api::DelayNotice delay;
    delay.trainId = 7;
    delay.dir = ubahn::api::Dir::II;
    delay.station = 5;
    delay.seconds = 37;
    state.pendingDelays.push_back(delay);

    ubahn::api::Warning warning;
    warning.code = ubahn::api::WarningCode::NoTrainForTrip;
    warning.terminal = static_cast<int>(ubahn::api::Term::A);
    warning.tripSlot = t;
    warning.message = "нет поезда к нитке";
    state.pendingWarnings.push_back(warning);

    const std::optional<ubahn::api::ViewDelta> delta =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(delta.has_value());
    QVERIFY(delta->trains.empty());
    QVERIFY(delta->boards.empty());
    QVERIFY(!delta->clock.has_value());
    QCOMPARE(static_cast<int>(delta->delays.size()), 1);
    QCOMPARE(delta->delays[0].trainId, 7);
    QCOMPARE(delta->delays[0].seconds, 37);
    QCOMPARE(delta->delays[0].station, 5);
    QCOMPARE(static_cast<int>(delta->warnings.size()), 1);
    QVERIFY(delta->warnings[0].message == warning.message);
    QVERIFY(state.pendingDelays.empty());
    QVERIFY(state.pendingWarnings.empty());
    QVERIFY(!ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());

    state.pendingDelays.push_back(delay);
    const std::optional<ubahn::api::ViewDelta> snapshot =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Snapshot);
    QVERIFY(snapshot.has_value());
    QCOMPARE(static_cast<int>(snapshot->delays.size()), 1);
    QCOMPARE(static_cast<int>(snapshot->trains.size()), kFleet);
    QVERIFY(state.pendingDelays.empty());
}

void PublisherTest::boardChangeEntersPlay() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 10 * 3600;
    QVERIFY(ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());

    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    platform.lastArrival = 25000;
    platform.lastDwell = 80;
    platform.lastShift = 4;

    const std::optional<ubahn::api::ViewDelta> departed =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(departed.has_value());
    QVERIFY(departed->trains.empty());
    QCOMPARE(static_cast<int>(departed->boards.size()), 1);
    QCOMPARE(departed->boards[0].dir, ubahn::api::Dir::I);
    QCOMPARE(departed->boards[0].station, 3);
    QCOMPARE(departed->boards[0].lastArrival, static_cast<ubahn::api::SimTime>(25000));
    QVERIFY(!departed->boards[0].occupied);
    QCOMPARE(departed->boards[0].dwellShown, 80);
    QCOMPARE(departed->boards[0].shift, 4);

    state.now = t;
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::II, 1);
    state.trains[0].aAct = 25231;
    ubahn::core::platformAt(state, static_cast<int>(ubahn::api::Dir::II), 1).lastArrival = 25231;
    ubahn::core::platformAt(state, static_cast<int>(ubahn::api::Dir::II), 1).lastShift = 0;

    const std::optional<ubahn::api::ViewDelta> arrived =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(arrived.has_value());
    const int station = ubahn::core::stationOf(state.line, ubahn::api::Dir::II, 1);
    const ubahn::api::BoardUpdate* board = findBoard(*arrived, ubahn::api::Dir::II, station);
    QVERIFY(board != nullptr);
    QVERIFY(board->occupied);
    QCOMPARE(board->dwellSince, static_cast<ubahn::api::SimTime>(25231));
    QCOMPARE(board->lastArrival, static_cast<ubahn::api::SimTime>(25231));
}

void PublisherTest::depotMoveChangesStation() {
    ubahn::core::EngineState state = trainingState();
    const ubahn::api::SimTime t = 3 * 3600;
    QVERIFY(ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play).has_value());

    state.trains[0].depotAt = ubahn::api::Term::B;
    const std::optional<ubahn::api::ViewDelta> delta =
        ubahn::core::collectDelta(state, t, ubahn::core::DeltaMode::Play);
    QVERIFY(delta.has_value());
    QCOMPARE(static_cast<int>(delta->trains.size()), 1);
    QCOMPARE(delta->trains[0].id, 0);
    QCOMPARE(static_cast<int>(delta->trains[0].edge),
             static_cast<int>(ubahn::api::TrackEdge::Depot));
    QCOMPARE(delta->trains[0].station, state.line.n - 1);
    QCOMPARE(delta->trains[0].mode, ubahn::api::TrainMode::Service);
}

QTEST_APPLESS_MAIN(PublisherTest)

#include "tst_publisher.moc"
