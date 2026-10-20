#pragma once

#include "api/SimController.h"

#include <QObject>

class QThread;

namespace ubahn::app {

class AppSession : public QObject {
    Q_OBJECT

public:
    explicit AppSession(QObject* parent = nullptr);
    ~AppSession() override;

    AppSession(const AppSession&) = delete;
    AppSession& operator=(const AppSession&) = delete;

    void start(const api::SimParams& params);
    void setRate(double rate);
    void setStep(int stepSec);
    void seekForward(api::SimTime target);
    void applySchedule(const api::ScheduleConfig& schedule);
    void stop();

signals:
    void started(const ubahn::api::RunInfo& info);
    void chunkReady(const ubahn::api::ViewChunkPtr& chunk);
    void summaryReady(const ubahn::api::SimSummary& summary);
    void errorOccurred(const ubahn::api::RuntimeError& error);

private:
    QThread* thread_ = nullptr;
    api::SimController* controller_ = nullptr;
};

} // namespace ubahn::app
