#pragma once

#include "api/Types.h"

#include <QDialog>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTime;
class QTimeEdit;

namespace ubahn::app {

class LineEditor;
class ScheduleEditor;

class SetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit SetupDialog(QWidget* parent = nullptr);

    api::SimParams selectedParams() const;

private:
    void loadPreset(int index);
    void updatePeriodMode();
    void validateAndAccept();
    std::vector<std::string> inputErrors() const;
    static api::SimTime dayAndTime(int day, const QTime& time);

    std::vector<api::Preset> presets_;
    QComboBox* presetCombo_ = nullptr;
    LineEditor* lineEditor_ = nullptr;
    ScheduleEditor* scheduleEditor_ = nullptr;
    QComboBox* startDay_ = nullptr;
    QTimeEdit* startTime_ = nullptr;
    QComboBox* periodMode_ = nullptr;
    QComboBox* endDay_ = nullptr;
    QTimeEdit* endTime_ = nullptr;
    QSpinBox* durationHours_ = nullptr;
    QSpinBox* step_ = nullptr;
    QComboBox* rate_ = nullptr;
    QCheckBox* fixedSeed_ = nullptr;
    QLineEdit* seed_ = nullptr;
    QLabel* validationErrors_ = nullptr;
};

} // namespace ubahn::app
