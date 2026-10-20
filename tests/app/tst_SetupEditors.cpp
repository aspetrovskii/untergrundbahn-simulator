#include "LineEditor.h"
#include "ScheduleEditor.h"
#include "SetupDialog.h"
#include "api/Functions.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

class SetupEditorsTest : public QObject {
    Q_OBJECT

private slots:
    void lineEditorLoadsPreset();
    void lineEditorResizesStations();
    void scheduleEditorShowsSevenDays();
    void setupSupportsDuration();
    void setupShowsValidationErrors();
};

void SetupEditorsTest::lineEditorLoadsPreset() {
    const auto preset = ubahn::api::builtinPresets().front();
    ubahn::app::LineEditor editor;
    editor.setLine(preset.params.line);

    const auto line = editor.line();
    QCOMPARE(line.stationNames, preset.params.line.stationNames);
    QCOMPARE(line.spans.size(), preset.params.line.spans.size());
    QCOMPARE(line.spans.front().lengthM, preset.params.line.spans.front().lengthM);
    QCOMPARE(line.fleetSize, preset.params.line.fleetSize);
    QVERIFY(editor.inputErrors().empty());
}

void SetupEditorsTest::lineEditorResizesStations() {
    ubahn::app::LineEditor editor;
    auto* count = editor.findChild<QSpinBox*>("stationCountSpin");
    auto* table = editor.findChild<QTableWidget*>("stationsTable");

    count->setValue(20);

    QCOMPARE(table->rowCount(), 20);
    QCOMPARE(editor.line().stationNames.size(), std::size_t{20});
    QCOMPARE(table->item(19, 1)->text(), QString("—"));
}

void SetupEditorsTest::scheduleEditorShowsSevenDays() {
    const auto schedule = ubahn::api::defaultSchedule();
    ubahn::app::ScheduleEditor editor;
    editor.setSchedule(schedule);

    auto* tabs = editor.findChild<QTabWidget*>("scheduleDays");
    QCOMPARE(tabs->count(), 7);
    for (int day = 0; day < 7; ++day) {
        QCOMPARE(editor.dayTable(day)->rowCount(),
                 static_cast<int>(schedule.days[static_cast<std::size_t>(day)].windows.size()));
    }
    QVERIFY(editor.inputErrors().empty());
    QCOMPARE(editor.schedule().days[0].windows.back().toTod, ubahn::api::kCloseTod);
}

void SetupEditorsTest::setupSupportsDuration() {
    ubahn::app::SetupDialog dialog;
    auto* mode = dialog.findChild<QComboBox*>("periodModeCombo");
    auto* duration = dialog.findChild<QSpinBox*>("durationHoursSpin");
    mode->setCurrentIndex(1);
    duration->setValue(30);

    const auto params = dialog.selectedParams();
    QCOMPARE(params.period.end - params.period.start, ubahn::api::SimTime{30 * 3600});
}

void SetupEditorsTest::setupShowsValidationErrors() {
    ubahn::app::SetupDialog dialog;
    dialog.findChild<QSpinBox*>("delayMinSpin")->setValue(100);
    dialog.findChild<QSpinBox*>("delayMaxSpin")->setValue(50);
    auto* startButton = dialog.findChild<QPushButton*>("startButton");
    auto* errors = dialog.findChild<QLabel*>("validationErrors");
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);

    QTest::mouseClick(startButton, Qt::LeftButton);

    QCOMPARE(acceptedSpy.count(), 0);
    QVERIFY(errors->isVisibleTo(&dialog));
    QVERIFY(errors->text().contains("Минимальная задержка"));
}

QTEST_MAIN(SetupEditorsTest)
#include "tst_SetupEditors.moc"
