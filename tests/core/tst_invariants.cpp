#include "EngineFixture.h"
#include "core/Invariants.h"
#include "core/Schedule.h"

#include <QtTest>
#include <string>
#include <vector>

namespace {

ubahn::api::SimParams trainingParams(int fleet) {
    return ubahn::core::makeParams(ubahn::core::trainingLineConfig(),
                                   ubahn::core::constantSchedule(120, 40, 0), fleet);
}

std::string joinViolations(const std::vector<std::string>& violations) {
    std::string text;
    for (const std::string& line : violations) {
        text += line;
        text += '\n';
    }
    return text;
}

bool containsText(const std::vector<std::string>& violations, const char* needle) {
    const std::string key(needle);
    for (const std::string& line : violations) {
        if (line.find(key) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void linkTrack(ubahn::core::EngineState& state, ubahn::api::Dir dir,
               const std::vector<int>& order) {
    const int dirIndex = static_cast<int>(dir);
    int previous = -1;
    for (int id : order) {
        ubahn::core::Train& train = state.trains[static_cast<std::size_t>(id)];
        train.prevOnTrack = previous;
        train.nextOnTrack = -1;
        if (previous >= 0) {
            state.trains[static_cast<std::size_t>(previous)].nextOnTrack = id;
        }
        previous = id;
    }
    ubahn::core::TrackList& list = state.tracks[static_cast<std::size_t>(dirIndex)];
    list.head = order.empty() ? -1 : order.front();
    list.tail = order.empty() ? -1 : order.back();
}

ubahn::core::Trip& planFor(ubahn::core::EngineState& state, int train, ubahn::api::Dir dir) {
    ubahn::core::Trip& trip = ubahn::core::addTrip(state, dir, 0, state.line.cfg.dwellMin);
    state.trains[static_cast<std::size_t>(train)].tripId = trip.id;
    return trip;
}

} // namespace

class InvariantsTest : public QObject {
    Q_OBJECT

private slots:
    void freshStateIsClean();
    void twoTrainsOnOnePlatform();
    void twoTrainsInOneDeadEnd();
    void twoTrainsInOneThroat();
    void trackOrderMatchesPositions();
    void trackOrderReversed();
    void spanIsAheadOfItsStation();
    void trainOnPathMissingFromList();
    void trackCycleDoesNotHang();
    void missingHeapEntry();
    void depotTrainWithoutTripHasNoEvent();
    void assignedDepotTrainNeedsHeapEntry();
    void arrivalOnTheBoundary();
    void arrivalBeforeHeadway();
    void arrivalBeforeClear();
    void arrivalBeforePlan();
    void noPreviousArrivalSample();
    void departureOnTheBoundary();
    void runShorterThanMin();
    void runLongerThanMax();
    void dwellShorterThanMin();
    void departureBeforePlan();
    void deadEndEntryOnTheBoundary();
    void deadEndEntryTooSoon();
    void deadEndOccupancyTooShort();
    void outOfRangeDoesNotCrash();
};

void InvariantsTest::freshStateIsClean() {
    const ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    const std::string text = joinViolations(ubahn::core::checkState(state));
    QCOMPARE(QString::fromStdString(text), QString());
}

void InvariantsTest::twoTrainsOnOnePlatform() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 2);
    ubahn::core::placeAtPlatform(state, 1, ubahn::api::Dir::I, 2);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C9: platform"), text.c_str());
}

void InvariantsTest::twoTrainsInOneDeadEnd() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeInDeadEnd(state, 0, ubahn::api::Term::A);
    ubahn::core::placeInDeadEnd(state, 1, ubahn::api::Term::A);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C9: dead end"), text.c_str());
}

void InvariantsTest::twoTrainsInOneThroat() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    state.trains[0].state = ubahn::core::TrainState::LeavingDepot;
    state.trains[1].state = ubahn::core::TrainState::ToDepot;
    state.trains[1].dir = ubahn::api::Dir::II;
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C9: throat"), text.c_str());
}

void InvariantsTest::trackOrderMatchesPositions() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 4);
    ubahn::core::placeAtPlatform(state, 1, ubahn::api::Dir::I, 1);
    linkTrack(state, ubahn::api::Dir::I, {0, 1});
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(!containsText(violations, "C10"), text.c_str());
}

void InvariantsTest::trackOrderReversed() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 4);
    ubahn::core::placeAtPlatform(state, 1, ubahn::api::Dir::I, 1);
    linkTrack(state, ubahn::api::Dir::I, {1, 0});
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C10: track dir 0 order"), text.c_str());
}

void InvariantsTest::spanIsAheadOfItsStation() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeOnSpan(state, 0, ubahn::api::Dir::I, 2, 1000, 1200);
    ubahn::core::placeAtPlatform(state, 1, ubahn::api::Dir::I, 2);
    linkTrack(state, ubahn::api::Dir::I, {0, 1});
    const std::vector<std::string> forward = ubahn::core::checkState(state);
    const std::string forwardText = joinViolations(forward);
    QVERIFY2(!containsText(forward, "C10"), forwardText.c_str());

    linkTrack(state, ubahn::api::Dir::I, {1, 0});
    const std::vector<std::string> reversed = ubahn::core::checkState(state);
    const std::string reversedText = joinViolations(reversed);
    QVERIFY2(containsText(reversed, "C10: track dir 0 order"), reversedText.c_str());
}

void InvariantsTest::trainOnPathMissingFromList() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 1);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "not in that track list"), text.c_str());
}

void InvariantsTest::trackCycleDoesNotHang() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    state.trains[0].nextOnTrack = 1;
    state.trains[0].prevOnTrack = 1;
    state.trains[1].nextOnTrack = 0;
    state.trains[1].prevOnTrack = 0;
    state.tracks[0].head = 0;
    state.tracks[0].tail = 1;
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C10:"), text.c_str());
}

void InvariantsTest::missingHeapEntry() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 1);
    linkTrack(state, ubahn::api::Dir::I, {0});
    ubahn::core::NextEvent depart;
    depart.type = ubahn::core::EventType::Depart;
    depart.time = state.now + 10;
    depart.valid = true;
    ubahn::core::setNext(state, 0, depart);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "missing its heap entry"), text.c_str());
    QVERIFY2(!containsText(violations, "C9"), text.c_str());
    QVERIFY2(!containsText(violations, "C10"), text.c_str());
}

void InvariantsTest::depotTrainWithoutTripHasNoEvent() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::NextEvent wake;
    wake.type = ubahn::core::EventType::LeaveDepot;
    wake.time = state.now + 30;
    wake.valid = true;
    ubahn::core::setNext(state, 0, wake);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "without a trip but next is valid"), text.c_str());
}

void InvariantsTest::assignedDepotTrainNeedsHeapEntry() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    planFor(state, 0, ubahn::api::Dir::I);
    QCOMPARE(state.trains[0].state, ubahn::core::TrainState::InDepot);
    const std::vector<std::string> violations = ubahn::core::checkState(state);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "train 0 is missing its heap entry"), text.c_str());
}

void InvariantsTest::arrivalOnTheBoundary() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 3);
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const int32_t headway = state.line.cfg.headwayMin;
    const int32_t clear = state.line.cfg.tClear;
    const ubahn::api::SimTime arrival = 10000 + headway;
    trip.aPlan[3] = arrival;
    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    platform.lastArrival = 10000;
    platform.lastDeparture = arrival - clear;
    const std::string text = joinViolations(ubahn::core::checkArrival(state, 0, arrival));
    QCOMPARE(QString::fromStdString(text), QString());
}

void InvariantsTest::arrivalBeforeHeadway() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 3);
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    trip.aPlan[3] = 0;
    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    platform.lastArrival = 20000;
    platform.lastDeparture = ubahn::api::kNoTime;
    const ubahn::api::SimTime arrival = 20000 + state.line.cfg.headwayMin - 1;
    const std::vector<std::string> violations = ubahn::core::checkArrival(state, 0, arrival);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C6:"), text.c_str());
    QVERIFY2(!containsText(violations, "C7:"), text.c_str());
    QVERIFY2(!containsText(violations, "C8:"), text.c_str());
}

void InvariantsTest::arrivalBeforeClear() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 3);
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    trip.aPlan[3] = 0;
    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    platform.lastArrival = ubahn::api::kNoTime;
    platform.lastDeparture = 30000;
    const ubahn::api::SimTime arrival = 30000 + state.line.cfg.tClear - 1;
    const std::vector<std::string> violations = ubahn::core::checkArrival(state, 0, arrival);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C7:"), text.c_str());
    QVERIFY2(!containsText(violations, "C6:"), text.c_str());
    QVERIFY2(!containsText(violations, "C8:"), text.c_str());
}

void InvariantsTest::arrivalBeforePlan() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 3);
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    trip.aPlan[3] = 50000;
    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    platform.lastArrival = ubahn::api::kNoTime;
    platform.lastDeparture = ubahn::api::kNoTime;
    const std::vector<std::string> violations = ubahn::core::checkArrival(state, 0, 49999);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C8:"), text.c_str());
    QVERIFY2(!containsText(violations, "C6:"), text.c_str());
    QVERIFY2(!containsText(violations, "C7:"), text.c_str());
}

void InvariantsTest::noPreviousArrivalSample() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 3);
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    trip.aPlan[3] = 0;
    ubahn::core::Platform& platform = ubahn::core::platformAt(state, 0, 3);
    QCOMPARE(platform.lastArrival, ubahn::api::kNoTime);
    QCOMPARE(platform.lastDeparture, ubahn::api::kNoTime);
    const std::string text = joinViolations(ubahn::core::checkArrival(state, 0, 100));
    QCOMPARE(QString::fromStdString(text), QString());
}

void InvariantsTest::departureOnTheBoundary() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::Train& train = state.trains[0];
    train.aAct = 100000;
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const ubahn::core::Span& span = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 0);
    QVERIFY(span.tMin > 0);
    QVERIFY(span.tMax >= span.tMin);
    const ubahn::api::SimTime depart = train.aAct + state.line.cfg.dwellMin;
    trip.dPlan[0] = depart;
    const std::string atMin =
        joinViolations(ubahn::core::checkDeparture(state, 0, depart, span.tMin));
    QCOMPARE(QString::fromStdString(atMin), QString());
    const std::string atMax =
        joinViolations(ubahn::core::checkDeparture(state, 0, depart, span.tMax));
    QCOMPARE(QString::fromStdString(atMax), QString());
}

void InvariantsTest::runShorterThanMin() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::Train& train = state.trains[0];
    train.aAct = 100000;
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const int32_t tMin = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 0).tMin;
    const ubahn::api::SimTime depart = train.aAct + state.line.cfg.dwellMin;
    trip.dPlan[0] = depart;
    const std::vector<std::string> violations =
        ubahn::core::checkDeparture(state, 0, depart, tMin - 1);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C3:"), text.c_str());
    QVERIFY2(!containsText(violations, "C4:"), text.c_str());
    QVERIFY2(!containsText(violations, "C5:"), text.c_str());
    QVERIFY2(!containsText(violations, "C8:"), text.c_str());
}

void InvariantsTest::runLongerThanMax() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::Train& train = state.trains[0];
    train.aAct = 100000;
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const int32_t tMax = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 0).tMax;
    const ubahn::api::SimTime depart = train.aAct + state.line.cfg.dwellMin;
    trip.dPlan[0] = depart;
    const std::vector<std::string> violations =
        ubahn::core::checkDeparture(state, 0, depart, tMax + 1);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C4:"), text.c_str());
    QVERIFY2(!containsText(violations, "C3:"), text.c_str());
    QVERIFY2(!containsText(violations, "C5:"), text.c_str());
    QVERIFY2(!containsText(violations, "C8:"), text.c_str());
}

void InvariantsTest::dwellShorterThanMin() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::Train& train = state.trains[0];
    train.aAct = 100000;
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const int32_t tMin = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 0).tMin;
    const ubahn::api::SimTime depart = train.aAct + state.line.cfg.dwellMin - 1;
    trip.dPlan[0] = train.aAct;
    const std::vector<std::string> violations = ubahn::core::checkDeparture(state, 0, depart, tMin);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C5:"), text.c_str());
    QVERIFY2(!containsText(violations, "C3:"), text.c_str());
    QVERIFY2(!containsText(violations, "C4:"), text.c_str());
    QVERIFY2(!containsText(violations, "C8:"), text.c_str());
}

void InvariantsTest::departureBeforePlan() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::Train& train = state.trains[0];
    train.aAct = 100000;
    ubahn::core::Trip& trip = planFor(state, 0, ubahn::api::Dir::I);
    const int32_t tMin = ubahn::core::spanOf(state.line, ubahn::api::Dir::I, 0).tMin;
    const ubahn::api::SimTime depart = train.aAct + state.line.cfg.dwellMin;
    trip.dPlan[0] = depart + 1;
    const std::vector<std::string> violations = ubahn::core::checkDeparture(state, 0, depart, tMin);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "C8:"), text.c_str());
    QVERIFY2(!containsText(violations, "C3:"), text.c_str());
    QVERIFY2(!containsText(violations, "C4:"), text.c_str());
    QVERIFY2(!containsText(violations, "C5:"), text.c_str());
}

void InvariantsTest::deadEndEntryOnTheBoundary() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    state.trains[0].dir = ubahn::api::Dir::I;
    const int32_t turn = ubahn::core::tTurn(state.line.cfg);
    ubahn::core::Resource& deadEnd =
        state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].deadEnd;
    deadEnd.occupant = -1;
    deadEnd.occupiedSince = 100000;
    deadEnd.freeAt = 100000 + turn;
    const std::string text =
        joinViolations(ubahn::core::checkDeadEndEntry(state, 0, 100000 + turn));
    QCOMPARE(QString::fromStdString(text), QString());

    deadEnd.occupiedSince = ubahn::api::kNoTime;
    deadEnd.freeAt = ubahn::api::kNoTime;
    const std::string first = joinViolations(ubahn::core::checkDeadEndEntry(state, 0, 10));
    QCOMPARE(QString::fromStdString(first), QString());
}

void InvariantsTest::deadEndEntryTooSoon() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    state.trains[0].dir = ubahn::api::Dir::I;
    const int32_t turn = ubahn::core::tTurn(state.line.cfg);
    ubahn::core::Resource& deadEnd =
        state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].deadEnd;
    deadEnd.occupant = -1;
    deadEnd.occupiedSince = 100000;
    deadEnd.freeAt = 100000 + turn;
    const std::vector<std::string> violations =
        ubahn::core::checkDeadEndEntry(state, 0, 100000 + turn - 1);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "entry interval"), text.c_str());
    QVERIFY2(!containsText(violations, "occupancy"), text.c_str());
}

void InvariantsTest::deadEndOccupancyTooShort() {
    ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    state.trains[0].dir = ubahn::api::Dir::I;
    const int32_t turn = ubahn::core::tTurn(state.line.cfg);
    ubahn::core::Resource& deadEnd =
        state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].deadEnd;
    deadEnd.occupant = -1;
    deadEnd.occupiedSince = 100000;
    deadEnd.freeAt = 100000 + turn - 1;
    const std::vector<std::string> violations =
        ubahn::core::checkDeadEndEntry(state, 0, 100000 + turn + 50);
    const std::string text = joinViolations(violations);
    QVERIFY2(containsText(violations, "occupancy"), text.c_str());
    QVERIFY2(!containsText(violations, "entry interval"), text.c_str());
}

void InvariantsTest::outOfRangeDoesNotCrash() {
    const ubahn::core::EngineState state = ubahn::core::makeState(trainingParams(4), 1);
    QVERIFY(!ubahn::core::checkArrival(state, -1, 0).empty());
    QVERIFY(!ubahn::core::checkArrival(state, 99, 0).empty());
    QVERIFY(!ubahn::core::checkDeparture(state, -1, 0, 0).empty());
    QVERIFY(!ubahn::core::checkDeparture(state, 99, 0, 0).empty());
    QVERIFY(!ubahn::core::checkDeadEndEntry(state, -1, 0).empty());
    QVERIFY(!ubahn::core::checkDeadEndEntry(state, 99, 0).empty());

    ubahn::core::EngineState broken = ubahn::core::makeState(trainingParams(4), 1);
    broken.trains[0].pos = 100;
    broken.trains[0].state = ubahn::core::TrainState::Dwelling;
    QVERIFY(!ubahn::core::checkState(broken).empty());
    QVERIFY(!ubahn::core::checkArrival(broken, 0, 0).empty());
}

QTEST_APPLESS_MAIN(InvariantsTest)
#include "tst_invariants.moc"
