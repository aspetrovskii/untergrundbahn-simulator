#include "MonitoringPanel.h"

#include <QLabel>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

namespace {

ubahn::api::LineConfig testLine() {
    ubahn::api::LineConfig line;
    line.stationNames = {"Северная", "Центральная", "Южная"};
    line.spans = {{1000.0, 100}, {1000.0, 100}};
    return line;
}

} // namespace

class MonitoringPanelTest : public QObject {
    Q_OBJECT

private slots:
    void showsTrainState();
    void keepsWarningJournal();
    void showsGroupedDelayNotice();
};

void MonitoringPanelTest::showsTrainState() {
    ubahn::app::MonitoringPanel panel;
    panel.setLine(testLine());
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    ubahn::api::TrainUpdate train;
    train.id = 2;
    train.mode = ubahn::api::TrainMode::CatchUp;
    train.edge = ubahn::api::TrackEdge::Span;
    train.dir = ubahn::api::Dir::I;
    train.station = 0;
    train.lateness = 15;
    train.flags.catchingUp = true;
    delta.trains.push_back(train);
    state.apply(delta);

    panel.setState(state);

    auto* table = panel.findChild<QTableWidget*>("trainTable");
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QString("3"));
    QCOMPARE(table->item(0, 1)->text(), QString("Нагон"));
    QVERIFY(table->item(0, 2)->text().contains("Северная → Центральная"));
    QCOMPARE(table->item(0, 3)->text(), QString("+15 с"));
    QCOMPARE(table->item(0, 4)->text(), QString("нагоняет"));
}

void MonitoringPanelTest::keepsWarningJournal() {
    ubahn::app::MonitoringPanel panel;
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    ubahn::api::Warning warning;
    warning.code = ubahn::api::WarningCode::IntervalStretched;
    warning.message = "Интервал растянут до 150 с";
    delta.warnings.push_back(warning);
    state.apply(delta);

    panel.setState(state);

    auto* table = panel.findChild<QTableWidget*>("warningTable");
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QString("Растянут интервал"));
    QCOMPARE(table->item(0, 1)->text(), QString("Интервал растянут до 150 с"));
}

void MonitoringPanelTest::showsGroupedDelayNotice() {
    ubahn::app::MonitoringPanel panel;
    panel.setLine(testLine());
    const std::vector<ubahn::api::DelayNotice> notices = {
        {0, ubahn::api::Dir::I, 1, 30},
        {2, ubahn::api::Dir::II, 2, 45},
    };

    panel.showDelayNotices(notices);

    auto* banner = panel.findChild<QLabel*>("delayBanner");
    auto* timer = panel.findChild<QTimer*>("delayTimer");
    QVERIFY(!banner->isHidden());
    QVERIFY(banner->text().contains("Поезд 1"));
    QVERIFY(banner->text().contains("Поезд 3"));
    QCOMPARE(timer->interval(), 4000);
    QVERIFY(timer->isActive());
}

QTEST_MAIN(MonitoringPanelTest)
#include "tst_MonitoringPanel.moc"
