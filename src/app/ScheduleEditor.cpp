#include "ScheduleEditor.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace ubahn::app {
namespace {

constexpr std::array<const char*, 7> kDayNames = {"Понедельник", "Вторник", "Среда",      "Четверг",
                                                  "Пятница",     "Суббота", "Воскресенье"};

QString cellText(const QTableWidget* table, int row, int column) {
    const QTableWidgetItem* item = table->item(row, column);
    return item == nullptr ? QString{} : item->text().trimmed();
}

} // namespace

ScheduleEditor::ScheduleEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("scheduleEditor");
    auto* layout = new QVBoxLayout(this);

    days_ = new QTabWidget(this);
    days_->setObjectName("scheduleDays");
    for (int day = 0; day < static_cast<int>(tables_.size()); ++day) {
        auto* page = new QWidget(days_);
        auto* pageLayout = new QVBoxLayout(page);
        auto* table = new QTableWidget(page);
        table->setObjectName(QStringLiteral("scheduleDay%1").arg(day));
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels(
            {tr("Начало"), tr("Конец"), tr("Интервал, с"), tr("Стоянка, с"), tr("Задержка, %")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        tables_[static_cast<std::size_t>(day)] = table;
        pageLayout->addWidget(table);

        auto* buttons = new QHBoxLayout;
        auto* addButton = new QPushButton(tr("Добавить окно"), page);
        auto* removeButton = new QPushButton(tr("Удалить окно"), page);
        addButton->setObjectName(QStringLiteral("addWindow%1").arg(day));
        removeButton->setObjectName(QStringLiteral("removeWindow%1").arg(day));
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addStretch();
        pageLayout->addLayout(buttons);

        connect(addButton, &QPushButton::clicked, this, [this, day] { addDefaultWindow(day); });
        connect(removeButton, &QPushButton::clicked, this,
                [this, day] { removeSelectedWindow(day); });
        days_->addTab(page, tr(kDayNames[static_cast<std::size_t>(day)]));
    }
    layout->addWidget(days_);

    auto* delayForm = new QFormLayout;
    delayMin_ = new QSpinBox(this);
    delayMin_->setObjectName("delayMinSpin");
    delayMin_->setRange(0, 600);
    delayMax_ = new QSpinBox(this);
    delayMax_->setObjectName("delayMaxSpin");
    delayMax_->setRange(0, 600);
    delayForm->addRow(tr("Минимальная случайная задержка, с:"), delayMin_);
    delayForm->addRow(tr("Максимальная случайная задержка, с:"), delayMax_);
    layout->addLayout(delayForm);
}

void ScheduleEditor::setSchedule(const api::ScheduleConfig& schedule) {
    for (int day = 0; day < static_cast<int>(tables_.size()); ++day) {
        QTableWidget* table = tables_[static_cast<std::size_t>(day)];
        table->setRowCount(0);
        for (const auto& window : schedule.days[static_cast<std::size_t>(day)].windows) {
            addWindow(day, window);
        }
    }
    delayMin_->setValue(schedule.delayMin);
    delayMax_->setValue(schedule.delayMax);
}

api::ScheduleConfig ScheduleEditor::schedule() const {
    api::ScheduleConfig result;
    for (int day = 0; day < static_cast<int>(tables_.size()); ++day) {
        const QTableWidget* table = tables_[static_cast<std::size_t>(day)];
        for (int row = 0; row < table->rowCount(); ++row) {
            api::Window window;
            window.fromTod = parseTime(cellText(table, row, 0)).value_or(0);
            window.toTod = parseTime(cellText(table, row, 1)).value_or(0);
            window.headway = cellText(table, row, 2).toInt();
            window.dwell = cellText(table, row, 3).toInt();
            window.delayProb = cellText(table, row, 4).toDouble() / 100.0;
            result.days[static_cast<std::size_t>(day)].windows.push_back(window);
        }
    }
    result.delayMin = delayMin_->value();
    result.delayMax = delayMax_->value();
    return result;
}

std::vector<std::string> ScheduleEditor::inputErrors() const {
    std::vector<std::string> errors;
    for (int day = 0; day < static_cast<int>(tables_.size()); ++day) {
        const QTableWidget* table = tables_[static_cast<std::size_t>(day)];
        if (table->rowCount() == 0) {
            errors.push_back(std::string(kDayNames[static_cast<std::size_t>(day)]) +
                             ": добавьте хотя бы одно окно");
            continue;
        }

        int32_t expectedStart = api::kOpenTod;
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto from = parseTime(cellText(table, row, 0));
            const auto to = parseTime(cellText(table, row, 1));
            const std::string prefix = std::string(kDayNames[static_cast<std::size_t>(day)]) +
                                       ", окно " + std::to_string(row + 1) + ": ";
            if (!from.has_value() || !to.has_value()) {
                errors.push_back(prefix + "время задаётся в формате ЧЧ:ММ");
                continue;
            }
            if (*from != expectedStart || *to <= *from || *to > api::kCloseTod) {
                errors.push_back(prefix + "окна должны без разрывов покрывать время 06:00–24:00");
            }
            expectedStart = *to;

            bool headwayOk = false;
            bool dwellOk = false;
            bool probabilityOk = false;
            const int headway = cellText(table, row, 2).toInt(&headwayOk);
            const int dwell = cellText(table, row, 3).toInt(&dwellOk);
            const double probability = cellText(table, row, 4).toDouble(&probabilityOk);
            if (!headwayOk || headway <= 0) {
                errors.push_back(prefix + "интервал должен быть положительным числом");
            }
            if (!dwellOk || dwell <= 0) {
                errors.push_back(prefix + "стоянка должна быть положительным числом");
            }
            if (!probabilityOk || probability < 0.0 || probability > 100.0) {
                errors.push_back(prefix + "вероятность должна быть от 0 до 100%");
            }
        }
        if (expectedStart != api::kCloseTod) {
            errors.push_back(std::string(kDayNames[static_cast<std::size_t>(day)]) +
                             ": последнее окно должно заканчиваться в 24:00");
        }
    }
    if (delayMin_->value() > delayMax_->value()) {
        errors.push_back("Минимальная задержка не может быть больше максимальной");
    }
    return errors;
}

QTableWidget* ScheduleEditor::dayTable(int day) const {
    if (day < 0 || day >= static_cast<int>(tables_.size())) {
        return nullptr;
    }
    return tables_[static_cast<std::size_t>(day)];
}

void ScheduleEditor::addWindow(int day, const api::Window& window) {
    QTableWidget* table = dayTable(day);
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(formatTime(window.fromTod)));
    table->setItem(row, 1, new QTableWidgetItem(formatTime(window.toTod)));
    table->setItem(row, 2, new QTableWidgetItem(QString::number(window.headway)));
    table->setItem(row, 3, new QTableWidgetItem(QString::number(window.dwell)));
    table->setItem(row, 4, new QTableWidgetItem(QString::number(window.delayProb * 100.0, 'f', 0)));
}

void ScheduleEditor::addDefaultWindow(int day) {
    QTableWidget* table = dayTable(day);
    int32_t from = api::kOpenTod;
    if (table->rowCount() > 0) {
        from = parseTime(cellText(table, table->rowCount() - 1, 1)).value_or(api::kOpenTod);
    }
    const int32_t to = std::min(from + 3600, api::kCloseTod);
    addWindow(day, {from, to, 120, 60, 0.1});
}

void ScheduleEditor::removeSelectedWindow(int day) {
    QTableWidget* table = dayTable(day);
    int row = table->currentRow();
    if (row < 0) {
        row = table->rowCount() - 1;
    }
    if (row >= 0) {
        table->removeRow(row);
    }
}

std::optional<int32_t> ScheduleEditor::parseTime(const QString& text) {
    const int separator = text.indexOf(':');
    if (separator <= 0 || separator != text.lastIndexOf(':')) {
        return std::nullopt;
    }
    bool hoursOk = false;
    bool minutesOk = false;
    const int hours = text.left(separator).toInt(&hoursOk);
    const int minutes = text.mid(separator + 1).toInt(&minutesOk);
    if (!hoursOk || !minutesOk || hours < 0 || hours > 24 || minutes < 0 || minutes > 59 ||
        (hours == 24 && minutes != 0)) {
        return std::nullopt;
    }
    return hours * 3600 + minutes * 60;
}

QString ScheduleEditor::formatTime(int32_t seconds) {
    const int hours = seconds / 3600;
    const int minutes = seconds % 3600 / 60;
    return QStringLiteral("%1:%2")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'));
}

} // namespace ubahn::app
