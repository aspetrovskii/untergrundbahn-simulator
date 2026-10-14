#include "MonitoringPanel.h"

#include "RuntimeScheduleEditor.h"
#include "SummaryWidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace ubahn::app {
namespace {

void setCell(QTableWidget* table, int row, int column, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table->setItem(row, column, item);
}

QString directionText(api::Dir direction) {
    return direction == api::Dir::I ? QStringLiteral("I") : QStringLiteral("II");
}

} // namespace

MonitoringPanel::MonitoringPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("monitoringPanel");
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    delayBanner_ = new QLabel(this);
    delayBanner_->setObjectName("delayBanner");
    delayBanner_->setWordWrap(true);
    delayBanner_->setStyleSheet(
        QStringLiteral("background: #fff3cd; border: 1px solid #d6b656; padding: 8px;"));
    delayBanner_->hide();
    layout->addWidget(delayBanner_);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("monitoringTabs");

    trains_ = new QTableWidget(tabs_);
    trains_->setObjectName("trainTable");
    trains_->setColumnCount(5);
    trains_->setHorizontalHeaderLabels(
        {tr("Поезд"), tr("Режим"), tr("Положение"), tr("Опоздание"), tr("Состояние")});
    trains_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    trains_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    trains_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    trains_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    trains_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    trains_->verticalHeader()->setVisible(false);
    trains_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs_->addTab(trains_, tr("Поезда"));

    warnings_ = new QTableWidget(tabs_);
    warnings_->setObjectName("warningTable");
    warnings_->setColumnCount(2);
    warnings_->setHorizontalHeaderLabels({tr("Тип"), tr("Сообщение")});
    warnings_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    warnings_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    warnings_->verticalHeader()->setVisible(false);
    warnings_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabs_->addTab(warnings_, tr("Предупреждения"));

    scheduleEditor_ = new RuntimeScheduleEditor(tabs_);
    scheduleTab_ = tabs_->addTab(scheduleEditor_, tr("Расписание"));
    connect(scheduleEditor_, &RuntimeScheduleEditor::scheduleAccepted, this,
            &MonitoringPanel::scheduleApplyRequested);

    summary_ = new SummaryWidget(tabs_);
    summaryTab_ = tabs_->addTab(summary_, tr("Итоги"));
    layout->addWidget(tabs_);

    delayTimer_ = new QTimer(this);
    delayTimer_->setObjectName("delayTimer");
    delayTimer_->setSingleShot(true);
    delayTimer_->setInterval(4000);
    connect(delayTimer_, &QTimer::timeout, delayBanner_, &QLabel::hide);
}

void MonitoringPanel::setLine(const api::LineConfig& line) {
    line_ = line;
    scheduleEditor_->setLine(line);
    summary_->setLine(line);
}

void MonitoringPanel::setSchedule(const api::ScheduleConfig& schedule) {
    scheduleEditor_->setSchedule(schedule);
}

void MonitoringPanel::setSummary(const api::SimSummary& summary) {
    summary_->setSummary(summary);
    tabs_->setCurrentIndex(summaryTab_);
}

void MonitoringPanel::showScheduleErrors(const std::vector<api::ValidationIssue>& issues) {
    scheduleEditor_->showApiErrors(issues);
    tabs_->setCurrentIndex(scheduleTab_);
}

void MonitoringPanel::setRunning(bool running) {
    scheduleEditor_->setEditable(running);
    if (running) {
        delayBanner_->hide();
        tabs_->setCurrentIndex(0);
    }
}

void MonitoringPanel::setState(const SimulationState& state) {
    updateTrains(state.trains());
    updateWarnings(state.warnings());
}

void MonitoringPanel::showDelayNotices(const std::vector<api::DelayNotice>& notices) {
    if (notices.empty()) {
        return;
    }

    QString text = tr("Задержка движения");
    for (const auto& notice : notices) {
        text += tr("\nПоезд %1 · %2 · путь %3 · +%4 с")
                    .arg(notice.trainId + 1)
                    .arg(stationName(notice.station))
                    .arg(directionText(notice.dir))
                    .arg(notice.seconds);
    }
    delayBanner_->setText(text);
    delayBanner_->show();
    delayTimer_->start();
}

void MonitoringPanel::updateTrains(const std::map<int, api::TrainUpdate>& trains) {
    trains_->setRowCount(static_cast<int>(trains.size()));
    int row = 0;
    for (const auto& [id, train] : trains) {
        setCell(trains_, row, 0, QString::number(id + 1));
        setCell(trains_, row, 1, modeText(train.mode));
        setCell(trains_, row, 2, locationText(train));
        setCell(trains_, row, 3, train.lateness > 0 ? tr("+%1 с").arg(train.lateness) : tr("0 с"));
        setCell(trains_, row, 4, flagsText(train.flags));
        ++row;
    }
}

void MonitoringPanel::updateWarnings(const std::vector<api::Warning>& warnings) {
    warnings_->setRowCount(static_cast<int>(warnings.size()));
    for (std::size_t row = 0; row < warnings.size(); ++row) {
        const auto& warning = warnings[row];
        const QString type = warning.code == api::WarningCode::IntervalStretched
                                 ? tr("Растянут интервал")
                                 : tr("Не хватает поезда");
        setCell(warnings_, static_cast<int>(row), 0, type);
        setCell(warnings_, static_cast<int>(row), 1, QString::fromStdString(warning.message));
    }
}

QString MonitoringPanel::modeText(api::TrainMode mode) const {
    switch (mode) {
    case api::TrainMode::Stopped:
        return tr("Остановлен");
    case api::TrainMode::Service:
        return tr("Служебный");
    case api::TrainMode::Slowed:
        return tr("Замедление");
    case api::TrainMode::CatchUp:
        return tr("Нагон");
    case api::TrainMode::OnTime:
        return tr("По графику");
    }
    return tr("—");
}

QString MonitoringPanel::locationText(const api::TrainUpdate& train) const {
    const QString station = stationName(train.station);
    switch (train.edge) {
    case api::TrackEdge::Span: {
        const int nextStation = train.dir == api::Dir::I ? train.station + 1 : train.station - 1;
        return tr("%1 → %2, путь %3")
            .arg(station)
            .arg(stationName(nextStation))
            .arg(directionText(train.dir));
    }
    case api::TrackEdge::Platform:
        return tr("%1, платформа %2").arg(station).arg(directionText(train.dir));
    case api::TrackEdge::ToDeadEnd:
        return tr("%1 → тупик").arg(station);
    case api::TrackEdge::FromDeadEnd:
        return tr("Тупик → %1").arg(station);
    case api::TrackEdge::ToDepot:
        return tr("%1 → депо").arg(station);
    case api::TrackEdge::FromDepot:
        return tr("Депо → %1").arg(station);
    case api::TrackEdge::Depot:
        return tr("Депо у станции %1").arg(station);
    }
    return tr("—");
}

QString MonitoringPanel::flagsText(const api::TrainFlags& flags) const {
    std::vector<std::string> names;
    if (flags.headingToDepot) {
        names.emplace_back("в депо");
    }
    if (flags.catchingUp) {
        names.emplace_back("нагоняет");
    }
    if (flags.slowed) {
        names.emplace_back("замедлен");
    }
    if (flags.held) {
        names.emplace_back("удержан");
    }
    if (flags.offHours) {
        names.emplace_back("вне часов работы");
    }
    if (names.empty()) {
        return tr("—");
    }

    std::string text;
    for (const auto& name : names) {
        if (!text.empty()) {
            text += ", ";
        }
        text += name;
    }
    return QString::fromStdString(text);
}

QString MonitoringPanel::stationName(int station) const {
    if (station < 0 || station >= static_cast<int>(line_.stationNames.size())) {
        return tr("станция %1").arg(station + 1);
    }
    return QString::fromStdString(line_.stationNames[static_cast<std::size_t>(station)]);
}

} // namespace ubahn::app
