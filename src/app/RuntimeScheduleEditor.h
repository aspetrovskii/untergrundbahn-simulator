#pragma once

#include "api/Types.h"

#include <QWidget>

class QLabel;

namespace ubahn::app {

class ScheduleEditor;

class RuntimeScheduleEditor : public QWidget {
    Q_OBJECT

public:
    explicit RuntimeScheduleEditor(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    void setSchedule(const api::ScheduleConfig& schedule);

signals:
    void scheduleAccepted(const ubahn::api::ScheduleConfig& schedule);

private:
    void applySchedule();

    api::LineConfig line_;
    ScheduleEditor* editor_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace ubahn::app
