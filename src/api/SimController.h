#pragma once

#include "api/Types.h"

#include <QMetaType>
#include <QObject>
#include <memory>

namespace ubahn::api {

// Lives in the simulation thread. Call slots only through queued connections.
class SimController : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~SimController() override = default;

public slots:
    virtual void start(const ubahn::api::SimParams& params) = 0;
    virtual void setRate(double rate) = 0; // >= 0; 0 pauses
    virtual void setStep(int stepSec) = 0; // 1..240, from the next step
    virtual void seekForward(ubahn::api::SimTime target) = 0;
    virtual void applySchedule(const ubahn::api::ScheduleConfig& schedule) = 0;
    virtual void stop() = 0;

signals:
    void started(const ubahn::api::RunInfo& info);
    void chunkReady(const ubahn::api::ViewChunkPtr& chunk);
    void summaryReady(const ubahn::api::SimSummary& summary);
    void error(const ubahn::api::RuntimeError& error);
};

// Implemented by src/worker. The caller owns the result; move it to the simulation thread
// before connecting and delete it there (deleteLater on QThread::finished).
std::unique_ptr<SimController> createSimController();

// Call once in the GUI thread before connecting signals.
void registerMetaTypes();

} // namespace ubahn::api

Q_DECLARE_METATYPE(ubahn::api::SimParams)
Q_DECLARE_METATYPE(ubahn::api::ScheduleConfig)
Q_DECLARE_METATYPE(ubahn::api::RunInfo)
Q_DECLARE_METATYPE(ubahn::api::ViewChunkPtr)
Q_DECLARE_METATYPE(ubahn::api::SimSummary)
Q_DECLARE_METATYPE(ubahn::api::RuntimeError)
