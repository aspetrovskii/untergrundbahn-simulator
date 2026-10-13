#pragma once

#include "api/SimController.h"

#include <QElapsedTimer>
#include <cstdint>
#include <memory>
#include <vector>

class QTimer;

namespace ubahn::core {
class Engine;
}

namespace ubahn::worker {

// ALGORITHM 14. The model is touched only on this object's thread.
class SimWorker : public api::SimController {
    Q_OBJECT

public:
    explicit SimWorker(QObject* parent = nullptr);
    ~SimWorker() override;

public slots:
    void start(const api::SimParams& params) override;
    void setRate(double rate) override;
    void setStep(int stepSec) override;
    void seekForward(api::SimTime target) override;
    void applySchedule(const api::ScheduleConfig& schedule) override;
    void stop() override;

private slots:
    void onTick();

private:
    enum class Phase { Idle, Running, Paused, Finished };

    void emitInvalidState();
    void failInternal();
    void finishRun(bool completed);
    void emitChunk(api::ChunkKind kind, api::SimTime from, api::SimTime to,
                   std::vector<api::ViewDelta> deltas);
    void producePlay();
    void retimeChunk(api::SimTime shown);
    int playbackMs(api::SimTime from, api::SimTime to) const;
    api::SimTime shownModelTime() const;

    Phase phase_ = Phase::Idle;
    std::unique_ptr<core::Engine> engine_;
    api::SimTime playhead_ = 0;
    api::SimTime periodEnd_ = 0;
    // Exclusive lower bound for seekForward (decision R6).
    api::SimTime seekFloor_ = 0;
    api::SimTime chunkFrom_ = 0;
    api::SimTime chunkTo_ = 0;
    api::SimTime shownAtAnchor_ = 0;
    int32_t stepSec_ = 60;
    int32_t chunkStep_ = 60;
    double rate_ = 0;
    double rateAtAnchor_ = 0;
    uint64_t nextSeq_ = 0;
    bool chunkPlaying_ = false;
    bool timerArmed_ = false;
    // Seek landed on period.end: summary waits for the next tick (decision R12).
    bool summaryOnNextTick_ = false;
    QTimer* timer_ = nullptr;
    QElapsedTimer elapsed_;
};

} // namespace ubahn::worker
