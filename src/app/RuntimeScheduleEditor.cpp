#include "RuntimeScheduleEditor.h"

#include "ScheduleEditor.h"
#include "api/Functions.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ubahn::app {
namespace {

QString errorText(const std::vector<std::string>& errors) {
    QString text = RuntimeScheduleEditor::tr("Исправьте ошибки:");
    for (const auto& error : errors) {
        text += QStringLiteral("\n• ") + QString::fromStdString(error);
    }
    return text;
}

} // namespace

RuntimeScheduleEditor::RuntimeScheduleEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("runtimeScheduleEditor");

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("Изменения будут переданы симуляции после проверки. Они начинают действовать с "
           "ближайшей границы расчётного блока."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    editor_ = new ScheduleEditor(this);
    layout->addWidget(editor_, 1);

    status_ = new QLabel(this);
    status_->setObjectName("runtimeScheduleStatus");
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);

    applyButton_ = new QPushButton(tr("Применить расписание"), this);
    applyButton_->setObjectName("applyScheduleButton");
    layout->addWidget(applyButton_);
    connect(applyButton_, &QPushButton::clicked, this, &RuntimeScheduleEditor::applySchedule);
}

void RuntimeScheduleEditor::setLine(const api::LineConfig& line) {
    line_ = line;
}

void RuntimeScheduleEditor::setSchedule(const api::ScheduleConfig& schedule) {
    editor_->setSchedule(schedule);
    status_->hide();
}

void RuntimeScheduleEditor::showApiErrors(const std::vector<api::ValidationIssue>& issues) {
    std::vector<std::string> errors;
    for (const auto& issue : issues) {
        errors.push_back(issue.message);
    }
    if (errors.empty()) {
        errors.push_back("Бэкенд отклонил расписание без описания ошибки");
    }
    status_->setStyleSheet(QStringLiteral("color: #b00020;"));
    status_->setText(errorText(errors));
    status_->show();
}

void RuntimeScheduleEditor::setEditable(bool editable) {
    editor_->setEnabled(editable);
    applyButton_->setEnabled(editable);
}

void RuntimeScheduleEditor::applySchedule() {
    std::vector<std::string> errors = editor_->inputErrors();
    const api::ScheduleConfig schedule = editor_->schedule();
    for (const auto& issue : api::validateSchedule(line_, schedule)) {
        errors.push_back(issue.message);
    }

    status_->show();
    if (!errors.empty()) {
        status_->setStyleSheet(QStringLiteral("color: #b00020;"));
        status_->setText(errorText(errors));
        return;
    }

    status_->setStyleSheet(QStringLiteral("color: #18743b;"));
    status_->setText(tr("Расписание проверено и передано симуляции."));
    emit scheduleAccepted(schedule);
}

} // namespace ubahn::app
