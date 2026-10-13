#include "AppSession.h"

#include <QMetaObject>
#include <QThread>

namespace ubahn::app {

AppSession::AppSession(QObject* parent) : QObject(parent), thread_(new QThread(this)) {
    api::registerMetaTypes();
    controller_ = api::createSimController().release();
    controller_->moveToThread(thread_);

    connect(thread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(controller_, &api::SimController::started, this, &AppSession::started);
    connect(controller_, &api::SimController::chunkReady, this, &AppSession::chunkReady);
    connect(controller_, &api::SimController::summaryReady, this, &AppSession::summaryReady);
    connect(controller_, &api::SimController::error, this, &AppSession::errorOccurred);
    thread_->start();
}

AppSession::~AppSession() {
    thread_->quit();
    thread_->wait();
    controller_ = nullptr;
}

void AppSession::start(const api::SimParams& params) {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_, params] { controller->start(params); },
        Qt::QueuedConnection);
}

void AppSession::setRate(double rate) {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_, rate] { controller->setRate(rate); },
        Qt::QueuedConnection);
}

void AppSession::setStep(int stepSec) {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_, stepSec] { controller->setStep(stepSec); },
        Qt::QueuedConnection);
}

void AppSession::seekForward(api::SimTime target) {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_, target] { controller->seekForward(target); },
        Qt::QueuedConnection);
}

void AppSession::applySchedule(const api::ScheduleConfig& schedule) {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_, schedule] { controller->applySchedule(schedule); },
        Qt::QueuedConnection);
}

void AppSession::stop() {
    QMetaObject::invokeMethod(
        controller_, [controller = controller_] { controller->stop(); }, Qt::QueuedConnection);
}

} // namespace ubahn::app
