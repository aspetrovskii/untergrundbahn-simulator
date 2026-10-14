#pragma once

#include "api/Types.h"

#include <QWidget>
#include <array>
#include <optional>
#include <string>
#include <vector>

class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace ubahn::app {

class ScheduleEditor : public QWidget {
    Q_OBJECT

public:
    explicit ScheduleEditor(QWidget* parent = nullptr);

    void setSchedule(const api::ScheduleConfig& schedule);
    api::ScheduleConfig schedule() const;
    std::vector<std::string> inputErrors() const;

    QTableWidget* dayTable(int day) const;

private:
    void addWindow(int day, const api::Window& window);
    void addDefaultWindow(int day);
    void removeSelectedWindow(int day);
    static std::optional<int32_t> parseTime(const QString& text);
    static QString formatTime(int32_t seconds);

    QTabWidget* days_ = nullptr;
    std::array<QTableWidget*, 7> tables_{};
    QSpinBox* delayMin_ = nullptr;
    QSpinBox* delayMax_ = nullptr;
};

} // namespace ubahn::app
