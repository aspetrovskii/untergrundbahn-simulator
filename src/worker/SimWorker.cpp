#include "worker/SimWorker.h"

namespace ubahn::worker {

SimWorker::SimWorker(QObject* parent) : api::SimController(parent) {}

void SimWorker::emitInvalidState() {
    // TODO(BE-18): state machine of ALGORITHM 14. Until then every slot is out of place.
    emit error(api::RuntimeError{api::ErrorCode::InvalidState, "not implemented", {}});
}

void SimWorker::start(const api::SimParams&) {
    emitInvalidState();
}

void SimWorker::setRate(double) {
    emitInvalidState();
}

void SimWorker::setStep(int) {
    emitInvalidState();
}

void SimWorker::seekForward(api::SimTime) {
    emitInvalidState();
}

void SimWorker::applySchedule(const api::ScheduleConfig&) {
    emitInvalidState();
}

void SimWorker::stop() {
    emitInvalidState();
}

} // namespace ubahn::worker
