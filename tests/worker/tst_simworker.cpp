#include "api/Functions.h"
#include "api/SimController.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr ubahn::api::SimTime kWarmup = 3 * 3600;

ubahn::api::SimParams shortRun(ubahn::api::SimTime seconds, int32_t step, double rate) {
    // Period starts at the warmup instant, so start() does not replay the night.
    ubahn::api::SimParams params = ubahn::api::defaultParams();
    params.period.start = kWarmup;
    params.period.end = kWarmup + seconds;
    params.stepSec = step;
    params.rate = rate;
    params.seed = 42;
    for (ubahn::api::DayProfile& day : params.schedule.days) {
        for (ubahn::api::Window& window : day.windows) {
            window.delayProb = 0;
        }
    }
    return params;
}

ubahn::api::RuntimeError errorAt(const QSignalSpy& spy, int index) {
    return spy.at(index).at(0).value<ubahn::api::RuntimeError>();
}

ubahn::api::ViewChunkPtr chunkAt(const QSignalSpy& spy, int index) {
    return spy.at(index).at(0).value<ubahn::api::ViewChunkPtr>();
}

bool waitCount(QSignalSpy& spy, int count, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (spy.count() < count) {
        const int left = timeoutMs - static_cast<int>(timer.elapsed());
        if (left <= 0) {
            return false;
        }
        if (!spy.wait(left) && spy.count() < count) {
            return false;
        }
    }
    return true;
}

bool deltasInOrder(const ubahn::api::ViewChunk& chunk) {
    ubahn::api::SimTime previous = chunk.from;
    for (const ubahn::api::ViewDelta& delta : chunk.deltas) {
        if (chunk.kind == ubahn::api::ChunkKind::Play) {
            if (!(delta.t > previous && delta.t <= chunk.to)) {
                return false;
            }
        } else if (delta.t != chunk.to) {
            return false;
        }
        previous = delta.t;
    }
    return true;
}

} // namespace

class SimWorkerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void idleCommandsEmitInvalidState();
    void invalidParamsDoNotStart();
    void pausedStartEmitsInitialWithoutPlay();
    void setRateEmitsOrderedPlayChunks();
    void setStepChangesNextChunk();
    void seekPastPlayheadThenPlay();
    void seekNotAfterFloorOrPastEndIsInvalid();
    void seekInsideChunkRaisesFloor();
    void seekToEndSummarizesOnNextTick();
    void applyScheduleErrorKeepsPlaying();
    void stopEmitsIncompleteSummaryOnce();
    void periodEndSummarizesAfterLastChunk();
    void sameSeedProducesSameChunks();
    void restartWhileRunningResetsSequence();
    void startWhilePausedIsInvalid();
};

void SimWorkerTest::init() {
    ubahn::api::registerMetaTypes();
}

void SimWorkerTest::idleCommandsEmitInvalidState() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);
    QSignalSpy started(ctrl.get(), &ubahn::api::SimController::started);
    QVERIFY(errors.isValid());

    const ubahn::api::SimParams params = shortRun(120, 60, 0);
    ctrl->setRate(1);
    ctrl->setStep(30);
    ctrl->seekForward(params.period.start + 1);
    ctrl->applySchedule(params.schedule);
    ctrl->stop();

    QCOMPARE(errors.count(), 5);
    QCOMPARE(started.count(), 0);
    for (int i = 0; i < errors.count(); ++i) {
        const ubahn::api::RuntimeError err = errorAt(errors, i);
        QVERIFY(err.code == ubahn::api::ErrorCode::InvalidState);
        QCOMPARE(QString::fromStdString(err.message), QString("not implemented"));
    }
}

void SimWorkerTest::invalidParamsDoNotStart() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);
    QSignalSpy started(ctrl.get(), &ubahn::api::SimController::started);

    ubahn::api::SimParams params = shortRun(120, 60, 0);
    params.rate = -1;
    ctrl->start(params);

    QCOMPARE(started.count(), 0);
    QCOMPARE(errors.count(), 1);
    const ubahn::api::RuntimeError err = errorAt(errors, 0);
    QVERIFY(err.code == ubahn::api::ErrorCode::InvalidParams);
    QVERIFY(!err.issues.empty());

    ctrl->stop();
    QCOMPARE(errors.count(), 2);
    QVERIFY(errorAt(errors, 1).code == ubahn::api::ErrorCode::InvalidState);
}

void SimWorkerTest::pausedStartEmitsInitialWithoutPlay() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy started(ctrl.get(), &ubahn::api::SimController::started);
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    const ubahn::api::SimParams params = shortRun(180, 60, 0);
    ctrl->start(params);

    QCOMPARE(errors.count(), 0);
    QCOMPARE(started.count(), 1);
    const auto info = started.at(0).at(0).value<ubahn::api::RunInfo>();
    QCOMPARE(info.seed, static_cast<uint64_t>(42));
    QCOMPARE(info.period.start, params.period.start);
    QCOMPARE(info.period.end, params.period.end);

    QCOMPARE(chunks.count(), 1);
    const ubahn::api::ViewChunkPtr initial = chunkAt(chunks, 0);
    QCOMPARE(initial->seq, static_cast<uint64_t>(0));
    QVERIFY(initial->kind == ubahn::api::ChunkKind::Initial);
    QCOMPARE(initial->from, params.period.start);
    QCOMPARE(initial->to, params.period.start);
    QCOMPARE(initial->deltas.size(), static_cast<size_t>(1));
    const ubahn::api::ViewDelta& delta = initial->deltas.front();
    QCOMPARE(delta.t, params.period.start);
    QCOMPARE(delta.trains.size(), static_cast<size_t>(params.line.fleetSize));
    QCOMPARE(delta.boards.size(), static_cast<size_t>(params.line.stationNames.size() * 2));
    QVERIFY(delta.clock.has_value());

    QTest::qWait(40);
    QCOMPARE(chunks.count(), 1);
}

void SimWorkerTest::setRateEmitsOrderedPlayChunks() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    const ubahn::api::SimParams params = shortRun(180, 60, 0);
    ctrl->start(params);
    QCOMPARE(chunks.count(), 1);

    bool paused = false;
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (!paused && chunk->kind == ubahn::api::ChunkKind::Play) {
                    paused = true;
                    ctrl->setRate(0);
                } else if (chunk->seq == 2) {
                    ctrl->stop();
                }
            });

    ctrl->setRate(100);
    QVERIFY(waitCount(chunks, 2, 2000));
    QTest::qWait(80);
    QCOMPARE(chunks.count(), 2);
    QCOMPARE(errors.count(), 0);

    const ubahn::api::ViewChunkPtr play = chunkAt(chunks, 1);
    QCOMPARE(play->seq, static_cast<uint64_t>(1));
    QVERIFY(play->kind == ubahn::api::ChunkKind::Play);
    QCOMPARE(play->from, chunkAt(chunks, 0)->to);
    QCOMPARE(play->to - play->from, static_cast<ubahn::api::SimTime>(60));
    QVERIFY(deltasInOrder(*play));

    ctrl->setRate(100);
    QVERIFY(waitCount(chunks, 3, 2000));
    const ubahn::api::ViewChunkPtr next = chunkAt(chunks, 2);
    QCOMPARE(next->seq, static_cast<uint64_t>(2));
    QCOMPARE(next->from, play->to);
    QVERIFY(next->kind == ubahn::api::ChunkKind::Play);
    QVERIFY(deltasInOrder(*next));
    ctrl->stop();
}

void SimWorkerTest::setStepChangesNextChunk() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    ctrl->start(shortRun(200, 60, 100));
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind != ubahn::api::ChunkKind::Play) {
                    return;
                }
                if (chunk->seq == 1) {
                    ctrl->setStep(0);
                    ctrl->setStep(30);
                } else if (chunk->seq == 2) {
                    ctrl->stop();
                }
            });

    QVERIFY(waitCount(chunks, 3, 2000));
    QCOMPARE(errors.count(), 1);
    QVERIFY(errorAt(errors, 0).code == ubahn::api::ErrorCode::InvalidState);

    const ubahn::api::ViewChunkPtr first = chunkAt(chunks, 1);
    const ubahn::api::ViewChunkPtr second = chunkAt(chunks, 2);
    QCOMPARE(first->to - first->from, static_cast<ubahn::api::SimTime>(60));
    QCOMPARE(second->from, first->to);
    QCOMPARE(second->to - second->from, static_cast<ubahn::api::SimTime>(30));
    QVERIFY(deltasInOrder(*first));
    QVERIFY(deltasInOrder(*second));
}

void SimWorkerTest::seekPastPlayheadThenPlay() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    const ubahn::api::SimParams params = shortRun(400, 60, 100);
    const ubahn::api::SimTime target = params.period.start + 150;
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind == ubahn::api::ChunkKind::Play && chunk->seq == 1) {
                    ctrl->seekForward(target);
                } else if (chunk->kind == ubahn::api::ChunkKind::Play && chunk->from == target) {
                    ctrl->stop();
                }
            });
    ctrl->start(params);

    QVERIFY(waitCount(chunks, 4, 2000));
    QCOMPARE(errors.count(), 0);

    const ubahn::api::ViewChunkPtr seek = chunkAt(chunks, 2);
    QVERIFY(seek->kind == ubahn::api::ChunkKind::Seek);
    QCOMPARE(seek->from, target);
    QCOMPARE(seek->to, target);
    QCOMPARE(seek->deltas.size(), static_cast<size_t>(1));
    QCOMPARE(seek->deltas.front().t, target);

    const ubahn::api::ViewChunkPtr play = chunkAt(chunks, 3);
    QVERIFY(play->kind == ubahn::api::ChunkKind::Play);
    QCOMPARE(play->from, target);
    QCOMPARE(play->seq, seek->seq + 1);
    QVERIFY(deltasInOrder(*play));
}

void SimWorkerTest::seekNotAfterFloorOrPastEndIsInvalid() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    const ubahn::api::SimParams params = shortRun(300, 60, 0);
    ctrl->start(params);
    ctrl->seekForward(params.period.start);
    ctrl->seekForward(params.period.start - 1);
    ctrl->seekForward(params.period.end + 1);

    QCOMPARE(chunks.count(), 1);
    QCOMPARE(errors.count(), 3);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(errorAt(errors, i).code == ubahn::api::ErrorCode::InvalidState);
    }

    ctrl->seekForward(params.period.start + 10);
    QCOMPARE(errors.count(), 3);
    QCOMPARE(chunks.count(), 2);
    QVERIFY(chunkAt(chunks, 1)->kind == ubahn::api::ChunkKind::Seek);
    ctrl->stop();
}

void SimWorkerTest::seekInsideChunkRaisesFloor() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    ctrl->start(shortRun(180, 60, 100));
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind == ubahn::api::ChunkKind::Play && chunk->seq == 1) {
                    ctrl->seekForward(chunk->from + 1);
                    ctrl->seekForward(chunk->from);
                    ctrl->stop();
                }
            });

    QVERIFY(waitCount(chunks, 2, 2000));
    QCOMPARE(chunks.count(), 2);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errorAt(errors, 0).code == ubahn::api::ErrorCode::InvalidState);
    QVERIFY(chunkAt(chunks, 1)->kind == ubahn::api::ChunkKind::Play);
}

void SimWorkerTest::seekToEndSummarizesOnNextTick() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);

    const ubahn::api::SimParams params = shortRun(90, 60, 100);
    int summaryDuringSeek = -1;
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind == ubahn::api::ChunkKind::Seek) {
                    summaryDuringSeek = summary.count();
                }
            });
    ctrl->start(params);
    ctrl->seekForward(params.period.end);

    QCOMPARE(summary.count(), 0);
    QCOMPARE(chunks.count(), 2);
    QVERIFY(chunkAt(chunks, 1)->kind == ubahn::api::ChunkKind::Seek);
    QCOMPARE(chunkAt(chunks, 1)->to, params.period.end);

    QVERIFY(summary.wait(2000));
    QCOMPARE(summaryDuringSeek, 0);
    QCOMPARE(summary.count(), 1);
    const auto done = summary.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(done.completed);
    QCOMPARE(done.stoppedAt, params.period.end);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(chunks.count(), 2);

    QTest::qWait(30);
    QCOMPARE(summary.count(), 1);
}

void SimWorkerTest::applyScheduleErrorKeepsPlaying() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);
    QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);

    const ubahn::api::SimParams params = shortRun(180, 60, 100);
    ubahn::api::ScheduleConfig broken = params.schedule;
    broken.days[0].windows[0].headway = 10;

    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind == ubahn::api::ChunkKind::Play && chunk->seq == 1) {
                    ctrl->applySchedule(broken);
                }
            });
    ctrl->start(params);

    QVERIFY(summary.wait(3000));
    QCOMPARE(errors.count(), 1);
    const ubahn::api::RuntimeError err = errorAt(errors, 0);
    QVERIFY(err.code == ubahn::api::ErrorCode::InvalidParams);
    QVERIFY(!err.issues.empty());
    QVERIFY(chunks.count() >= 3);
    const auto done = summary.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(done.completed);
    QCOMPARE(chunkAt(chunks, chunks.count() - 1)->to, params.period.end);
}

void SimWorkerTest::stopEmitsIncompleteSummaryOnce() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);

    const ubahn::api::SimParams params = shortRun(180, 60, 100);
    ctrl->start(params);
    QVERIFY(waitCount(chunks, 2, 2000));
    ctrl->stop();

    QCOMPARE(summary.count(), 1);
    const auto done = summary.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(!done.completed);
    QCOMPARE(done.stoppedAt, chunkAt(chunks, chunks.count() - 1)->to);

    QTest::qWait(40);
    QCOMPARE(summary.count(), 1);
    ctrl->stop();
    QCOMPARE(errors.count(), 1);
    QVERIFY(errorAt(errors, 0).code == ubahn::api::ErrorCode::InvalidState);
}

void SimWorkerTest::periodEndSummarizesAfterLastChunk() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);

    const ubahn::api::SimParams params = shortRun(100, 60, 100);
    int summaryDuringLast = -1;
    connect(ctrl.get(), &ubahn::api::SimController::chunkReady, ctrl.get(),
            [&](const ubahn::api::ViewChunkPtr& chunk) {
                if (chunk->kind == ubahn::api::ChunkKind::Play && chunk->to == params.period.end) {
                    summaryDuringLast = summary.count();
                }
            });
    ctrl->start(params);

    QVERIFY(summary.wait(3000));
    QCOMPARE(summaryDuringLast, 0);
    QCOMPARE(summary.count(), 1);
    const auto done = summary.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(done.completed);
    QCOMPARE(done.stoppedAt, params.period.end);

    const ubahn::api::ViewChunkPtr last = chunkAt(chunks, chunks.count() - 1);
    QVERIFY(last->kind == ubahn::api::ChunkKind::Play);
    QCOMPARE(last->to, params.period.end);
    ubahn::api::SimTime cursor = params.period.start;
    for (int i = 0; i < chunks.count(); ++i) {
        const ubahn::api::ViewChunkPtr chunk = chunkAt(chunks, i);
        QCOMPARE(chunk->seq, static_cast<uint64_t>(i));
        if (i == 0) {
            QVERIFY(chunk->kind == ubahn::api::ChunkKind::Initial);
            QCOMPARE(chunk->from, cursor);
            QCOMPARE(chunk->to, cursor);
        } else {
            QVERIFY(chunk->kind == ubahn::api::ChunkKind::Play);
            QCOMPARE(chunk->from, cursor);
            QVERIFY(chunk->to > chunk->from);
            cursor = chunk->to;
        }
        QVERIFY(deltasInOrder(*chunk));
    }
    QCOMPARE(cursor, params.period.end);

    QTest::qWait(30);
    QCOMPARE(summary.count(), 1);
}

void SimWorkerTest::sameSeedProducesSameChunks() {
    const ubahn::api::SimParams params = shortRun(180, 60, 200);

    auto collect = [&]() {
        std::vector<ubahn::api::ViewChunk> chunks;
        const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
        QSignalSpy spy(ctrl.get(), &ubahn::api::SimController::chunkReady);
        QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);
        ctrl->start(params);
        const bool done = summary.wait(3000);
        for (int i = 0; i < spy.count(); ++i) {
            chunks.push_back(*chunkAt(spy, i));
        }
        return std::pair<bool, std::vector<ubahn::api::ViewChunk>>{done, std::move(chunks)};
    };

    const auto first = collect();
    const auto second = collect();
    QVERIFY(first.first);
    QVERIFY(second.first);
    QCOMPARE(first.second.size(), second.second.size());
    QVERIFY(first.second.size() >= 2);
    for (size_t i = 0; i < first.second.size(); ++i) {
        const ubahn::api::ViewChunk& left = first.second[i];
        const ubahn::api::ViewChunk& right = second.second[i];
        QCOMPARE(left.seq, right.seq);
        QVERIFY(left.kind == right.kind);
        QCOMPARE(left.from, right.from);
        QCOMPARE(left.to, right.to);
        QCOMPARE(left.deltas.size(), right.deltas.size());
        for (size_t d = 0; d < left.deltas.size(); ++d) {
            QCOMPARE(left.deltas[d].t, right.deltas[d].t);
            QCOMPARE(left.deltas[d].trains.size(), right.deltas[d].trains.size());
            QCOMPARE(left.deltas[d].boards.size(), right.deltas[d].boards.size());
            QCOMPARE(left.deltas[d].delays.size(), right.deltas[d].delays.size());
            QCOMPARE(left.deltas[d].warnings.size(), right.deltas[d].warnings.size());
        }
    }
}

void SimWorkerTest::restartWhileRunningResetsSequence() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy started(ctrl.get(), &ubahn::api::SimController::started);
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);
    QSignalSpy summary(ctrl.get(), &ubahn::api::SimController::summaryReady);

    const ubahn::api::SimParams params = shortRun(180, 60, 100);
    ctrl->start(params);
    ctrl->start(params);

    QCOMPARE(summary.count(), 0);
    QCOMPARE(started.count(), 2);
    QCOMPARE(chunks.count(), 2);
    QCOMPARE(chunkAt(chunks, 0)->seq, static_cast<uint64_t>(0));
    QCOMPARE(chunkAt(chunks, 1)->seq, static_cast<uint64_t>(0));
    QVERIFY(chunkAt(chunks, 1)->kind == ubahn::api::ChunkKind::Initial);
    ctrl->stop();
    QCOMPARE(summary.count(), 1);
}

void SimWorkerTest::startWhilePausedIsInvalid() {
    const std::unique_ptr<ubahn::api::SimController> ctrl = ubahn::api::createSimController();
    QSignalSpy errors(ctrl.get(), &ubahn::api::SimController::error);
    QSignalSpy started(ctrl.get(), &ubahn::api::SimController::started);
    QSignalSpy chunks(ctrl.get(), &ubahn::api::SimController::chunkReady);

    ctrl->start(shortRun(120, 60, 0));
    ctrl->start(shortRun(120, 60, 0));

    QCOMPARE(started.count(), 1);
    QCOMPARE(chunks.count(), 1);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errorAt(errors, 0).code == ubahn::api::ErrorCode::InvalidState);
    ctrl->stop();
}

QTEST_GUILESS_MAIN(SimWorkerTest)

#include "tst_simworker.moc"
