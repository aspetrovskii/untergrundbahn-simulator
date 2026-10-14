#include "AppSession.h"
#include "api/Functions.h"

#include <QSignalSpy>
#include <QTest>

class AppSessionTest : public QObject {
    Q_OBJECT

private slots:
    void forwardsMockSignalsFromWorkerThread();
};

void AppSessionTest::forwardsMockSignalsFromWorkerThread() {
    ubahn::app::AppSession session;
    QSignalSpy startedSpy(&session, &ubahn::app::AppSession::started);
    QSignalSpy chunkSpy(&session, &ubahn::app::AppSession::chunkReady);
    QSignalSpy summarySpy(&session, &ubahn::app::AppSession::summaryReady);

    session.start(ubahn::api::defaultParams());

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(chunkSpy.count(), 4, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(summarySpy.count(), 1, 1000);
}

QTEST_GUILESS_MAIN(AppSessionTest)
#include "tst_AppSession.moc"
