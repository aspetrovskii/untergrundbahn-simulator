#include "PlaybackController.h"

#include <QSignalSpy>
#include <QTest>
#include <memory>

namespace {

ubahn::api::ViewChunkPtr initialChunk(ubahn::api::SimTime time) {
    auto chunk = std::make_shared<ubahn::api::ViewChunk>();
    chunk->seq = 0;
    chunk->kind = ubahn::api::ChunkKind::Initial;
    chunk->from = time;
    chunk->to = time;

    ubahn::api::ViewDelta delta;
    delta.t = time;
    ubahn::api::TrainUpdate train;
    train.id = 1;
    train.edge = ubahn::api::TrackEdge::Platform;
    delta.trains.push_back(train);
    chunk->deltas.push_back(delta);
    return chunk;
}

ubahn::api::ViewChunkPtr playChunk(uint64_t sequence, ubahn::api::SimTime from,
                                   ubahn::api::SimTime to, ubahn::api::SimTime deltaTime,
                                   int lateness, bool withDelay = false) {
    auto chunk = std::make_shared<ubahn::api::ViewChunk>();
    chunk->seq = sequence;
    chunk->kind = ubahn::api::ChunkKind::Play;
    chunk->from = from;
    chunk->to = to;

    ubahn::api::ViewDelta delta;
    delta.t = deltaTime;
    ubahn::api::TrainUpdate train;
    train.id = 1;
    train.lateness = lateness;
    train.edge = ubahn::api::TrackEdge::Span;
    delta.trains.push_back(train);
    if (withDelay) {
        delta.delays.push_back({1, ubahn::api::Dir::I, 2, 30});
    }
    chunk->deltas.push_back(delta);
    return chunk;
}

ubahn::api::ViewChunkPtr seekChunk(uint64_t sequence, ubahn::api::SimTime target) {
    auto chunk = std::make_shared<ubahn::api::ViewChunk>();
    chunk->seq = sequence;
    chunk->kind = ubahn::api::ChunkKind::Seek;
    chunk->from = target;
    chunk->to = target;

    ubahn::api::ViewDelta delta;
    delta.t = target;
    ubahn::api::TrainUpdate train;
    train.id = 1;
    train.lateness = 99;
    delta.trains.push_back(train);
    chunk->deltas.push_back(delta);
    return chunk;
}

} // namespace

class PlaybackControllerTest : public QObject {
    Q_OBJECT

private slots:
    void appliesAbsoluteState();
    void playsChunksInOrder();
    void batchesDeltasAndKeepsAllNotices();
    void emitsFramesBetweenWholeSeconds();
    void pauseFreezesTime();
    void seekInsideChunkSkipsNotices();
    void seekBeyondChunkWaitsForSnapshot();
    void rejectsBrokenSequence();
};

void PlaybackControllerTest::appliesAbsoluteState() {
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta first;
    first.trains.push_back({});
    first.trains.front().id = 4;
    first.trains.front().lateness = 10;
    state.apply(first);

    ubahn::api::ViewDelta second;
    second.trains.push_back(first.trains.front());
    second.trains.front().lateness = 25;
    state.apply(second);

    QCOMPARE(state.trains().size(), std::size_t{1});
    QCOMPARE(state.trains().at(4).lateness, 25);
}

void PlaybackControllerTest::playsChunksInOrder() {
    ubahn::app::PlaybackController playback;
    playback.setRate(100.0);
    QSignalSpy finishedSpy(&playback, &ubahn::app::PlaybackController::chunkFinished);

    playback.addChunk(initialChunk(100));
    playback.addChunk(playChunk(1, 100, 110, 105, 20));
    playback.addChunk(playChunk(2, 110, 120, 115, 40));

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 500);
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{120});
    QCOMPARE(playback.state().trains().at(1).lateness, 40);
}

void PlaybackControllerTest::batchesDeltasAndKeepsAllNotices() {
    ubahn::app::PlaybackController playback;
    playback.setRate(1'000'000.0);
    playback.addChunk(initialChunk(100));
    auto chunk = std::make_shared<ubahn::api::ViewChunk>();
    chunk->seq = 1;
    chunk->kind = ubahn::api::ChunkKind::Play;
    chunk->from = 100;
    chunk->to = 110;
    for (int index = 1; index <= 3; ++index) {
        ubahn::api::ViewDelta delta;
        delta.t = 100 + index;
        ubahn::api::TrainUpdate train;
        train.id = 1;
        train.lateness = index * 10;
        delta.trains.push_back(train);
        ubahn::api::Warning warning;
        warning.message = std::to_string(index);
        delta.warnings.push_back(warning);
        if (index < 3) {
            delta.delays.push_back({index, ubahn::api::Dir::I, index, index * 10});
        }
        chunk->deltas.push_back(delta);
    }
    QSignalSpy stateSpy(&playback, &ubahn::app::PlaybackController::stateChanged);
    QSignalSpy frameSpy(&playback, &ubahn::app::PlaybackController::frameReady);
    std::vector<ubahn::api::DelayNotice> notices;
    int noticeBatches = 0;
    connect(&playback, &ubahn::app::PlaybackController::delayNoticesReady, this,
            [&](const auto& batch) {
                ++noticeBatches;
                notices.insert(notices.end(), batch.begin(), batch.end());
            });
    connect(&playback, &ubahn::app::PlaybackController::stateChanged, this, [&] {
        QCOMPARE(playback.currentTime(), ubahn::api::SimTime{110});
        QCOMPARE(playback.state().trains().at(1).lateness, 30);
    });

    playback.addChunk(chunk);

    QTRY_COMPARE_WITH_TIMEOUT(playback.currentTime(), ubahn::api::SimTime{110}, 500);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(frameSpy.count(), 1);
    QCOMPARE(frameSpy.at(0).at(1).toBool(), true);
    QCOMPARE(playback.state().trainUpdateTime(1), ubahn::api::SimTime{103});
    QCOMPARE(playback.state().warnings().size(), std::size_t{3});
    for (int index = 0; index < 3; ++index) {
        QCOMPARE(playback.state().warnings()[static_cast<std::size_t>(index)].message,
                 std::to_string(index + 1));
    }
    QCOMPARE(noticeBatches, 1);
    QCOMPARE(notices.size(), std::size_t{2});
    QCOMPARE(notices[0].trainId, 1);
    QCOMPARE(notices[1].trainId, 2);
    QVERIFY(playback.state().delayNotices().empty());
}

void PlaybackControllerTest::emitsFramesBetweenWholeSeconds() {
    ubahn::app::PlaybackController playback;
    playback.setRate(0.01);
    playback.addChunk(initialChunk(100));
    QSignalSpy frameSpy(&playback, &ubahn::app::PlaybackController::frameReady);
    QSignalSpy timeSpy(&playback, &ubahn::app::PlaybackController::timeChanged);
    QSignalSpy stateSpy(&playback, &ubahn::app::PlaybackController::stateChanged);
    playback.addChunk(playChunk(1, 100, 110, 105, 20));

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() >= 2, 500);
    QVERIFY(playback.exactTime() > 100.0);
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{100});
    QCOMPARE(timeSpy.count(), 0);
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(frameSpy.at(0).at(1).toBool(), false);

    playback.setRate(0.0);
    const int pausedFrames = frameSpy.count();
    const double pausedTime = playback.exactTime();
    QTest::qWait(40);
    QCOMPARE(frameSpy.count(), pausedFrames);
    QCOMPARE(playback.exactTime(), pausedTime);
}

void PlaybackControllerTest::pauseFreezesTime() {
    ubahn::app::PlaybackController playback;
    playback.setRate(0.0);
    playback.addChunk(initialChunk(100));
    playback.addChunk(playChunk(1, 100, 110, 105, 20));

    QTest::qWait(50);
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{100});

    playback.setRate(100.0);
    QTRY_COMPARE_WITH_TIMEOUT(playback.currentTime(), ubahn::api::SimTime{110}, 500);
}

void PlaybackControllerTest::seekInsideChunkSkipsNotices() {
    ubahn::app::PlaybackController playback;
    playback.setRate(0.0);
    playback.addChunk(initialChunk(100));
    auto chunk = std::make_shared<ubahn::api::ViewChunk>(*playChunk(1, 100, 110, 103, 30, true));
    auto second = chunk->deltas.front();
    second.t = 104;
    second.trains.front().lateness = 40;
    chunk->deltas.push_back(second);
    auto future = second;
    future.t = 109;
    future.trains.front().lateness = 90;
    chunk->deltas.push_back(future);
    playback.addChunk(chunk);
    QSignalSpy stateSpy(&playback, &ubahn::app::PlaybackController::stateChanged);
    int notices = 0;
    connect(&playback, &ubahn::app::PlaybackController::delayNoticesReady, this,
            [&](const auto&) { ++notices; });

    playback.seekForward(105);

    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{105});
    QCOMPARE(playback.state().trains().at(1).lateness, 40);
    QCOMPARE(stateSpy.count(), 1);
    QVERIFY(playback.state().delayNotices().empty());
    QCOMPARE(notices, 0);

    playback.seekForward(110);
    QCOMPARE(playback.state().trains().at(1).lateness, 90);
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{110});
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(notices, 0);
}

void PlaybackControllerTest::seekBeyondChunkWaitsForSnapshot() {
    ubahn::app::PlaybackController playback;
    playback.setRate(0.0);
    playback.addChunk(initialChunk(100));
    playback.addChunk(playChunk(1, 100, 110, 105, 20));

    playback.seekForward(200);
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{110});

    playback.addChunk(seekChunk(2, 200));
    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{200});
    QCOMPARE(playback.state().trains().at(1).lateness, 99);
}

void PlaybackControllerTest::rejectsBrokenSequence() {
    ubahn::app::PlaybackController playback;
    QSignalSpy errorSpy(&playback, &ubahn::app::PlaybackController::sequenceError);

    playback.addChunk(playChunk(2, 100, 110, 105, 20));

    QCOMPARE(errorSpy.count(), 1);
}

QTEST_MAIN(PlaybackControllerTest)
#include "tst_PlaybackController.moc"
