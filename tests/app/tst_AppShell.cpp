#include "BoardWidget.h"
#include "LineView.h"
#include "MainWindow.h"
#include "MonitoringPanel.h"
#include "PlaybackPanel.h"
#include "SetupDialog.h"
#include "SummaryWidget.h"
#include "api/Version.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class AppShellTest : public QObject {
    Q_OBJECT

private slots:
    void apiVersionIsExpected();
    void setupDialogShowsPresets();
    void setupDialogAcceptsSelection();
    void mainWindowHasLineView();
};

void AppShellTest::apiVersionIsExpected() {
    QCOMPARE(ubahn::api::kApiVersionMajor, 0);
    QCOMPARE(ubahn::api::kApiVersionMinor, 1);
    QCOMPARE(ubahn::api::kApiVersionPatch, 0);
}

void AppShellTest::setupDialogShowsPresets() {
    ubahn::app::SetupDialog dialog;
    auto* combo = dialog.findChild<QComboBox*>("presetCombo");

    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentIndex(), 0);
    QCOMPARE(dialog.selectedParams().line.stationNames.size(), std::size_t{8});
}

void AppShellTest::setupDialogAcceptsSelection() {
    ubahn::app::SetupDialog dialog;
    auto* combo = dialog.findChild<QComboBox*>("presetCombo");
    auto* startButton = dialog.findChild<QPushButton*>("startButton");
    QVERIFY(combo != nullptr);
    QVERIFY(startButton != nullptr);

    combo->setCurrentIndex(2);
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);
    QTest::mouseClick(startButton, Qt::LeftButton);

    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(dialog.selectedParams().line.stationNames.size(), std::size_t{20});
}

void AppShellTest::mainWindowHasLineView() {
    ubahn::app::SetupDialog dialog;
    ubahn::app::MainWindow window(dialog.selectedParams());

    QVERIFY(window.findChild<ubahn::app::LineView*>("lineView") != nullptr);
    QVERIFY(window.findChild<ubahn::app::BoardWidget*>("boardWidget") != nullptr);
    QVERIFY(window.findChild<ubahn::app::PlaybackPanel*>("playbackPanel") != nullptr);
    QVERIFY(window.findChild<ubahn::app::MonitoringPanel*>("monitoringPanel") != nullptr);
    QVERIFY(window.findChild<ubahn::app::SummaryWidget*>("summaryWidget") != nullptr);
    QVERIFY(window.findChild<QPushButton*>("stopButton") != nullptr);
    auto* stationSelector = window.findChild<QComboBox*>("boardStationSelector");
    QVERIFY(stationSelector != nullptr);
    QCOMPARE(stationSelector->count(), 8);
    QCOMPARE(window.windowTitle(), QString("Untergrundbahn Simulator"));
}

QTEST_MAIN(AppShellTest)
#include "tst_AppShell.moc"
