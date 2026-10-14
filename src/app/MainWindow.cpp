#include "MainWindow.h"

#include "AppSession.h"
#include "BoardWidget.h"
#include "LineView.h"
#include "MonitoringPanel.h"
#include "PlaybackController.h"
#include "PlaybackPanel.h"
#include "RuntimeErrorBanner.h"

#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace ubahn::app {
namespace {

QString validationDetails(const std::vector<api::ValidationIssue>& issues,
                          const QString& fallback) {
    QStringList messages;
    for (const auto& issue : issues) {
        messages.push_back(QString::fromStdString(issue.message));
    }
    return messages.empty() ? fallback : messages.join(QStringLiteral("\n• ")).prepend("• ");
}

} // namespace

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent)
    : QMainWindow(parent), params_(params) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1250, 720);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    errorBanner_ = new RuntimeErrorBanner(central);
    layout->addWidget(errorBanner_);

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
    connect(playbackPanel_, &PlaybackPanel::rateChanged, this, [this](double rate) {
        if (rate > 0.0) {
            params_.rate = rate;
        }
    });
    connect(playbackPanel_, &PlaybackPanel::stepChanged, session_, &AppSession::setStep);
    connect(playbackPanel_, &PlaybackPanel::stepChanged, this,
            [this](int step) { params_.stepSec = step; });
    connect(playbackPanel_, &PlaybackPanel::seekForwardRequested, playback_,
            &PlaybackController::seekForward);
    connect(playbackPanel_, &PlaybackPanel::seekForwardRequested, session_,
            &AppSession::seekForward);
    connect(playbackPanel_, &PlaybackPanel::stopRequested, session_, &AppSession::stop);
    connect(monitoringPanel_, &MonitoringPanel::scheduleApplyRequested, session_,
            &AppSession::applySchedule);
    connect(monitoringPanel_, &MonitoringPanel::scheduleApplyRequested, this,
            [this](const api::ScheduleConfig& schedule) {
                scheduleBeforeChange_ = params_.schedule;
                params_.schedule = schedule;
                statusBar()->showMessage(tr("Новое расписание передано симуляции"));
            });
    connect(errorBanner_, &RuntimeErrorBanner::restartRequested, this,
            &MainWindow::restartSimulation);

    connect(session_, &AppSession::started, this, [this](const api::RunInfo& info) {
        hasStarted_ = true;
        lastSeed_ = info.seed;
        errorBanner_->clear();
        playbackPanel_->setFinished(false);
        monitoringPanel_->setRunning(true);
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
        playbackPanel_->setFinished(true);
        monitoringPanel_->setRunning(false);
        monitoringPanel_->setSummary(summary);
        statusBar()->showMessage(summary.completed ? tr("Моделирование завершено")
                                                   : tr("Моделирование остановлено"));
    });
    connect(session_, &AppSession::errorOccurred, this, &MainWindow::handleRuntimeError);

    refreshBoard();
    session_->start(params);
}

void MainWindow::handleRuntimeError(const api::RuntimeError& error) {
    const QString details = QString::fromStdString(error.message);
    switch (error.code) {
    case api::ErrorCode::InvalidParams:
        if (!hasStarted_) {
            playbackPanel_->setFinished(true);
            monitoringPanel_->setRunning(false);
            errorBanner_->showError(
                tr("Симуляцию не удалось запустить."),
                validationDetails(error.issues, QString::fromStdString(error.message)), false);
            statusBar()->showMessage(tr("Исправьте параметры запуска"));
            break;
        }
        if (scheduleBeforeChange_.has_value()) {
            params_.schedule = *scheduleBeforeChange_;
            scheduleBeforeChange_.reset();
        }
        monitoringPanel_->showScheduleErrors(error.issues);
        statusBar()->showMessage(tr("Расписание отклонено: исправьте указанные ошибки"));
        break;
    case api::ErrorCode::InvalidState:
        qWarning().noquote() << tr("Команда отклонена:") << details;
        break;
    case api::ErrorCode::InternalError:
        playbackPanel_->setFinished(true);
        monitoringPanel_->setRunning(false);
        errorBanner_->showError(tr("Симуляция остановлена из-за внутренней ошибки."), details,
                                true);
        statusBar()->showMessage(tr("Симуляция остановлена"));
        break;
    }
}

void MainWindow::restartSimulation() {
    if (lastSeed_.has_value()) {
        params_.seed = lastSeed_;
    }
    errorBanner_->clear();
    playback_->reset();
    playback_->setRate(params_.rate);
    playbackPanel_->setInitialValues(params_.rate, params_.stepSec);
    playbackPanel_->setFinished(false);
    playbackPanel_->setModelState(params_.period.start, std::nullopt);
    lineView_->setLine(params_.line);
    boardWidget_->setBoards(nullptr, nullptr, params_.period.start);
    monitoringPanel_->setState(playback_->state());
    monitoringPanel_->setRunning(true);
    hasStarted_ = false;
    session_->start(params_);
    statusBar()->showMessage(tr("Перезапуск симуляции"));
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
