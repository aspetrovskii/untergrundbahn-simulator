#include "AppSession.h"
#include "BoardWidget.h"
#include "LineView.h"
#include "MainWindow.h"
#include "MonitoringPanel.h"
#include "PlaybackController.h"
#include "PlaybackPanel.h"
#include "RuntimeErrorBanner.h"
#include "RuntimeScheduleEditor.h"
#include "SetupDialog.h"
#include "SummaryWidget.h"
#include "api/Functions.h"
#include "api/Version.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <memory>

class AppShellTest : public QObject {
    Q_OBJECT

private slots:
    void apiVersionIsExpected();
    void setupDialogShowsPresets();
    void setupDialogAcceptsSelection();
    void mainWindowHasLineView();
    void mainWindowHandlesInternalError();
    void mainWindowAnimatesFramesAndShowsSeekState();
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
    QCOMPARE(combo->count(), static_cast<int>(ubahn::api::builtinPresets().size()));
    QCOMPARE(combo->currentIndex(), 0);
    QCOMPARE(dialog.selectedParams().line.stationNames.size(), std::size_t{8});
}

void AppShellTest::setupDialogAcceptsSelection() {
    ubahn::app::SetupDialog dialog;
    auto* combo = dialog.findChild<QComboBox*>("presetCombo");
    auto* startButton = dialog.findChild<QPushButton*>("startButton");
    QVERIFY(combo != nullptr);
    QVERIFY(startButton != nullptr);

    const auto presets = ubahn::api::builtinPresets();
    combo->setCurrentIndex(combo->count() - 1);
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);
    QTest::mouseClick(startButton, Qt::LeftButton);

    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(dialog.selectedParams().line.stationNames.size(),
             presets.back().params.line.stationNames.size());
    auto* stationCount = dialog.findChild<QSpinBox*>("stationCountSpin");
    QVERIFY(stationCount != nullptr);
    QCOMPARE(stationCount->value(),
             static_cast<int>(presets.back().params.line.stationNames.size()));
}

void AppShellTest::mainWindowHasLineView() {
    ubahn::app::SetupDialog dialog;
    ubahn::app::MainWindow window(dialog.selectedParams());

    QVERIFY(window.findChild<ubahn::app::LineView*>("lineView") != nullptr);
    QVERIFY(window.findChild<ubahn::app::BoardWidget*>("boardWidget") != nullptr);
    QVERIFY(window.findChild<ubahn::app::PlaybackPanel*>("playbackPanel") != nullptr);
    QVERIFY(window.findChild<ubahn::app::MonitoringPanel*>("monitoringPanel") != nullptr);
    QVERIFY(window.findChild<ubahn::app::RuntimeScheduleEditor*>("runtimeScheduleEditor") !=
            nullptr);
    QVERIFY(window.findChild<ubahn::app::RuntimeErrorBanner*>("runtimeErrorBanner") != nullptr);
    QVERIFY(window.findChild<ubahn::app::SummaryWidget*>("summaryWidget") != nullptr);
    QVERIFY(window.findChild<QPushButton*>("stopButton") != nullptr);
    auto* stationSelector = window.findChild<QComboBox*>("boardStationSelector");
    QVERIFY(stationSelector != nullptr);
    QCOMPARE(stationSelector->count(), 8);
    QCOMPARE(window.windowTitle(), QString("Untergrundbahn Simulator"));
}

void AppShellTest::mainWindowHandlesInternalError() {
    ubahn::app::SetupDialog dialog;
    ubahn::app::MainWindow window(dialog.selectedParams());
    ubahn::api::RuntimeError error;
    error.code = ubahn::api::ErrorCode::InternalError;
    error.message = "Тестовая внутренняя ошибка";

    window.findChild<ubahn::app::AppSession*>()->errorOccurred(error);

    auto* banner = window.findChild<ubahn::app::RuntimeErrorBanner*>("runtimeErrorBanner");
    QVERIFY(!banner->isHidden());
    QVERIFY(banner->findChild<QLabel*>("runtimeErrorMessage")->text().contains("Тестовая"));
    QVERIFY(!window.findChild<QPushButton*>("pauseButton")->isEnabled());
    QVERIFY(window.findChild<QPushButton*>("restartSimulationButton")->isVisibleTo(&window));
}

void AppShellTest::mainWindowAnimatesFramesAndShowsSeekState() {
    ubahn::app::SetupDialog dialog;
    auto params = dialog.selectedParams();
    params.rate = 0.0;
    ubahn::app::MainWindow window(params);
    // Isolate rendering from the static mock's immediate summary.
    auto* session = window.findChild<ubahn::app::AppSession*>();
    QObject::disconnect(session, nullptr, nullptr, nullptr);
    auto* playback = window.findChild<ubahn::app::PlaybackController*>();
    auto* view = window.findChild<ubahn::app::LineView*>("lineView");
    playback->reset();
    auto initial = std::make_shared<ubahn::api::ViewChunk>();
    initial->kind = ubahn::api::ChunkKind::Initial;
    initial->from = initial->to = 100;
    ubahn::api::ViewDelta delta;
    delta.t = 100;
    ubahn::api::TrainUpdate train;
    train.id = 1;
    train.edge = ubahn::api::TrackEdge::Span;
    train.along = 0.1;
    train.v = 0.01;
    delta.trains.push_back(train);
    initial->deltas.push_back(delta);
    playback->addChunk(initial);
    const QPointF start = view->trainPosition(1);
    auto play = std::make_shared<ubahn::api::ViewChunk>();
    play->seq = 1;
    play->kind = ubahn::api::ChunkKind::Play;
    play->from = 100;
    play->to = 110;
    delta.t = 105;
    delta.trains[0].along = 0.6;
    delta.trains[0].lateness = 25;
    play->deltas.push_back(delta);
    playback->addChunk(play);
    playback->setRate(0.01);

    QTRY_VERIFY_WITH_TIMEOUT(view->trainPosition(1).x() > start.x(), 500);
    QCOMPARE(playback->currentTime(), ubahn::api::SimTime{100});
    playback->setRate(0.0);
    const QPointF paused = view->trainPosition(1);
    QTest::qWait(40);
    QCOMPARE(view->trainPosition(1), paused);
    playback->seekForward(107);
    QVERIFY(view->trainPosition(1).x() > paused.x());
    QVERIFY(window.findChild<QLabel*>("modelClockLabel")->text().contains("00:01:47"));

    auto* pauseButton = window.findChild<QPushButton*>("pauseButton");
    QVERIFY(pauseButton->isEnabled());
    QTest::mouseClick(pauseButton, Qt::LeftButton);
    QCOMPARE(playback->rate(), 1.0);
    const QPointF resumed = view->trainPosition(1);
    QTRY_VERIFY_WITH_TIMEOUT(view->trainPosition(1).x() > resumed.x(), 500);
    QTest::mouseClick(pauseButton, Qt::LeftButton);
    QCOMPARE(playback->rate(), 0.0);
    const QPointF pausedByButton = view->trainPosition(1);
    QTest::qWait(40);
    QCOMPARE(view->trainPosition(1), pausedByButton);
}

QTEST_MAIN(AppShellTest)
#include "tst_AppShell.moc"
