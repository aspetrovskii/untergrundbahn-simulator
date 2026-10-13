#include "MainWindow.h"

#include "AppSession.h"
#include "PlaybackController.h"

#include <QLabel>
#include <QStatusBar>

namespace ubahn::app {

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1000, 650);

    placeholder_ = new QLabel(tr("Подготовка статических данных…"), this);
    placeholder_->setObjectName("mainWindowPlaceholder");
    placeholder_->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder_);

    const auto stationCount = params.line.stationNames.size();
    statusBar()->showMessage(tr("Линия настроена: станций — %1").arg(stationCount));

    playback_ = new PlaybackController(this);
    playback_->setRate(params.rate);
    session_ = new AppSession(this);

    connect(session_, &AppSession::started, this, [this](const api::RunInfo& info) {
        placeholder_->setText(tr("Получены статические данные запуска. Зерно: %1").arg(info.seed));
    });
    connect(session_, &AppSession::chunkReady, playback_, &PlaybackController::addChunk);
    connect(playback_, &PlaybackController::timeChanged, this, [this](api::SimTime time) {
        statusBar()->showMessage(tr("Модельное время: %1 с").arg(time));
    });
    connect(session_, &AppSession::summaryReady, this, [this](const api::SimSummary&) {
        placeholder_->setText(tr("Статические примеры API получены"));
    });
    connect(session_, &AppSession::errorOccurred, this, [this](const api::RuntimeError& error) {
        placeholder_->setText(QString::fromStdString(error.message));
    });

    session_->start(params);
}

} // namespace ubahn::app
