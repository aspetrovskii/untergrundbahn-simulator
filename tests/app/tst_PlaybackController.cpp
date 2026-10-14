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
    playback.addChunk(playChunk(1, 100, 110, 103, 30, true));

    playback.seekForward(105);

    QCOMPARE(playback.currentTime(), ubahn::api::SimTime{105});
    QCOMPARE(playback.state().trains().at(1).lateness, 30);
    QVERIFY(playback.state().delayNotices().empty());
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
