#pragma once

#include "SimulationState.h"
#include "api/Types.h"

#include <QElapsedTimer>
#include <QObject>
#include <deque>

class QTimer;

namespace ubahn::app {

class PlaybackController : public QObject {
    Q_OBJECT

public:
    explicit PlaybackController(QObject* parent = nullptr);

    const SimulationState& state() const;
    api::SimTime currentTime() const;
    double exactTime() const;
    double rate() const;

public slots:
    void addChunk(const api::ViewChunkPtr& chunk);
    void setRate(double rate);
    void seekForward(api::SimTime target);
    void reset();

signals:
    void timeChanged(ubahn::api::SimTime time);
    void stateChanged();
    void frameReady(double modelTime, bool stateChanged);
    void delayNoticesReady(const std::vector<ubahn::api::DelayNotice>& notices);
    void chunkFinished(uint64_t sequence);
    void sequenceError(uint64_t expected, uint64_t received);

private slots:
    void advance();

private:
    void startNextChunk();
    void applyDeltasThrough(double target, bool includeDelayNotices);
    void completeActiveChunk();
    void setCurrentTime(double time, bool stateChanged = false);
    void restartClock();

    SimulationState state_;
    QTimer* timer_ = nullptr;
    QElapsedTimer elapsed_;
    std::deque<api::ViewChunkPtr> pending_;
    api::ViewChunkPtr active_;
    std::size_t nextDelta_ = 0;
    uint64_t expectedSequence_ = 0;
    double currentTime_ = 0.0;
    double clockBaseTime_ = 0.0;
    double rate_ = 1.0;
    bool hasTime_ = false;
    bool waitingForSeek_ = false;
    api::SimTime seekTarget_ = 0;
};

} // namespace ubahn::app
