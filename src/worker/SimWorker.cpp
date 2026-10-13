#include "worker/SimWorker.h"

#include "api/Functions.h"
#include "core/Rng.h"

#include <QTimer>

// Qt's slots macro renames EngineState::slots. Keep the public Engine header intact.
#pragma push_macro("slots")
#undef slots
#include "core/Engine.h"
#pragma pop_macro("slots")
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ubahn::worker {

SimWorker::SimWorker(QObject* parent) : api::SimController(parent), timer_(new QTimer(this)) {
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &SimWorker::onTick);
}

SimWorker::~SimWorker() {
    timer_->stop();
}

void SimWorker::emitInvalidState() {
    // The idle-stop text is fixed by tst_worker_smoke.
    emit error(api::RuntimeError{api::ErrorCode::InvalidState, "not implemented", {}});
}

void SimWorker::failInternal() {
    timer_->stop();
    timerArmed_ = false;
    chunkPlaying_ = false;
    summaryOnNextTick_ = false;
    phase_ = Phase::Finished;
    api::RuntimeError err;
    err.code = api::ErrorCode::InternalError;
    if (engine_ != nullptr && engine_->internalError().has_value()) {
        err.message = *engine_->internalError();
    } else {
        err.message = "internal error";
    }
    emit error(err);
}

void SimWorker::finishRun(bool completed) {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        return;
    }
    timer_->stop();
    timerArmed_ = false;
    chunkPlaying_ = false;
    summaryOnNextTick_ = false;
    phase_ = Phase::Finished;
    emit summaryReady(engine_->summary(playhead_, completed));
}

void SimWorker::emitChunk(api::ChunkKind kind, api::SimTime from, api::SimTime to,
                          std::vector<api::ViewDelta> deltas) {
    auto chunk = std::make_shared<api::ViewChunk>();
    chunk->seq = nextSeq_++;
    chunk->kind = kind;
    chunk->from = from;
    chunk->to = to;
    chunk->deltas = std::move(deltas);
    // A sent chunk replaces the within-chunk seek floor (decision R6).
    seekFloor_ = from;
    emit chunkReady(chunk);
}

int SimWorker::playbackMs(api::SimTime from, api::SimTime to) const {
    if (!(rate_ > 0.0) || chunkStep_ <= 0 || to <= from) {
        return 0;
    }
    const double ms =
        (static_cast<double>(to - from) / (rate_ * static_cast<double>(chunkStep_))) * 1000.0;
    auto rounded = static_cast<long long>(std::ceil(ms - 1e-9));
    if (rounded < 1) {
        rounded = 1;
    }
    if (rounded > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

api::SimTime SimWorker::shownModelTime() const {
    if (!chunkPlaying_) {
        return playhead_;
    }
    if (!timerArmed_ || !elapsed_.isValid() || !(rateAtAnchor_ > 0.0) || chunkStep_ <= 0) {
        return shownAtAnchor_;
    }
    const double advanced = (static_cast<double>(elapsed_.elapsed()) / 1000.0) * rateAtAnchor_ *
                            static_cast<double>(chunkStep_);
    api::SimTime shown = shownAtAnchor_ + static_cast<api::SimTime>(std::floor(advanced));
    if (shown < chunkFrom_) {
        shown = chunkFrom_;
    }
    if (shown > chunkTo_) {
        shown = chunkTo_;
    }
    return shown;
}

void SimWorker::retimeChunk(api::SimTime shown) {
    seekFloor_ = shown;
    shownAtAnchor_ = shown;
    rateAtAnchor_ = rate_;
    if (!(rate_ > 0.0) || phase_ != Phase::Running) {
        timer_->stop();
        timerArmed_ = false;
        elapsed_.invalidate();
        return;
    }
    elapsed_.start();
    timer_->start(playbackMs(shown, chunkTo_));
    timerArmed_ = true;
}

void SimWorker::producePlay() {
    if (phase_ != Phase::Running || engine_ == nullptr) {
        return;
    }
    const api::SimTime from = playhead_;
    const api::SimTime to = std::min(from + static_cast<api::SimTime>(stepSec_), periodEnd_);
    if (to <= from) {
        finishRun(true);
        return;
    }
    engine_->runUntil(to, true);
    if (engine_->internalError().has_value()) {
        static_cast<void>(engine_->takeDeltas());
        failInternal();
        return;
    }
    std::vector<api::ViewDelta> deltas = engine_->takeDeltas();
    playhead_ = to;
    chunkFrom_ = from;
    chunkTo_ = to;
    chunkStep_ = stepSec_;
    chunkPlaying_ = true;
    shownAtAnchor_ = from;
    rateAtAnchor_ = rate_;
    elapsed_.start();
    timer_->start(playbackMs(from, to));
    timerArmed_ = true;
    emitChunk(api::ChunkKind::Play, from, to, std::move(deltas));
}

void SimWorker::onTick() {
    timerArmed_ = false;
    if (phase_ != Phase::Running) {
        return;
    }
    // Decision R12: the tick after the last chunk, once that chunk has played out.
    if (summaryOnNextTick_) {
        summaryOnNextTick_ = false;
        finishRun(true);
        return;
    }
    if (chunkPlaying_) {
        chunkPlaying_ = false;
        if (playhead_ >= periodEnd_) {
            finishRun(true);
            return;
        }
    }
    producePlay();
}

void SimWorker::start(const api::SimParams& params) {
    if (phase_ != Phase::Idle && phase_ != Phase::Finished && phase_ != Phase::Running) {
        emitInvalidState();
        return;
    }
    const std::vector<api::ValidationIssue> issues = api::validate(params);
    if (!issues.empty()) {
        emit error(
            api::RuntimeError{api::ErrorCode::InvalidParams, issues.front().message, issues});
        return;
    }

    timer_->stop();
    timerArmed_ = false;
    chunkPlaying_ = false;
    summaryOnNextTick_ = false;
    elapsed_.invalidate();

    const uint64_t seed = params.seed.has_value() ? *params.seed : core::randomSeed();
    auto created = std::make_unique<core::Engine>(params, seed);
    created->runUntil(params.period.start, false);
    if (created->internalError().has_value()) {
        engine_ = std::move(created);
        failInternal();
        return;
    }
    static_cast<void>(created->takeDeltas());

    engine_ = std::move(created);
    stepSec_ = params.stepSec;
    rate_ = params.rate;
    periodEnd_ = params.period.end;
    playhead_ = params.period.start;
    chunkFrom_ = playhead_;
    chunkTo_ = playhead_;
    chunkStep_ = stepSec_;
    nextSeq_ = 0;
    seekFloor_ = playhead_;
    phase_ = rate_ > 0.0 ? Phase::Running : Phase::Paused;

    emit started(engine_->runInfo());
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        return;
    }
    if (engine_->internalError().has_value()) {
        failInternal();
        return;
    }

    emitChunk(api::ChunkKind::Initial, playhead_, playhead_, {engine_->snapshotDelta()});
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        return;
    }
    if (phase_ == Phase::Running) {
        // First Play is the next tick, so start() itself only publishes Initial.
        timer_->start(0);
        timerArmed_ = true;
    }
}

void SimWorker::setRate(double rate) {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        emitInvalidState();
        return;
    }
    if (!(rate >= 0.0)) {
        emitInvalidState();
        return;
    }
    const bool same = rate == rate_ && ((rate == 0.0 && phase_ == Phase::Paused) ||
                                        (rate > 0.0 && phase_ == Phase::Running));
    if (same) {
        return;
    }

    if (chunkPlaying_) {
        // Remainder of the chunk already on screen, measured with QElapsedTimer (ALGORITHM 14).
        const api::SimTime shown = shownModelTime();
        rate_ = rate;
        shownAtAnchor_ = shown;
        rateAtAnchor_ = rate;
        if (rate == 0.0) {
            phase_ = Phase::Paused;
            timer_->stop();
            timerArmed_ = false;
            elapsed_.invalidate();
            return;
        }
        phase_ = Phase::Running;
        elapsed_.start();
        timer_->start(playbackMs(shown, chunkTo_));
        timerArmed_ = true;
        return;
    }

    rate_ = rate;
    if (rate == 0.0) {
        phase_ = Phase::Paused;
        timer_->stop();
        timerArmed_ = false;
        elapsed_.invalidate();
        return;
    }
    phase_ = Phase::Running;
    timer_->start(0);
    timerArmed_ = true;
}

void SimWorker::setStep(int stepSec) {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        emitInvalidState();
        return;
    }
    if (stepSec < 1 || stepSec > 240) {
        emitInvalidState();
        return;
    }
    // SPEC 9.2: the new step applies to the next chunk, not the one on screen.
    stepSec_ = stepSec;
}

void SimWorker::seekForward(api::SimTime target) {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        emitInvalidState();
        return;
    }
    // Decision R6: not after the last sent chunk start (or the previous seek), or past the end.
    if (target <= seekFloor_ || target > periodEnd_) {
        emitInvalidState();
        return;
    }
    if (target <= playhead_) {
        if (!chunkPlaying_) {
            emitInvalidState();
            return;
        }
        // Inside the chunk already sent: the engine stays, only the timer moves (ALGORITHM 14.3).
        retimeChunk(target);
        return;
    }

    timer_->stop();
    timerArmed_ = false;
    chunkPlaying_ = false;
    elapsed_.invalidate();
    engine_->runUntil(target, false);
    if (engine_->internalError().has_value()) {
        static_cast<void>(engine_->takeDeltas());
        failInternal();
        return;
    }
    static_cast<void>(engine_->takeDeltas());
    api::ViewDelta delta = engine_->seekDelta();
    // seekDelta returns a default ViewDelta when nothing changed, with t == 0.
    // A Seek chunk's delta is at the target (API.md 4.4).
    if (delta.trains.empty() && delta.boards.empty() && !delta.clock.has_value() &&
        delta.warnings.empty() && delta.delays.empty()) {
        delta.t = target;
    }
    playhead_ = target;
    chunkFrom_ = target;
    chunkTo_ = target;
    summaryOnNextTick_ = target >= periodEnd_;
    emitChunk(api::ChunkKind::Seek, target, target, {std::move(delta)});
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        return;
    }
    if (phase_ == Phase::Running) {
        // Next Play, or summary when the seek landed on period.end (decision R12).
        timer_->start(0);
        timerArmed_ = true;
    }
}

void SimWorker::applySchedule(const api::ScheduleConfig& schedule) {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        emitInvalidState();
        return;
    }
    // The slot runs between chunks, so the boundary is the engine playhead (ALGORITHM 14.2).
    const std::vector<api::ValidationIssue> issues = engine_->applySchedule(schedule);
    if (!issues.empty()) {
        emit error(
            api::RuntimeError{api::ErrorCode::InvalidParams, issues.front().message, issues});
    }
}

void SimWorker::stop() {
    if (phase_ != Phase::Running && phase_ != Phase::Paused) {
        emitInvalidState();
        return;
    }
    finishRun(false);
}

} // namespace ubahn::worker
