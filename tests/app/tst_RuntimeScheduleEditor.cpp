#include "RuntimeScheduleEditor.h"
#include "ScheduleEditor.h"
#include "api/Functions.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

class RuntimeScheduleEditorTest : public QObject {
    Q_OBJECT

private slots:
    void validScheduleIsAccepted();
    void invalidScheduleIsRejected();
};

void RuntimeScheduleEditorTest::validScheduleIsAccepted() {
    const auto preset = ubahn::api::builtinPresets().front();
    ubahn::app::RuntimeScheduleEditor editor;
    editor.setLine(preset.params.line);
    editor.setSchedule(preset.params.schedule);

    int acceptedCount = 0;
    ubahn::api::ScheduleConfig acceptedSchedule;
    connect(&editor, &ubahn::app::RuntimeScheduleEditor::scheduleAccepted, &editor,
            [&](const ubahn::api::ScheduleConfig& schedule) {
                ++acceptedCount;
                acceptedSchedule = schedule;
            });

    QTest::mouseClick(editor.findChild<QPushButton*>("applyScheduleButton"), Qt::LeftButton);

    QCOMPARE(acceptedCount, 1);
    QCOMPARE(acceptedSchedule.days[0].windows.size(),
             preset.params.schedule.days[0].windows.size());
    auto* status = editor.findChild<QLabel*>("runtimeScheduleStatus");
    QVERIFY(status->text().contains("передано"));
}

void RuntimeScheduleEditorTest::invalidScheduleIsRejected() {
    const auto preset = ubahn::api::builtinPresets().front();
    ubahn::app::RuntimeScheduleEditor editor;
    editor.setLine(preset.params.line);
    editor.setSchedule(preset.params.schedule);

    int acceptedCount = 0;
    connect(&editor, &ubahn::app::RuntimeScheduleEditor::scheduleAccepted, &editor,
            [&](const ubahn::api::ScheduleConfig&) { ++acceptedCount; });
    auto* scheduleEditor = editor.findChild<ubahn::app::ScheduleEditor*>("scheduleEditor");
    scheduleEditor->dayTable(0)->item(0, 0)->setText("ошибка");

    QTest::mouseClick(editor.findChild<QPushButton*>("applyScheduleButton"), Qt::LeftButton);

    QCOMPARE(acceptedCount, 0);
    auto* status = editor.findChild<QLabel*>("runtimeScheduleStatus");
    QVERIFY(status->text().contains("Исправьте ошибки"));
    QVERIFY(status->text().contains("ЧЧ:ММ"));
}

QTEST_MAIN(RuntimeScheduleEditorTest)
#include "tst_RuntimeScheduleEditor.moc"
