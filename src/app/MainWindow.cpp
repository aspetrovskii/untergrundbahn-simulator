#include "MainWindow.h"

#include "AppSession.h"
#include "BoardWidget.h"
#include "LineView.h"
#include "PlaybackController.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace ubahn::app {

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1000, 650);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    lineView_ = new LineView(central);
    lineView_->setLine(params.line);
    layout->addWidget(lineView_, 1);

    auto* selectorLayout = new QHBoxLayout;
    selectorLayout->addWidget(new QLabel(tr("Станция:"), central));
    stationSelector_ = new QComboBox(central);
    stationSelector_->setObjectName("boardStationSelector");
    for (const auto& stationName : params.line.stationNames) {
        stationSelector_->addItem(QString::fromStdString(stationName));
    }
    selectorLayout->addWidget(stationSelector_);
    selectorLayout->addStretch();
    layout->addLayout(selectorLayout);

    boardWidget_ = new BoardWidget(central);
    layout->addWidget(boardWidget_);
    setCentralWidget(central);

    const auto stationCount = params.line.stationNames.size();
    statusBar()->showMessage(tr("Линия настроена: станций — %1").arg(stationCount));

    playback_ = new PlaybackController(this);
    playback_->setRate(params.rate);
    session_ = new AppSession(this);

    connect(session_, &AppSession::started, this, [this](const api::RunInfo& info) {
        statusBar()->showMessage(tr("Запуск с зерном %1").arg(info.seed));
    });
    connect(session_, &AppSession::chunkReady, playback_, &PlaybackController::addChunk);
    connect(playback_, &PlaybackController::stateChanged, this, &MainWindow::refreshView);
    connect(playback_, &PlaybackController::timeChanged, this, [this](api::SimTime time) {
        refreshView();
        statusBar()->showMessage(tr("Модельное время: %1 с").arg(time));
    });
    connect(stationSelector_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshBoard);
    connect(session_, &AppSession::summaryReady, this, [this](const api::SimSummary&) {
        statusBar()->showMessage(tr("Статические примеры API получены"));
    });
    connect(session_, &AppSession::errorOccurred, this, [this](const api::RuntimeError& error) {
        statusBar()->showMessage(QString::fromStdString(error.message));
    });

    refreshBoard();
    session_->start(params);
}

void MainWindow::refreshView() {
    lineView_->setState(playback_->state(), playback_->exactTime());
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
