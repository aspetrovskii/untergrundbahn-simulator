#pragma once

#include "api/Types.h"

#include <QWidget>
#include <vector>

class QLabel;
class QPushButton;

namespace ubahn::app {

class ScheduleEditor;

class RuntimeScheduleEditor : public QWidget {
    Q_OBJECT

public:
    explicit RuntimeScheduleEditor(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    void setSchedule(const api::ScheduleConfig& schedule);
    void showApiErrors(const std::vector<api::ValidationIssue>& issues);
    void setEditable(bool editable);

signals:
    void scheduleAccepted(const ubahn::api::ScheduleConfig& schedule);

private:
    void applySchedule();

    api::LineConfig line_;
    ScheduleEditor* editor_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* applyButton_ = nullptr;
};

} // namespace ubahn::app
