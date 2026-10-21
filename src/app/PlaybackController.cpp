#include "PlaybackController.h"

#include <QTimer>
#include <algorithm>
#include <cmath>

namespace ubahn::app {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent), timer_(new QTimer(this)) {
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &PlaybackController::advance);
}

const SimulationState& PlaybackController::state() const {
    return state_;
}

api::SimTime PlaybackController::currentTime() const {
    return static_cast<api::SimTime>(std::floor(currentTime_));
}

double PlaybackController::exactTime() const {
    return currentTime_;
}

double PlaybackController::rate() const {
    return rate_;
}

void PlaybackController::addChunk(const api::ViewChunkPtr& chunk) {
    if (!chunk) {
        return;
    }
    if (chunk->seq != expectedSequence_) {
        emit sequenceError(expectedSequence_, chunk->seq);
        return;
    }
    ++expectedSequence_;

    if (waitingForSeek_) {
        if (chunk->kind == api::ChunkKind::Seek && chunk->to == seekTarget_) {
            pending_.clear();
            active_ = chunk;
            nextDelta_ = 0;
            applyDeltasThrough(chunk->to, false);
            waitingForSeek_ = false;
            completeActiveChunk();
        } else {
            pending_.push_back(chunk);
        }
        return;
    }

    pending_.push_back(chunk);
    startNextChunk();
}

void PlaybackController::setRate(double rate) {
    if (rate < 0.0 || rate == rate_) {
        return;
    }
    if (active_ && active_->kind == api::ChunkKind::Play && rate_ > 0.0) {
        advance();
    }

    rate_ = rate;
    if (rate_ == 0.0) {
        timer_->stop();
        return;
    }
    if (active_ && active_->kind == api::ChunkKind::Play) {
        restartClock();
        timer_->start();
    }
}

void PlaybackController::seekForward(api::SimTime target) {
    if (!hasTime_ || target <= currentTime()) {
        return;
    }

    timer_->stop();
    if (active_ && active_->kind == api::ChunkKind::Play) {
        if (target <= active_->to) {
            applyDeltasThrough(static_cast<double>(target), false);
            if (rate_ > 0.0) {
                restartClock();
                timer_->start();
            }
            return;
        }

        applyDeltasThrough(static_cast<double>(active_->to), false);
        const uint64_t sequence = active_->seq;
        active_.reset();
        nextDelta_ = 0;
        emit chunkFinished(sequence);
    }

    pending_.clear();
    waitingForSeek_ = true;
    seekTarget_ = target;
}

void PlaybackController::reset() {
    timer_->stop();
    state_.clear();
    pending_.clear();
    active_.reset();
    nextDelta_ = 0;
    expectedSequence_ = 0;
    currentTime_ = 0.0;
    clockBaseTime_ = 0.0;
    hasTime_ = false;
    waitingForSeek_ = false;
    seekTarget_ = 0;
}

void PlaybackController::advance() {
    if (!active_ || active_->kind != api::ChunkKind::Play || rate_ <= 0.0 || !elapsed_.isValid()) {
        return;
    }

    const double realSeconds = static_cast<double>(elapsed_.nsecsElapsed()) / 1'000'000'000.0;
    const double chunkDuration = static_cast<double>(active_->to - active_->from);
    const double target = std::min(static_cast<double>(active_->to),
                                   clockBaseTime_ + realSeconds * rate_ * chunkDuration);
    applyDeltasThrough(target, true);

    if (target >= static_cast<double>(active_->to)) {
        completeActiveChunk();
    }
}

void PlaybackController::startNextChunk() {
    if (active_ || waitingForSeek_ || pending_.empty()) {
        return;
    }

    active_ = pending_.front();
    pending_.pop_front();
    nextDelta_ = 0;

    if (active_->kind == api::ChunkKind::Initial || active_->kind == api::ChunkKind::Seek) {
        applyDeltasThrough(static_cast<double>(active_->to), false);
        completeActiveChunk();
        return;
    }

    setCurrentTime(static_cast<double>(active_->from));
    if (rate_ > 0.0) {
        restartClock();
        timer_->start();
    }
}

void PlaybackController::applyDeltasThrough(double target, bool includeDelayNotices) {
    if (!active_) {
        return;
    }
    bool changed = false;
    std::vector<api::DelayNotice> notices;
    while (nextDelta_ < active_->deltas.size() &&
           static_cast<double>(active_->deltas[nextDelta_].t) <= target) {
        const auto& delta = active_->deltas[nextDelta_];
        state_.apply(delta, includeDelayNotices);
        if (includeDelayNotices) {
            notices.insert(notices.end(), delta.delays.begin(), delta.delays.end());
        }
        changed = true;
        ++nextDelta_;
    }
    // Publish only after the entire batch and its clock are consistent. Notices have a
    // separate payload because SimulationState retains only the last delta's notices.
    setCurrentTime(target, changed);
    if (!notices.empty()) {
        emit delayNoticesReady(notices);
    }
}

void PlaybackController::completeActiveChunk() {
    if (!active_) {
        return;
    }
    timer_->stop();
    const uint64_t sequence = active_->seq;
    active_.reset();
    nextDelta_ = 0;
    emit chunkFinished(sequence);
    startNextChunk();
}

void PlaybackController::setCurrentTime(double time, bool changed) {
    const api::SimTime previous = currentTime();
    const bool needsFrame = !hasTime_ || currentTime_ != time || changed;
    currentTime_ = time;
    hasTime_ = true;
    const api::SimTime current = currentTime();
    if (changed) {
        emit stateChanged();
    }
    if (current != previous) {
        emit timeChanged(current);
    }
    if (needsFrame) {
        emit frameReady(time, changed);
    }
}

void PlaybackController::restartClock() {
    clockBaseTime_ = currentTime_;
    elapsed_.restart();
}

} // namespace ubahn::app
