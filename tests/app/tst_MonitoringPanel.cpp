#include "MonitoringPanel.h"

#include <QLabel>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
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
    void updatesOnlyChangedCellsAndPreservesSelection();
    void keepsWarningJournal();
    void appendsWarningsWithoutLosingHistoryOrScroll();
    void defersHiddenTabsUntilOpened();
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
    panel.findChild<QTabWidget*>("monitoringTabs")->setCurrentWidget(table);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QString("Растянут интервал"));
    QCOMPARE(table->item(0, 1)->text(), QString("Интервал растянут до 150 с"));
}

void MonitoringPanelTest::updatesOnlyChangedCellsAndPreservesSelection() {
    ubahn::app::MonitoringPanel panel;
    panel.resize(500, 300);
    panel.show();
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    for (int id = 10; id < 110; ++id) {
        ubahn::api::TrainUpdate train;
        train.id = id;
        delta.trains.push_back(train);
    }
    state.apply(delta);
    panel.setState(state);
    auto* table = panel.findChild<QTableWidget*>("trainTable");
    QCoreApplication::processEvents();
    table->selectRow(40);
    table->verticalScrollBar()->setValue(30);
    const int scroll = table->verticalScrollBar()->value();
    QVERIFY(scroll > 0);
    auto* selected = table->item(40, 0);
    auto* changedItem = table->item(42, 3);
    QSignalSpy changes(table, &QTableWidget::cellChanged);

    ubahn::api::ViewDelta update;
    auto train = state.trains().at(52);
    train.lateness = 25;
    update.trains.push_back(train);
    state.apply(update);
    panel.setState(state);

    QCOMPARE(changes.count(), 1);
    QCOMPARE(changes.at(0).at(0).toInt(), 42);
    QCOMPARE(changes.at(0).at(1).toInt(), 3);
    QCOMPARE(table->item(42, 3), changedItem);
    QCOMPARE(changedItem->text(), QString("+25 с"));
    QVERIFY(selected->isSelected());
    QCOMPARE(table->verticalScrollBar()->value(), scroll);
    changes.clear();
    panel.setState(state);
    QCOMPARE(changes.count(), 0);

    train.id = 5;
    update.trains = {train};
    state.apply(update);
    panel.setState(state);
    QCOMPARE(table->item(41, 0), selected);
    QVERIFY(selected->isSelected());
    QCOMPARE(selected->text(), QString("51"));
    QCOMPARE(table->verticalScrollBar()->value(), scroll);
}

void MonitoringPanelTest::appendsWarningsWithoutLosingHistoryOrScroll() {
    ubahn::app::MonitoringPanel panel;
    panel.resize(500, 300);
    panel.show();
    auto* table = panel.findChild<QTableWidget*>("warningTable");
    panel.findChild<QTabWidget*>("monitoringTabs")->setCurrentWidget(table);
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    for (int index = 0; index < 100; ++index) {
        ubahn::api::Warning warning;
        warning.message = "Сообщение " + std::to_string(index);
        delta.warnings.push_back(warning);
    }
    state.apply(delta);
    panel.setState(state);
    QCoreApplication::processEvents();
    table->selectRow(20);
    table->verticalScrollBar()->setValue(15);
    const int scroll = table->verticalScrollBar()->value();
    QVERIFY(scroll > 0);
    auto* selected = table->item(20, 1);
    QSignalSpy changes(table, &QTableWidget::cellChanged);
    ubahn::api::ViewDelta update;
    update.warnings = {delta.warnings[0], delta.warnings[0]};
    state.apply(update);

    panel.setState(state);

    QCOMPARE(table->rowCount(), 102);
    QCOMPARE(changes.count(), 4);
    QCOMPARE(table->item(20, 1), selected);
    QVERIFY(selected->isSelected());
    QCOMPARE(table->verticalScrollBar()->value(), scroll);
    for (int index = 0; index < 100; ++index) {
        QCOMPARE(table->item(index, 1)->text(), QString("Сообщение %1").arg(index));
    }
    QCOMPARE(table->item(100, 1)->text(), table->item(0, 1)->text());
    QCOMPARE(table->item(101, 1)->text(), table->item(0, 1)->text());
    changes.clear();
    panel.setState(state);
    QCOMPARE(changes.count(), 0);
}

void MonitoringPanelTest::defersHiddenTabsUntilOpened() {
    ubahn::app::MonitoringPanel panel;
    auto* tabs = panel.findChild<QTabWidget*>("monitoringTabs");
    auto* trains = panel.findChild<QTableWidget*>("trainTable");
    auto* warnings = panel.findChild<QTableWidget*>("warningTable");
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    ubahn::api::TrainUpdate train;
    train.id = 1;
    delta.trains.push_back(train);
    state.apply(delta);
    panel.setState(state);
    auto* original = trains->item(0, 3);
    tabs->setCurrentIndex(2); // Schedule: both data tables are hidden.
    QSignalSpy trainChanges(trains, &QTableWidget::cellChanged);
    QSignalSpy warningChanges(warnings, &QTableWidget::cellChanged);
    for (int index = 1; index <= 2; ++index) {
        delta.trains[0].lateness = index * 10;
        ubahn::api::Warning warning;
        warning.message = std::to_string(index);
        delta.warnings = {warning};
        state.apply(delta);
        panel.setState(state);
    }
    QCOMPARE(trainChanges.count(), 0);
    QCOMPARE(warningChanges.count(), 0);
    QCOMPARE(warnings->rowCount(), 0);
    QCOMPARE(original->text(), QString("0 с"));

    tabs->setCurrentWidget(warnings);
    QCOMPARE(warnings->rowCount(), 2);
    QCOMPARE(warnings->item(0, 1)->text(), QString("1"));
    QCOMPARE(warnings->item(1, 1)->text(), QString("2"));
    QCOMPARE(trainChanges.count(), 0);
    tabs->setCurrentWidget(trains);
    QCOMPARE(trains->item(0, 3), original);
    QCOMPARE(original->text(), QString("+20 с"));
    QCOMPARE(trainChanges.count(), 1);

    state.clear(); // New run must not retain rows from the previous simulation.
    panel.setState(state);
    QCOMPARE(trains->rowCount(), 0);
    tabs->setCurrentWidget(warnings);
    QCOMPARE(warnings->rowCount(), 0);
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
