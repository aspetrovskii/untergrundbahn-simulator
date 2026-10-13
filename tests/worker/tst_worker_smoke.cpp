#include "api/SimController.h"

#include <QSignalSpy>
#include <QString>
#include <QtTest>

class WorkerSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void stopWhileIdleEmitsInvalidState();
};

void WorkerSmokeTest::stopWhileIdleEmitsInvalidState() {
    ubahn::api::registerMetaTypes();

    const std::unique_ptr<ubahn::api::SimController> controller = ubahn::api::createSimController();
    QVERIFY(controller != nullptr);

    QSignalSpy spy(controller.get(), &ubahn::api::SimController::error);
    QVERIFY(spy.isValid());

    controller->stop();

    QCOMPARE(spy.count(), 1);
    const auto err = spy.at(0).at(0).value<ubahn::api::RuntimeError>();
    QVERIFY(err.code == ubahn::api::ErrorCode::InvalidState);
    QCOMPARE(QString::fromStdString(err.message), QString("not implemented"));
}

QTEST_GUILESS_MAIN(WorkerSmokeTest)

#include "tst_worker_smoke.moc"
