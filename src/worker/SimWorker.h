#pragma once

#include "api/SimController.h"

namespace ubahn::worker {

class SimWorker : public api::SimController {
    Q_OBJECT

public:
    explicit SimWorker(QObject* parent = nullptr);

public slots:
    void start(const api::SimParams& params) override;
    void setRate(double rate) override;
    void setStep(int stepSec) override;
    void seekForward(api::SimTime target) override;
    void applySchedule(const api::ScheduleConfig& schedule) override;
    void stop() override;

private:
    void emitInvalidState();
};

} // namespace ubahn::worker
