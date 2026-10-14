#pragma once

#include "SimulationState.h"
#include "api/Types.h"

#include <QWidget>
#include <string>
#include <vector>

class QLabel;
class QTableWidget;
class QTabWidget;
class QTimer;

namespace ubahn::app {

class SummaryWidget;
class RuntimeScheduleEditor;

class MonitoringPanel : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPanel(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    void setSchedule(const api::ScheduleConfig& schedule);
    void setState(const SimulationState& state);
    void showDelayNotices(const std::vector<api::DelayNotice>& notices);
    void setSummary(const api::SimSummary& summary);
    void showScheduleErrors(const std::vector<api::ValidationIssue>& issues);
    void setRunning(bool running);

signals:
    void scheduleApplyRequested(const ubahn::api::ScheduleConfig& schedule);

private:
    void updateTrains(const std::map<int, api::TrainUpdate>& trains);
    void updateWarnings(const std::vector<api::Warning>& warnings);
    QString modeText(api::TrainMode mode) const;
    QString locationText(const api::TrainUpdate& train) const;
    QString flagsText(const api::TrainFlags& flags) const;
    QString stationName(int station) const;

    api::LineConfig line_;
    QLabel* delayBanner_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* trains_ = nullptr;
    QTableWidget* warnings_ = nullptr;
    QTimer* delayTimer_ = nullptr;
    SummaryWidget* summary_ = nullptr;
    RuntimeScheduleEditor* scheduleEditor_ = nullptr;
    int scheduleTab_ = -1;
    int summaryTab_ = -1;
};

} // namespace ubahn::app
