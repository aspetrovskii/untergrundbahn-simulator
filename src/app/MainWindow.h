#pragma once

#include "api/Types.h"

#include <QMainWindow>

class QComboBox;

namespace ubahn::app {

class AppSession;
class BoardWidget;
class LineView;
class MonitoringPanel;
class PlaybackController;
class PlaybackPanel;
class RuntimeErrorBanner;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const api::SimParams& params, QWidget* parent = nullptr);

private:
    void refreshView();
    void refreshBoard();
    void handleRuntimeError(const api::RuntimeError& error);
    void restartSimulation();

    LineView* lineView_ = nullptr;
    MonitoringPanel* monitoringPanel_ = nullptr;
    PlaybackPanel* playbackPanel_ = nullptr;
    BoardWidget* boardWidget_ = nullptr;
    QComboBox* stationSelector_ = nullptr;
    AppSession* session_ = nullptr;
    PlaybackController* playback_ = nullptr;
    RuntimeErrorBanner* errorBanner_ = nullptr;
    api::SimParams params_;
    std::optional<uint64_t> lastSeed_;
    std::optional<api::ScheduleConfig> scheduleBeforeChange_;
    bool hasStarted_ = false;
};

} // namespace ubahn::app
