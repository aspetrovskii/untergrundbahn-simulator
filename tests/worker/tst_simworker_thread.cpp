#include "api/Functions.h"
#include "api/SimController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

namespace {

ubahn::api::SimParams pausedRun() {
    ubahn::api::SimParams params = ubahn::api::defaultParams();
    params.period.start = 3 * 3600;
    params.period.end = params.period.start + 120;
    params.stepSec = 60;
    params.rate = 0;
    params.seed = 7;
    return params;
}

} // namespace

class SimWorkerThreadTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void queuedCommandsSignalGuiThreadAndDeleteLater();
};

void SimWorkerThreadTest::init() {
    ubahn::api::registerMetaTypes();
}

void SimWorkerThreadTest::queuedCommandsSignalGuiThreadAndDeleteLater() {
    QThread thread;
    ubahn::api::SimController* ctrl = ubahn::api::createSimController().release();
    ctrl->moveToThread(&thread);
    connect(&thread, &QThread::finished, ctrl, &QObject::deleteLater);

    QPointer<ubahn::api::SimController> alive(ctrl);
    [[maybe_unused]] const auto shutdown = qScopeGuard([&] {
        if (alive != nullptr) {
            QMetaObject::invokeMethod(alive, &QObject::deleteLater, Qt::QueuedConnection);
            QElapsedTimer timer;
            timer.start();
            while (alive != nullptr && timer.elapsed() < 3000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        thread.quit();
        thread.wait(3000);
    });

    QSignalSpy errors(ctrl, &ubahn::api::SimController::error);
    QSignalSpy started(ctrl, &ubahn::api::SimController::started);
    QSignalSpy chunks(ctrl, &ubahn::api::SimController::chunkReady);
    QSignalSpy summary(ctrl, &ubahn::api::SimController::summaryReady);
    QVERIFY(errors.isValid());

    QThread* signalThread = nullptr;
    connect(ctrl, &ubahn::api::SimController::chunkReady, this,
            [&](const ubahn::api::ViewChunkPtr&) { signalThread = QThread::currentThread(); });

    thread.start();
    QVERIFY(thread.isRunning());

    QVERIFY(QMetaObject::invokeMethod(ctrl, [ctrl] { ctrl->stop(); }, Qt::QueuedConnection));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 3000);
    QCOMPARE(errors.count(), 1);
    const auto err = errors.at(0).at(0).value<ubahn::api::RuntimeError>();
    QVERIFY(err.code == ubahn::api::ErrorCode::InvalidState);

    const ubahn::api::SimParams params = pausedRun();
    QVERIFY(QMetaObject::invokeMethod(
        ctrl, [ctrl, params] { ctrl->start(params); }, Qt::QueuedConnection));
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 3000);
    // started and Initial are emitted in one slot, so the chunk may already be queued.
    QTRY_COMPARE(chunks.count(), 1);
    QTRY_COMPARE(signalThread, QThread::currentThread());
    QVERIFY(signalThread != &thread);

    const auto info = started.at(0).at(0).value<ubahn::api::RunInfo>();
    QCOMPARE(info.seed, static_cast<uint64_t>(7));
    QCOMPARE(chunks.count(), 1);

    QVERIFY(QMetaObject::invokeMethod(ctrl, [ctrl] { ctrl->stop(); }, Qt::QueuedConnection));
    QTRY_COMPARE_WITH_TIMEOUT(summary.count(), 1, 3000);
    QCOMPARE(summary.count(), 1);
    const auto done = summary.at(0).at(0).value<ubahn::api::SimSummary>();
    QVERIFY(!done.completed);

    QVERIFY(QMetaObject::invokeMethod(ctrl, &QObject::deleteLater, Qt::QueuedConnection));
    QTRY_VERIFY(alive.isNull());
    thread.quit();
    QVERIFY(thread.wait(3000));
    QVERIFY(!thread.isRunning());
}

QTEST_GUILESS_MAIN(SimWorkerThreadTest)

#include "tst_simworker_thread.moc"
