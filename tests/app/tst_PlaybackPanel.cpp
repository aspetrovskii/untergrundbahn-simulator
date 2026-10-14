#include "PlaybackPanel.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class PlaybackPanelTest : public QObject {
    Q_OBJECT

private slots:
    void showsModelClockAndWindow();
    void pausesAndResumes();
    void selectsStep();
    void requestsForwardSeek();
    void requestsStopAndPauses();
};

void PlaybackPanelTest::showsModelClockAndWindow() {
    ubahn::app::PlaybackPanel panel;
    ubahn::api::ClockUpdate clock;
    clock.dayOfWeek = 0;
    clock.open = true;
    clock.windowIndex = 1;
    clock.window = {7 * 3600, 10 * 3600, 120, 80, 0.55};
    clock.hEff = 150;

    panel.setModelState(7 * 3600 + 5 * 60 + 9, clock);

    QCOMPARE(panel.findChild<QLabel*>("modelClockLabel")->text(), QString("Понедельник, 07:05:09"));
    const QString windowText = panel.findChild<QLabel*>("currentWindowLabel")->text();
    QVERIFY(windowText.contains("07:00–10:00"));
    QVERIFY(windowText.contains("интервал 120 с"));
    QVERIFY(windowText.contains("эффективный 150 с"));
    QVERIFY(windowText.contains("задержка 55%"));
}

void PlaybackPanelTest::pausesAndResumes() {
    ubahn::app::PlaybackPanel panel;
    panel.setInitialValues(2.0, 60);
    QSignalSpy rateSpy(&panel, &ubahn::app::PlaybackPanel::rateChanged);
    auto* pauseButton = panel.findChild<QPushButton*>("pauseButton");

    QTest::mouseClick(pauseButton, Qt::LeftButton);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(rateSpy.takeFirst().at(0).toDouble(), 0.0);
    QCOMPARE(pauseButton->text(), QString("Продолжить"));

    QTest::mouseClick(pauseButton, Qt::LeftButton);
    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(rateSpy.takeFirst().at(0).toDouble(), 2.0);
    QCOMPARE(pauseButton->text(), QString("Пауза"));
}

void PlaybackPanelTest::selectsStep() {
    ubahn::app::PlaybackPanel panel;
    QSignalSpy stepSpy(&panel, &ubahn::app::PlaybackPanel::stepChanged);
    auto* stepCombo = panel.findChild<QComboBox*>("stepCombo");

    stepCombo->setCurrentIndex(stepCombo->findData(120));

    QCOMPARE(stepSpy.count(), 1);
    QCOMPARE(stepSpy.takeFirst().at(0).toInt(), 120);
}

void PlaybackPanelTest::requestsForwardSeek() {
    ubahn::app::PlaybackPanel panel;
    panel.setModelState(1000, std::nullopt);
    QSignalSpy seekSpy(&panel, &ubahn::app::PlaybackPanel::seekForwardRequested);
    auto* seekCombo = panel.findChild<QComboBox*>("seekCombo");
    auto* seekButton = panel.findChild<QPushButton*>("seekButton");
    seekCombo->setCurrentIndex(seekCombo->findData(5 * 60));

    QTest::mouseClick(seekButton, Qt::LeftButton);

    QCOMPARE(seekSpy.count(), 1);
    QCOMPARE(seekSpy.takeFirst().at(0).value<ubahn::api::SimTime>(), ubahn::api::SimTime{1300});
}

void PlaybackPanelTest::requestsStopAndPauses() {
    ubahn::app::PlaybackPanel panel;
    panel.setInitialValues(1.0, 60);
    QSignalSpy rateSpy(&panel, &ubahn::app::PlaybackPanel::rateChanged);
    QSignalSpy stopSpy(&panel, &ubahn::app::PlaybackPanel::stopRequested);
    auto* stopButton = panel.findChild<QPushButton*>("stopButton");

    QTest::mouseClick(stopButton, Qt::LeftButton);

    QCOMPARE(rateSpy.count(), 1);
    QCOMPARE(rateSpy.takeFirst().at(0).toDouble(), 0.0);
    QCOMPARE(stopSpy.count(), 1);
    QVERIFY(!stopButton->isEnabled());
}

QTEST_MAIN(PlaybackPanelTest)
#include "tst_PlaybackPanel.moc"
