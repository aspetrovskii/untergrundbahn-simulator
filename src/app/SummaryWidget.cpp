#include "SummaryWidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace ubahn::app {
namespace {

void setCell(QTableWidget* table, int row, int column, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table->setItem(row, column, item);
}

QString modelTimeText(api::SimTime time) {
    static const std::array<const char*, 7> days = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    const int day = static_cast<int>((time / api::kSecondsPerDay) % 7);
    const int32_t timeOfDay = static_cast<int32_t>(time % api::kSecondsPerDay);
    const int hours = timeOfDay / 3600;
    const int minutes = timeOfDay % 3600 / 60;
    const int seconds = timeOfDay % 60;
    return QStringLiteral("%1 %2:%3:%4")
        .arg(SummaryWidget::tr(days[static_cast<std::size_t>(day)]))
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

SummaryWidget::SummaryWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("summaryWidget");
    auto* layout = new QVBoxLayout(this);

    status_ = new QLabel(tr("Итоги ещё не получены"), this);
    status_->setObjectName("summaryStatus");
    QFont statusFont = status_->font();
    statusFont.setBold(true);
    status_->setFont(statusFont);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* tabs = new QTabWidget(this);
    metrics_ = new QTableWidget(tabs);
    metrics_->setObjectName("summaryMetrics");
    metrics_->setColumnCount(2);
    metrics_->setHorizontalHeaderLabels({tr("Показатель"), tr("Значение")});
    metrics_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    metrics_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    metrics_->verticalHeader()->setVisible(false);
    tabs->addTab(metrics_, tr("Основное"));

    stations_ = new QTableWidget(tabs);
    stations_->setObjectName("summaryStations");
    stations_->setColumnCount(5);
    stations_->setHorizontalHeaderLabels(
        {tr("Станция"), tr("I средний"), tr("I максимум"), tr("II средний"), tr("II максимум")});
    stations_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        stations_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    stations_->verticalHeader()->setVisible(false);
    tabs->addTab(stations_, tr("По станциям"));

    stretches_ = new QTableWidget(tabs);
    stretches_->setObjectName("summaryStretches");
    stretches_->setColumnCount(4);
    stretches_->setHorizontalHeaderLabels({tr("День"), tr("Окно"), tr("План, с"), tr("Факт, с")});
    stretches_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    stretches_->verticalHeader()->setVisible(false);
    tabs->addTab(stretches_, tr("Растяжения"));
    layout->addWidget(tabs);
}

void SummaryWidget::setLine(const api::LineConfig& line) {
    line_ = line;
}

void SummaryWidget::setSummary(const api::SimSummary& summary) {
    status_->setText(summary.completed ? tr("Моделирование завершено")
                                       : tr("Моделирование остановлено пользователем"));
    metrics_->setRowCount(0);
    addMetric(tr("Период"), tr("%1 — %2")
                                .arg(modelTimeText(summary.period.start))
                                .arg(modelTimeText(summary.period.end)));
    addMetric(tr("Остановлено на"), modelTimeText(summary.stoppedAt));
    addMetric(tr("Задержки, всего"),
              QString::number(summary.delayCount[0] + summary.delayCount[1]));
    addMetric(tr("Задержки, путь I / путь II"),
              tr("%1 / %2").arg(summary.delayCount[0]).arg(summary.delayCount[1]));
    addMetric(tr("Средняя задержка"), seconds(summary.delayMeanSec));
    addMetric(tr("Доля стоянок с задержкой"), tr("%1%").arg(summary.delayShare * 100.0, 0, 'f', 1));
    addMetric(tr("Интервал прибытия, средний / максимум"),
              tr("%1 / %2 с")
                  .arg(summary.headwayTotal.meanSec, 0, 'f', 1)
                  .arg(summary.headwayTotal.maxSec));
    addMetric(tr("Интервал пути I, средний / максимум"),
              tr("%1 / %2 с")
                  .arg(summary.headwayByDir[0].meanSec, 0, 'f', 1)
                  .arg(summary.headwayByDir[0].maxSec));
    addMetric(tr("Интервал пути II, средний / максимум"),
              tr("%1 / %2 с")
                  .arg(summary.headwayByDir[1].meanSec, 0, 'f', 1)
                  .arg(summary.headwayByDir[1].maxSec));
    addMetric(tr("Максимальное опоздание"), seconds(summary.maxLatenessSec));
    addMetric(tr("Восстановление: эпизоды / среднее / максимум"),
              tr("%1 / %2 / %3 с")
                  .arg(summary.recovery.count)
                  .arg(summary.recovery.meanSec, 0, 'f', 1)
                  .arg(summary.recovery.maxSec));
    addMetric(tr("Удержания: число / добавленное время"),
              tr("%1 / %2 с").arg(summary.holds.count).arg(summary.holds.totalSec));
    addMetric(tr("Замедления: число / добавленное время"),
              tr("%1 / %2 с").arg(summary.slowdowns.count).arg(summary.slowdowns.totalSec));
    addMetric(tr("Стоянки у сигнала: число / время"),
              tr("%1 / %2 с").arg(summary.signalStops.count).arg(summary.signalStops.totalSec));

    updateStationHeadways(summary);
    updateStretchedWindows(summary);
}

void SummaryWidget::addMetric(const QString& name, const QString& value) {
    const int row = metrics_->rowCount();
    metrics_->insertRow(row);
    setCell(metrics_, row, 0, name);
    setCell(metrics_, row, 1, value);
}

void SummaryWidget::updateStationHeadways(const api::SimSummary& summary) {
    const std::size_t count =
        std::max(summary.headwayByStation[0].size(), summary.headwayByStation[1].size());
    stations_->setRowCount(static_cast<int>(count));
    for (std::size_t station = 0; station < count; ++station) {
        setCell(stations_, static_cast<int>(station), 0, stationName(static_cast<int>(station)));
        for (int direction = 0; direction < 2; ++direction) {
            if (station >= summary.headwayByStation[direction].size()) {
                setCell(stations_, static_cast<int>(station), 1 + direction * 2, tr("—"));
                setCell(stations_, static_cast<int>(station), 2 + direction * 2, tr("—"));
                continue;
            }
            const auto& stat = summary.headwayByStation[direction][station];
            setCell(stations_, static_cast<int>(station), 1 + direction * 2, seconds(stat.meanSec));
            setCell(stations_, static_cast<int>(station), 2 + direction * 2, seconds(stat.maxSec));
        }
    }
}

void SummaryWidget::updateStretchedWindows(const api::SimSummary& summary) {
    static const std::array<const char*, 7> dayNames = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    stretches_->setRowCount(static_cast<int>(summary.stretchedWindows.size()));
    for (std::size_t row = 0; row < summary.stretchedWindows.size(); ++row) {
        const auto& stretch = summary.stretchedWindows[row];
        const int day = std::clamp(stretch.dayOfWeek, 0, 6);
        setCell(stretches_, static_cast<int>(row), 0, tr(dayNames[static_cast<std::size_t>(day)]));
        setCell(stretches_, static_cast<int>(row), 1, QString::number(stretch.windowIndex + 1));
        setCell(stretches_, static_cast<int>(row), 2, QString::number(stretch.planned));
        setCell(stretches_, static_cast<int>(row), 3, QString::number(stretch.effective));
    }
}

QString SummaryWidget::stationName(int station) const {
    if (station < 0 || station >= static_cast<int>(line_.stationNames.size())) {
        return tr("Станция %1").arg(station + 1);
    }
    return QString::fromStdString(line_.stationNames[static_cast<std::size_t>(station)]);
}

QString SummaryWidget::seconds(double value) {
    return tr("%1 с").arg(value, 0, 'f', value == static_cast<int64_t>(value) ? 0 : 1);
}

} // namespace ubahn::app
