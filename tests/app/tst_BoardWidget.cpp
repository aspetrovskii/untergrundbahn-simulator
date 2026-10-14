#include "BoardWidget.h"

#include <QLabel>
#include <QTest>

class BoardWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void showsEmptyBoard();
    void countsArrivalAndOccupiedDwell();
    void showsCompletedDwellAndShift();
};

void BoardWidgetTest::showsEmptyBoard() {
    ubahn::app::BoardWidget widget;
    ubahn::api::BoardUpdate directionI;
    ubahn::api::BoardUpdate directionII;
    directionII.dir = ubahn::api::Dir::II;
    widget.setStationName("Центральная");
    widget.setBoards(&directionI, &directionII, 100);

    QCOMPARE(widget.findChild<QLabel*>("boardStationName")->text(),
             QString("Табло станции «Центральная»"));
    QCOMPARE(widget.findChild<QLabel*>("boardArrivalI")->text(), QString("—"));
    QCOMPARE(widget.findChild<QLabel*>("boardDwellII")->text(), QString("0 с"));
    QCOMPARE(widget.findChild<QLabel*>("boardShiftII")->text(), QString("0 с"));
}

void BoardWidgetTest::countsArrivalAndOccupiedDwell() {
    ubahn::app::BoardWidget widget;
    ubahn::api::BoardUpdate board;
    board.dir = ubahn::api::Dir::I;
    board.lastArrival = 90;
    board.occupied = true;
    board.dwellSince = 95;
    board.shift = 12;

    widget.setBoards(&board, nullptr, 110);

    QCOMPARE(widget.findChild<QLabel*>("boardArrivalI")->text(), QString("20 с"));
    QCOMPARE(widget.findChild<QLabel*>("boardDwellI")->text(), QString("15 с"));
    QCOMPARE(widget.findChild<QLabel*>("boardShiftI")->text(), QString("+12 с"));
}

void BoardWidgetTest::showsCompletedDwellAndShift() {
    ubahn::app::BoardWidget widget;
    ubahn::api::BoardUpdate board;
    board.dir = ubahn::api::Dir::II;
    board.lastArrival = 80;
    board.occupied = false;
    board.dwellShown = 42;
    board.shift = 0;

    widget.setBoards(nullptr, &board, 120);

    QCOMPARE(widget.findChild<QLabel*>("boardArrivalII")->text(), QString("40 с"));
    QCOMPARE(widget.findChild<QLabel*>("boardDwellII")->text(), QString("42 с"));
    QCOMPARE(widget.findChild<QLabel*>("boardShiftII")->text(), QString("0 с"));
}

QTEST_MAIN(BoardWidgetTest)
#include "tst_BoardWidget.moc"
