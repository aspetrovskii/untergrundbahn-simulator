#include "MainWindow.h"

#include "AppSession.h"
#include "LineView.h"
#include "PlaybackController.h"

#include <QStatusBar>

namespace ubahn::app {

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1000, 650);

    lineView_ = new LineView(this);
    lineView_->setLine(params.line);
    setCentralWidget(lineView_);

    const auto stationCount = params.line.stationNames.size();
    statusBar()->showMessage(tr("Линия настроена: станций — %1").arg(stationCount));

    playback_ = new PlaybackController(this);
    playback_->setRate(params.rate);
    session_ = new AppSession(this);

    connect(session_, &AppSession::started, this, [this](const api::RunInfo& info) {
        statusBar()->showMessage(tr("Запуск с зерном %1").arg(info.seed));
    });
    connect(session_, &AppSession::chunkReady, playback_, &PlaybackController::addChunk);
    connect(playback_, &PlaybackController::stateChanged, this,
            [this] { lineView_->setState(playback_->state(), playback_->exactTime()); });
    connect(playback_, &PlaybackController::timeChanged, this, [this](api::SimTime time) {
        lineView_->setState(playback_->state(), playback_->exactTime());
        statusBar()->showMessage(tr("Модельное время: %1 с").arg(time));
    });
    connect(session_, &AppSession::summaryReady, this, [this](const api::SimSummary&) {
        statusBar()->showMessage(tr("Статические примеры API получены"));
    });
    connect(session_, &AppSession::errorOccurred, this, [this](const api::RuntimeError& error) {
        statusBar()->showMessage(QString::fromStdString(error.message));
    });

    session_->start(params);
}

} // namespace ubahn::app
