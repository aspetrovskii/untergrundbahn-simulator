#include "SummaryWidget.h"

#include <QLabel>
#include <QTableWidget>
#include <QTest>

class SummaryWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void showsCompletedSummary();
    void showsStoppedSummary();
};

void SummaryWidgetTest::showsCompletedSummary() {
    ubahn::app::SummaryWidget widget;
    ubahn::api::LineConfig line;
    line.stationNames = {"Северная", "Южная"};
    widget.setLine(line);

    ubahn::api::SimSummary summary;
    summary.period = {6 * 3600, 12 * 3600};
    summary.stoppedAt = summary.period.end;
    summary.completed = true;
    summary.delayCount = {4, 6};
    summary.stopCount = {20, 20};
    summary.delayMeanSec = 35.5;
    summary.delayShare = 0.25;
    summary.headwayTotal = {10, 120.5, 150};
    summary.headwayByDir = {{{5, 118.0, 145}, {5, 123.0, 150}}};
    summary.headwayByStation[0] = {{3, 110.0, 130}, {2, 125.0, 140}};
    summary.headwayByStation[1] = {{3, 115.0, 135}, {2, 128.0, 145}};
    summary.maxLatenessSec = 90;
    summary.recovery = {2, 200.0, 260};
    summary.holds = {1, 30};
    summary.slowdowns = {2, 45};
    summary.signalStops = {1, 20};
    summary.stretchedWindows.push_back({0, 1, 120, 150});

    widget.setSummary(summary);

    QCOMPARE(widget.findChild<QLabel*>("summaryStatus")->text(),
             QString("Моделирование завершено"));
    auto* metrics = widget.findChild<QTableWidget*>("summaryMetrics");
    QVERIFY(metrics->rowCount() >= 12);
    QCOMPARE(metrics->item(2, 1)->text(), QString("10"));
    auto* stations = widget.findChild<QTableWidget*>("summaryStations");
    QCOMPARE(stations->rowCount(), 2);
    QCOMPARE(stations->item(0, 0)->text(), QString("Северная"));
    auto* stretches = widget.findChild<QTableWidget*>("summaryStretches");
    QCOMPARE(stretches->rowCount(), 1);
    QCOMPARE(stretches->item(0, 3)->text(), QString("150"));
}

void SummaryWidgetTest::showsStoppedSummary() {
    ubahn::app::SummaryWidget widget;
    ubahn::api::SimSummary summary;
    summary.completed = false;

    widget.setSummary(summary);

    QCOMPARE(widget.findChild<QLabel*>("summaryStatus")->text(),
             QString("Моделирование остановлено пользователем"));
}

QTEST_MAIN(SummaryWidgetTest)
#include "tst_SummaryWidget.moc"
