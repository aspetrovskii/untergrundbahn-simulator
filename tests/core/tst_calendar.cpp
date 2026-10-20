#include "EngineFixture.h"
#include "core/Calendar.h"
#include "core/Time.h"

#include <QtTest>
#include <cstdint>
#include <deque>
#include <vector>

namespace {

constexpr int32_t kHour = 3600;
constexpr ubahn::api::SimTime kDay = ubahn::api::kSecondsPerDay;

std::vector<ubahn::api::Window> weekdayWindows() {
    return {
        {6 * kHour, 7 * kHour, 180, 60, 0.15},   {7 * kHour, 10 * kHour, 120, 80, 0.55},
        {10 * kHour, 17 * kHour, 160, 60, 0.25}, {17 * kHour, 20 * kHour, 120, 80, 0.50},
        {20 * kHour, 24 * kHour, 300, 40, 0.12},
    };
}

std::vector<ubahn::api::Window> weekendWindows() {
    return {
        {6 * kHour, 10 * kHour, 360, 40, 0.12},
        {10 * kHour, 20 * kHour, 240, 60, 0.20},
        {20 * kHour, 24 * kHour, 360, 40, 0.10},
    };
}

// SPEC 6.3: Monday..Friday share one profile, Saturday and Sunday the other.
ubahn::api::ScheduleConfig specSchedule() {
    ubahn::api::ScheduleConfig schedule;
    for (int day = 0; day < 5; ++day) {
        schedule.days[static_cast<std::size_t>(day)].windows = weekdayWindows();
    }
    for (int day = 5; day < 7; ++day) {
        schedule.days[static_cast<std::size_t>(day)].windows = weekendWindows();
    }
    return schedule;
}

ubahn::core::EngineState calendarState(int fleet) {
    const ubahn::api::SimParams params =
        ubahn::core::makeParams(ubahn::core::trainingLineConfig(), specSchedule(), fleet);
    return ubahn::core::makeState(params, 1);
}

ubahn::core::NextEvent wake(ubahn::core::EventType type, ubahn::api::SimTime time) {
    ubahn::core::NextEvent event;
    event.type = type;
    event.time = time;
    event.valid = true;
    return event;
}

void expectSequence(const ubahn::core::EngineState& state, ubahn::api::SimTime now,
                    const std::vector<ubahn::core::NextEvent>& expected) {
    for (const ubahn::core::NextEvent& event : expected) {
        const ubahn::core::NextEvent next = ubahn::core::calendarNext(state, now);
        QVERIFY(next.time > now);
        QCOMPARE(next.valid, event.valid);
        QCOMPARE(next.time, event.time);
        QCOMPARE(static_cast<int>(next.type), static_cast<int>(event.type));
        now = next.time;
    }
}

std::vector<int> range(int first, int last) {
    std::vector<int> ids;
    for (int id = first; id <= last; ++id) {
        ids.push_back(id);
    }
    return ids;
}

void fillDepots(ubahn::core::EngineState& state, const std::vector<int>& inA,
                const std::vector<int>& inB) {
    std::deque<int>& depotA = state.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot;
    std::deque<int>& depotB = state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot;
    depotA.clear();
    depotB.clear();
    const auto park = [&](std::deque<int>& depot, const std::vector<int>& ids,
                          ubahn::api::Term term) {
        for (const int id : ids) {
            ubahn::core::Train& train = state.trains[static_cast<std::size_t>(id)];
            train.state = ubahn::core::TrainState::InDepot;
            train.depotAt = term;
            depot.push_back(id);
        }
    };
    park(depotA, inA, ubahn::api::Term::A);
    park(depotB, inB, ubahn::api::Term::B);
}

void expectIds(const std::deque<int>& depot, const std::vector<int>& expected) {
    QCOMPARE(static_cast<int>(depot.size()), static_cast<int>(expected.size()));
    for (int i = 0; i < static_cast<int>(expected.size()); ++i) {
        QCOMPARE(depot[static_cast<std::size_t>(i)], expected[static_cast<std::size_t>(i)]);
    }
}

std::vector<int> drain(ubahn::core::DirtySet& dirty) {
    std::vector<int> ids;
    while (!dirty.empty()) {
        ids.push_back(dirty.pop());
    }
    return ids;
}

void expectDirty(ubahn::core::DirtySet& dirty, const std::vector<int>& expected) {
    const std::vector<int> ids = drain(dirty);
    QCOMPARE(static_cast<int>(ids.size()), static_cast<int>(expected.size()));
    for (int i = 0; i < static_cast<int>(expected.size()); ++i) {
        QCOMPARE(ids[static_cast<std::size_t>(i)], expected[static_cast<std::size_t>(i)]);
    }
}

} // namespace

class CalendarTest : public QObject {
    Q_OBJECT

private slots:
    void weekdayWakeSequence();
    void weekendWakeSequence();
    void negativeWarmupFloorDay();
    void openClearsOffHoursAndDirties();
    void closeSetsOffHours();
    void windowChangeWakesDispatchersOnly();
    void rebalanceTwentyAndSix();
    void rebalanceSevenAndSix();
    void movedTrainsAreDirty();
    void periodEndIsNotCalendar();
};

void CalendarTest::weekdayWakeSequence() {
    const ubahn::core::EngineState state = calendarState(2);
    using ubahn::core::EventType;
    // One weekday from just before midnight: 00:00, 03:00, 06:00, window edges, 24:00.
    expectSequence(state, -1,
                   {
                       wake(EventType::ServiceClose, 0),
                       wake(EventType::NightRebalance, 3 * kHour),
                       wake(EventType::ServiceOpen, 6 * kHour),
                       wake(EventType::WindowBoundary, 7 * kHour),
                       wake(EventType::WindowBoundary, 10 * kHour),
                       wake(EventType::WindowBoundary, 17 * kHour),
                       wake(EventType::WindowBoundary, 20 * kHour),
                       wake(EventType::ServiceClose, kDay),
                   });
}

void CalendarTest::weekendWakeSequence() {
    const ubahn::core::EngineState state = calendarState(2);
    using ubahn::core::EventType;
    const ubahn::api::SimTime saturday = 5 * kDay;
    expectSequence(state, saturday - 1,
                   {
                       wake(EventType::ServiceClose, saturday),
                       wake(EventType::NightRebalance, saturday + 3 * kHour),
                       wake(EventType::ServiceOpen, saturday + 6 * kHour),
                       wake(EventType::WindowBoundary, saturday + 10 * kHour),
                       wake(EventType::WindowBoundary, saturday + 20 * kHour),
                       wake(EventType::ServiceClose, saturday + kDay),
                   });
}

void CalendarTest::negativeWarmupFloorDay() {
    const ubahn::core::EngineState state = calendarState(2);
    using ubahn::core::EventType;
    // Sunday of the week before the epoch. api::dayOfWeek truncates this onto Monday;
    // floor division keeps the weekend profile (decision R16, ALGORITHM 2).
    const ubahn::api::SimTime sunday = -kDay;
    QCOMPARE(ubahn::core::dowOf(sunday + 6 * kHour), 6);
    expectSequence(state, sunday - 1,
                   {
                       wake(EventType::ServiceClose, sunday),
                       wake(EventType::NightRebalance, sunday + 3 * kHour),
                       wake(EventType::ServiceOpen, sunday + 6 * kHour),
                       wake(EventType::WindowBoundary, sunday + 10 * kHour),
                       wake(EventType::WindowBoundary, sunday + 20 * kHour),
                       wake(EventType::ServiceClose, 0),
                       wake(EventType::NightRebalance, 3 * kHour),
                       wake(EventType::ServiceOpen, 6 * kHour),
                       wake(EventType::WindowBoundary, 7 * kHour),
                   });
}

void CalendarTest::openClearsOffHoursAndDirties() {
    ubahn::core::EngineState state = calendarState(4);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::placeOnSpan(state, 1, ubahn::api::Dir::I, 1, 0, 100);
    state.trains[0].flags.offHours = true;
    state.trains[1].flags.offHours = true;
    state.trains[2].flags.offHours = true;
    state.trains[3].flags.offHours = false;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(4));
    // Sunday 06:00 is still negative: open must use floor time-of-day, not truncation.
    ubahn::core::handleCalendar(state, -kDay + 6 * kHour, dirty);

    QVERIFY(!state.trains[0].flags.offHours);
    QVERIFY(!state.trains[1].flags.offHours);
    QVERIFY(state.trains[2].flags.offHours);
    QVERIFY(!state.trains[3].flags.offHours);
    expectDirty(dirty, {0, 1, 4, 5});
}

void CalendarTest::closeSetsOffHours() {
    ubahn::core::EngineState state = calendarState(4);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 0);
    ubahn::core::placeOnSpan(state, 1, ubahn::api::Dir::II, 2, 0, 100);
    state.trains[0].flags.offHours = false;
    state.trains[1].flags.offHours = false;
    state.trains[2].flags.offHours = true;
    state.trains[3].flags.offHours = false;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(4));
    ubahn::core::handleCalendar(state, -kDay, dirty);

    QVERIFY(state.trains[0].flags.offHours);
    QVERIFY(state.trains[1].flags.offHours);
    QVERIFY(state.trains[2].flags.offHours);
    QVERIFY(!state.trains[3].flags.offHours);
    // Close sets the flag and does not push the trains. Day change wakes both dispatchers.
    expectDirty(dirty, {4, 5});
}

void CalendarTest::windowChangeWakesDispatchersOnly() {
    ubahn::core::EngineState state = calendarState(4);
    ubahn::core::placeAtPlatform(state, 0, ubahn::api::Dir::I, 2);
    state.trains[0].flags.offHours = false;

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(4));
    ubahn::core::handleCalendar(state, 7 * kHour, dirty);

    QVERIFY(!state.trains[0].flags.offHours);
    expectDirty(dirty, {4, 5});
}

void CalendarTest::rebalanceTwentyAndSix() {
    ubahn::core::EngineState state = calendarState(26);
    fillDepots(state, range(0, 19), range(20, 25));
    ubahn::core::rebalanceDepots(state, 3 * kHour);

    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot, range(0, 12));
    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot,
              {20, 21, 22, 23, 24, 25, 13, 14, 15, 16, 17, 18, 19});
    for (int id = 0; id <= 12; ++id) {
        QCOMPARE(state.trains[static_cast<std::size_t>(id)].depotAt, ubahn::api::Term::A);
        QCOMPARE(state.trains[static_cast<std::size_t>(id)].state,
                 ubahn::core::TrainState::InDepot);
    }
    for (int id = 13; id <= 25; ++id) {
        QCOMPARE(state.trains[static_cast<std::size_t>(id)].depotAt, ubahn::api::Term::B);
    }

    ubahn::core::EngineState other = calendarState(26);
    fillDepots(other, range(0, 5), range(6, 25));
    ubahn::core::rebalanceDepots(other, 3 * kHour);
    expectIds(other.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot,
              {0, 1, 2, 3, 4, 5, 19, 20, 21, 22, 23, 24, 25});
    expectIds(other.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot, range(6, 18));
}

void CalendarTest::rebalanceSevenAndSix() {
    ubahn::core::EngineState state = calendarState(13);
    const std::deque<int> beforeA =
        state.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot;
    const std::deque<int> beforeB =
        state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot;
    QCOMPARE(static_cast<int>(beforeA.size()), 7);
    QCOMPARE(static_cast<int>(beforeB.size()), 6);

    ubahn::core::rebalanceDepots(state, -kDay + 3 * kHour);
    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot,
              std::vector<int>(beforeA.begin(), beforeA.end()));
    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot,
              std::vector<int>(beforeB.begin(), beforeB.end()));

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(13));
    ubahn::core::handleCalendar(state, 3 * kHour, dirty);
    QVERIFY(dirty.empty());
}

void CalendarTest::movedTrainsAreDirty() {
    ubahn::core::EngineState state = calendarState(26);
    fillDepots(state, range(0, 19), range(20, 25));

    ubahn::core::DirtySet dirty(ubahn::core::ownerCount(26));
    ubahn::core::handleCalendar(state, -kDay + 3 * kHour, dirty);

    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::A)].depot, range(0, 12));
    expectIds(state.terms[static_cast<std::size_t>(ubahn::api::Term::B)].depot,
              {20, 21, 22, 23, 24, 25, 13, 14, 15, 16, 17, 18, 19});
    expectDirty(dirty, range(13, 19));
}

void CalendarTest::periodEndIsNotCalendar() {
    ubahn::core::EngineState state = calendarState(2);
    state.params.period.end = 12 * kHour;
    const ubahn::core::NextEvent next = ubahn::core::calendarNext(state, 10 * kHour);
    QCOMPARE(next.valid, true);
    QCOMPARE(next.time, static_cast<ubahn::api::SimTime>(17 * kHour));
    QCOMPARE(static_cast<int>(next.type), static_cast<int>(ubahn::core::EventType::WindowBoundary));
}

QTEST_APPLESS_MAIN(CalendarTest)

#include "tst_calendar.moc"
