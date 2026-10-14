#include "MainWindow.h"

#include "AppSession.h"
#include "BoardWidget.h"
#include "LineView.h"
#include "MonitoringPanel.h"
#include "PlaybackController.h"
#include "PlaybackPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace ubahn::app {

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1250, 720);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    playbackPanel_ = new PlaybackPanel(central);
    playbackPanel_->setInitialValues(params.rate, params.stepSec);
    layout->addWidget(playbackPanel_);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName("mainSplitter");
    auto* lineArea = new QWidget(splitter);
    auto* lineLayout = new QVBoxLayout(lineArea);
    lineLayout->setContentsMargins(0, 0, 0, 0);

    lineView_ = new LineView(lineArea);
    lineView_->setLine(params.line);
    lineLayout->addWidget(lineView_, 1);

    auto* selectorLayout = new QHBoxLayout;
    selectorLayout->addWidget(new QLabel(tr("Станция:"), lineArea));
    stationSelector_ = new QComboBox(lineArea);
    stationSelector_->setObjectName("boardStationSelector");
    for (const auto& stationName : params.line.stationNames) {
        stationSelector_->addItem(QString::fromStdString(stationName));
    }
    selectorLayout->addWidget(stationSelector_);
    selectorLayout->addStretch();
    lineLayout->addLayout(selectorLayout);

    boardWidget_ = new BoardWidget(lineArea);
    lineLayout->addWidget(boardWidget_);
    splitter->addWidget(lineArea);

    monitoringPanel_ = new MonitoringPanel(splitter);
    monitoringPanel_->setLine(params.line);
    monitoringPanel_->setSchedule(params.schedule);
    splitter->addWidget(monitoringPanel_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    const auto stationCount = params.line.stationNames.size();
    statusBar()->showMessage(tr("Линия настроена: станций — %1").arg(stationCount));

    playback_ = new PlaybackController(this);
    playback_->setRate(params.rate);
    session_ = new AppSession(this);

    connect(playbackPanel_, &PlaybackPanel::rateChanged, playback_, &PlaybackController::setRate);
    connect(playbackPanel_, &PlaybackPanel::rateChanged, session_, &AppSession::setRate);
    connect(playbackPanel_, &PlaybackPanel::stepChanged, session_, &AppSession::setStep);
    connect(playbackPanel_, &PlaybackPanel::seekForwardRequested, playback_,
            &PlaybackController::seekForward);
    connect(playbackPanel_, &PlaybackPanel::seekForwardRequested, session_,
            &AppSession::seekForward);
    connect(playbackPanel_, &PlaybackPanel::stopRequested, session_, &AppSession::stop);
    connect(monitoringPanel_, &MonitoringPanel::scheduleApplyRequested, session_,
            &AppSession::applySchedule);
    connect(monitoringPanel_, &MonitoringPanel::scheduleApplyRequested, this,
            [this] { statusBar()->showMessage(tr("Новое расписание передано симуляции")); });

    connect(session_, &AppSession::started, this, [this](const api::RunInfo& info) {
        statusBar()->showMessage(tr("Запуск с зерном %1").arg(info.seed));
    });
    connect(session_, &AppSession::chunkReady, playback_, &PlaybackController::addChunk);
    connect(playback_, &PlaybackController::stateChanged, this, &MainWindow::refreshView);
    connect(playback_, &PlaybackController::delayNoticesReady, this,
            [this] { monitoringPanel_->showDelayNotices(playback_->state().delayNotices()); });
    connect(playback_, &PlaybackController::timeChanged, this, [this](api::SimTime time) {
        refreshView();
        statusBar()->showMessage(tr("Модельное время: %1 с").arg(time));
    });
    connect(stationSelector_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshBoard);
    connect(session_, &AppSession::summaryReady, this, [this](const api::SimSummary& summary) {
        monitoringPanel_->setSummary(summary);
        statusBar()->showMessage(summary.completed ? tr("Моделирование завершено")
                                                   : tr("Моделирование остановлено"));
    });
    connect(session_, &AppSession::errorOccurred, this, [this](const api::RuntimeError& error) {
        statusBar()->showMessage(QString::fromStdString(error.message));
    });

    refreshBoard();
    session_->start(params);
}

void MainWindow::refreshView() {
    lineView_->setState(playback_->state(), playback_->exactTime());
    playbackPanel_->setModelState(playback_->currentTime(), playback_->state().clock());
    monitoringPanel_->setState(playback_->state());
    refreshBoard();
}

void MainWindow::refreshBoard() {
    const int station = stationSelector_->currentIndex();
    if (station < 0) {
        return;
    }

    boardWidget_->setStationName(stationSelector_->currentText().toStdString());
    const auto& boards = playback_->state().boards();
    const auto directionI = boards.find({static_cast<int>(api::Dir::I), station});
    const auto directionII = boards.find({static_cast<int>(api::Dir::II), station});
    const api::BoardUpdate* boardI = directionI == boards.end() ? nullptr : &directionI->second;
    const api::BoardUpdate* boardII = directionII == boards.end() ? nullptr : &directionII->second;
    boardWidget_->setBoards(boardI, boardII, playback_->currentTime());
}

} // namespace ubahn::app
