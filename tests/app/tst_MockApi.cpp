#include "api/Functions.h"
#include "api/SimController.h"

#include <QSignalSpy>
#include <QTest>

class MockApiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultsMatchDocumentation();
    void controllerEmitsStaticSamples();
    void controllerPlacesTerminalTrainsAtSelectedLineEnds();
    void simpleValidationFindsBasicErrors();
};

void MockApiTest::initTestCase() {
    ubahn::api::registerMetaTypes();
}

void MockApiTest::defaultsMatchDocumentation() {
    const auto presets = ubahn::api::builtinPresets();
    QCOMPARE(presets.size(), std::size_t{3});
    QCOMPARE(presets.front().id, std::string("training-8"));
    QCOMPARE(presets.front().params.line.stationNames.size(), std::size_t{8});
    QCOMPARE(presets.front().params.line.spans.size(), std::size_t{7});

    const auto schedule = ubahn::api::defaultSchedule();
    QCOMPARE(schedule.days[0].windows.size(), std::size_t{5});
    QCOMPARE(schedule.days[5].windows.size(), std::size_t{3});
    QCOMPARE(schedule.delayMin, 20);
    QCOMPARE(schedule.delayMax, 60);
}

void MockApiTest::controllerEmitsStaticSamples() {
    auto controller = ubahn::api::createSimController();
    QSignalSpy startedSpy(controller.get(), &ubahn::api::SimController::started);
    QSignalSpy chunkSpy(controller.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy summarySpy(controller.get(), &ubahn::api::SimController::summaryReady);

    controller->start(ubahn::api::defaultParams());

    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(chunkSpy.count(), 4);
    QCOMPARE(summarySpy.count(), 1);

    const auto info = startedSpy.at(0).at(0).value<ubahn::api::RunInfo>();
    QCOMPARE(info.seed, uint64_t{42});

    const auto initial = chunkSpy.at(0).at(0).value<ubahn::api::ViewChunkPtr>();
    const auto play = chunkSpy.at(1).at(0).value<ubahn::api::ViewChunkPtr>();
    const auto seek = chunkSpy.at(3).at(0).value<ubahn::api::ViewChunkPtr>();
    QVERIFY(initial != nullptr);
    QVERIFY(play != nullptr);
    QVERIFY(seek != nullptr);
    QCOMPARE(initial->kind, ubahn::api::ChunkKind::Initial);
    QCOMPARE(play->kind, ubahn::api::ChunkKind::Play);
    QCOMPARE(seek->kind, ubahn::api::ChunkKind::Seek);
    QVERIFY(!play->deltas.front().delays.empty());

    const auto result = summarySpy.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(result.completed);
}

void MockApiTest::controllerPlacesTerminalTrainsAtSelectedLineEnds() {
    for (const auto& preset : ubahn::api::builtinPresets()) {
        auto controller = ubahn::api::createSimController();
        QSignalSpy chunkSpy(controller.get(), &ubahn::api::SimController::chunkReady);
        controller->start(preset.params);

        QCOMPARE(chunkSpy.count(), 4);
        const auto initial = chunkSpy.at(0).at(0).value<ubahn::api::ViewChunkPtr>();
        QVERIFY(initial != nullptr);
        const auto& delta = initial->deltas.front();
        const int lastStation = static_cast<int>(preset.params.line.stationNames.size()) - 1;
        QCOMPARE(delta.boards.size(), preset.params.line.stationNames.size() * 2);
        QCOMPARE(delta.trains.at(1).edge, ubahn::api::TrackEdge::Depot);
        QCOMPARE(delta.trains.at(1).station, lastStation);
        QCOMPARE(delta.trains.at(4).edge, ubahn::api::TrackEdge::ToDeadEnd);
        QCOMPARE(delta.trains.at(4).station, lastStation);
        QCOMPARE(delta.trains.at(7).edge, ubahn::api::TrackEdge::FromDepot);
        QCOMPARE(delta.trains.at(7).station, lastStation);
    }
}

void MockApiTest::simpleValidationFindsBasicErrors() {
    auto params = ubahn::api::defaultParams();
    params.stepSec = 0;
    params.period.end = params.period.start;

    const auto issues = ubahn::api::validate(params);
    QCOMPARE(issues.size(), std::size_t{2});
}

QTEST_APPLESS_MAIN(MockApiTest)
#include "tst_MockApi.moc"
