#include "RuntimeErrorBanner.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class RuntimeErrorBannerTest : public QObject {
    Q_OBJECT

private slots:
    void showsRestartForInternalError();
    void clearsMessage();
};

void RuntimeErrorBannerTest::showsRestartForInternalError() {
    ubahn::app::RuntimeErrorBanner banner;
    QSignalSpy restartSpy(&banner, &ubahn::app::RuntimeErrorBanner::restartRequested);

    banner.showError("Симуляция остановлена", "Нарушен внутренний инвариант", true);

    QVERIFY(!banner.isHidden());
    QVERIFY(banner.findChild<QLabel*>("runtimeErrorMessage")->text().contains("инвариант"));
    auto* restart = banner.findChild<QPushButton*>("restartSimulationButton");
    QVERIFY(!restart->isHidden());
    QTest::mouseClick(restart, Qt::LeftButton);
    QCOMPARE(restartSpy.count(), 1);
}

void RuntimeErrorBannerTest::clearsMessage() {
    ubahn::app::RuntimeErrorBanner banner;
    banner.showError("Ошибка", {}, false);

    banner.clear();

    QVERIFY(banner.isHidden());
    QVERIFY(banner.findChild<QLabel*>("runtimeErrorMessage")->text().isEmpty());
}

QTEST_MAIN(RuntimeErrorBannerTest)
#include "tst_RuntimeErrorBanner.moc"
