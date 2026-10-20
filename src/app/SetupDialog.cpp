#include "SetupDialog.h"

#include "LineEditor.h"
#include "ScheduleEditor.h"
#include "api/Functions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace ubahn::app {
namespace {

constexpr std::array<const char*, 7> kShortDayNames = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};

void addDays(QComboBox* combo) {
    for (const char* day : kShortDayNames) {
        combo->addItem(SetupDialog::tr(day));
    }
}

QScrollArea* scrollable(QWidget* content, QWidget* parent) {
    auto* area = new QScrollArea(parent);
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    return area;
}

} // namespace

SetupDialog::SetupDialog(QWidget* parent) : QDialog(parent), presets_(api::builtinPresets()) {
    setObjectName("setupDialog");
    setWindowTitle(tr("Настройка симуляции"));
    resize(900, 720);

    auto* layout = new QVBoxLayout(this);
    auto* presetForm = new QFormLayout;
    presetCombo_ = new QComboBox(this);
    presetCombo_->setObjectName("presetCombo");
    for (const auto& preset : presets_) {
        presetCombo_->addItem(QString::fromStdString(preset.name));
    }
    presetForm->addRow(tr("Готовая конфигурация:"), presetCombo_);
    layout->addLayout(presetForm);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName("setupTabs");
    lineEditor_ = new LineEditor(tabs);
    tabs->addTab(scrollable(lineEditor_, tabs), tr("Линия"));
    scheduleEditor_ = new ScheduleEditor(tabs);
    tabs->addTab(scheduleEditor_, tr("Расписание на 7 дней"));

    auto* launchPage = new QWidget(tabs);
    auto* launchLayout = new QFormLayout(launchPage);
    startDay_ = new QComboBox(launchPage);
    startDay_->setObjectName("startDayCombo");
    addDays(startDay_);
    startTime_ = new QTimeEdit(launchPage);
    startTime_->setObjectName("startTimeEdit");
    startTime_->setDisplayFormat("HH:mm");
    auto* startRow = new QWidget(launchPage);
    auto* startRowLayout = new QHBoxLayout(startRow);
    startRowLayout->setContentsMargins(0, 0, 0, 0);
    startRowLayout->addWidget(startDay_);
    startRowLayout->addWidget(startTime_);
    launchLayout->addRow(tr("Начало:"), startRow);

    periodMode_ = new QComboBox(launchPage);
    periodMode_->setObjectName("periodModeCombo");
    periodMode_->addItem(tr("До выбранного дня и времени"), 0);
    periodMode_->addItem(tr("По длительности"), 1);
    launchLayout->addRow(tr("Как задать конец:"), periodMode_);

    endDay_ = new QComboBox(launchPage);
    endDay_->setObjectName("endDayCombo");
    addDays(endDay_);
    endTime_ = new QTimeEdit(launchPage);
    endTime_->setObjectName("endTimeEdit");
    endTime_->setDisplayFormat("HH:mm");
    auto* endRow = new QWidget(launchPage);
    auto* endRowLayout = new QHBoxLayout(endRow);
    endRowLayout->setContentsMargins(0, 0, 0, 0);
    endRowLayout->addWidget(endDay_);
    endRowLayout->addWidget(endTime_);
    launchLayout->addRow(tr("Конец:"), endRow);

    durationHours_ = new QSpinBox(launchPage);
    durationHours_->setObjectName("durationHoursSpin");
    durationHours_->setRange(1, 8760);
    durationHours_->setSuffix(tr(" ч"));
    launchLayout->addRow(tr("Длительность:"), durationHours_);

    step_ = new QSpinBox(launchPage);
    step_->setObjectName("stepSpin");
    step_->setRange(1, 240);
    step_->setSuffix(tr(" с"));
    launchLayout->addRow(tr("Шаг проигрывания:"), step_);

    rate_ = new QComboBox(launchPage);
    rate_->setObjectName("initialRateCombo");
    rate_->addItem(tr("Пауза"), 0.0);
    rate_->addItem(tr("0,5×"), 0.5);
    rate_->addItem(tr("1×"), 1.0);
    rate_->addItem(tr("2×"), 2.0);
    rate_->addItem(tr("5×"), 5.0);
    launchLayout->addRow(tr("Начальный темп:"), rate_);

    fixedSeed_ = new QCheckBox(tr("Использовать заданное зерно"), launchPage);
    fixedSeed_->setObjectName("fixedSeedCheck");
    launchLayout->addRow(fixedSeed_);
    seed_ = new QLineEdit(launchPage);
    seed_->setObjectName("seedEdit");
    seed_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,20}")), seed_));
    seed_->setEnabled(false);
    launchLayout->addRow(tr("Зерно генератора:"), seed_);
    tabs->addTab(launchPage, tr("Запуск"));
    layout->addWidget(tabs, 1);

    validationErrors_ = new QLabel(this);
    validationErrors_->setObjectName("validationErrors");
    validationErrors_->setWordWrap(true);
    validationErrors_->setStyleSheet(QStringLiteral("color: #b00020;"));
    validationErrors_->hide();
    layout->addWidget(validationErrors_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    auto* startButton = buttons->addButton(tr("Открыть симуляцию"), QDialogButtonBox::AcceptRole);
    startButton->setObjectName("startButton");
    connect(startButton, &QPushButton::clicked, this, &SetupDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(presetCombo_, &QComboBox::currentIndexChanged, this, &SetupDialog::loadPreset);
    connect(periodMode_, &QComboBox::currentIndexChanged, this, &SetupDialog::updatePeriodMode);
    connect(fixedSeed_, &QCheckBox::toggled, seed_, &QLineEdit::setEnabled);

    loadPreset(0);
}

api::SimParams SetupDialog::selectedParams() const {
    api::SimParams params;
    params.line = lineEditor_->line();
    params.schedule = scheduleEditor_->schedule();
    params.period.start = dayAndTime(startDay_->currentIndex(), startTime_->time());
    if (periodMode_->currentData().toInt() == 0) {
        params.period.end = dayAndTime(endDay_->currentIndex(), endTime_->time());
    } else {
        params.period.end = params.period.start + durationHours_->value() * 3600;
    }
    params.stepSec = step_->value();
    params.rate = rate_->currentData().toDouble();
    if (fixedSeed_->isChecked()) {
        bool ok = false;
        const uint64_t value = seed_->text().toULongLong(&ok);
        if (ok) {
            params.seed = value;
        }
    }
    return params;
}

void SetupDialog::loadPreset(int index) {
    if (index < 0 || index >= static_cast<int>(presets_.size())) {
        return;
    }
    const api::SimParams& params = presets_[static_cast<std::size_t>(index)].params;
    lineEditor_->setLine(params.line);
    scheduleEditor_->setSchedule(params.schedule);
    startDay_->setCurrentIndex(api::dayOfWeek(params.period.start));
    startTime_->setTime(QTime(0, 0).addSecs(api::timeOfDay(params.period.start)));
    endDay_->setCurrentIndex(api::dayOfWeek(params.period.end));
    endTime_->setTime(QTime(0, 0).addSecs(api::timeOfDay(params.period.end)));
    durationHours_->setValue(static_cast<int>(
        std::max<api::SimTime>(1, (params.period.end - params.period.start) / 3600)));
    step_->setValue(params.stepSec);
    const int rateIndex = rate_->findData(params.rate);
    rate_->setCurrentIndex(rateIndex >= 0 ? rateIndex : 2);
    fixedSeed_->setChecked(params.seed.has_value());
    seed_->setText(params.seed.has_value() ? QString::number(*params.seed) : QString{});
    periodMode_->setCurrentIndex(0);
    updatePeriodMode();
    validationErrors_->hide();
}

void SetupDialog::updatePeriodMode() {
    const bool useEnd = periodMode_->currentData().toInt() == 0;
    endDay_->setEnabled(useEnd);
    endTime_->setEnabled(useEnd);
    durationHours_->setEnabled(!useEnd);
}

void SetupDialog::validateAndAccept() {
    std::vector<std::string> errors = inputErrors();
    const auto issues = api::validate(selectedParams());
    for (const auto& issue : issues) {
        errors.push_back(issue.message);
    }
    if (errors.empty()) {
        accept();
        return;
    }

    QString text = tr("Исправьте настройки:");
    for (const auto& error : errors) {
        text += QStringLiteral("\n• ") + QString::fromStdString(error);
    }
    validationErrors_->setText(text);
    validationErrors_->show();
}

std::vector<std::string> SetupDialog::inputErrors() const {
    std::vector<std::string> errors = lineEditor_->inputErrors();
    const auto scheduleErrors = scheduleEditor_->inputErrors();
    errors.insert(errors.end(), scheduleErrors.begin(), scheduleErrors.end());
    if (fixedSeed_->isChecked()) {
        bool seedOk = false;
        seed_->text().toULongLong(&seedOk);
        if (!seedOk) {
            errors.push_back("Зерно генератора должно быть целым неотрицательным числом");
        }
    }
    return errors;
}

api::SimTime SetupDialog::dayAndTime(int day, const QTime& time) {
    return static_cast<api::SimTime>(day) * api::kSecondsPerDay + QTime(0, 0).secsTo(time);
}

} // namespace ubahn::app
