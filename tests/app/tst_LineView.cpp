#include "LineView.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QTest>
#include <cmath>
#include <string>

namespace {

ubahn::api::LineConfig lineWithStations(int stationCount) {
    ubahn::api::LineConfig line;
    for (int station = 0; station < stationCount; ++station) {
        line.stationNames.push_back("Станция " + std::to_string(station + 1));
        if (station + 1 < stationCount) {
            line.spans.push_back({1000.0, 100});
        }
    }
    return line;
}

ubahn::app::SimulationState stateWithTrain(const ubahn::api::TrainUpdate& train,
                                           ubahn::api::SimTime time) {
    ubahn::app::SimulationState state;
    ubahn::api::ViewDelta delta;
    delta.t = time;
    delta.trains.push_back(train);
    state.apply(delta);
    return state;
}

} // namespace

class LineViewTest : public QObject {
    Q_OBJECT

private slots:
    void supportsAllowedStationCounts_data();
    void supportsAllowedStationCounts();
    void stationDistancesAreProportional();
    void positionsTrainOnBothDirections();
    void positionsServiceEdges();
    void animatesUsingLatestVelocityWithoutChangingLabels();
};

void LineViewTest::supportsAllowedStationCounts_data() {
    QTest::addColumn<int>("stationCount");
    QTest::newRow("seven") << 7;
    QTest::newRow("eight") << 8;
    QTest::newRow("twenty") << 20;
}

void LineViewTest::supportsAllowedStationCounts() {
    QFETCH(int, stationCount);
    ubahn::app::LineView view;
    view.setLine(lineWithStations(stationCount));

    QVERIFY(view.stationX(0) > 0.0);
    QVERIFY(view.stationX(stationCount - 1) > view.stationX(0));
}

void LineViewTest::stationDistancesAreProportional() {
    ubahn::api::LineConfig line;
    line.stationNames = {"A", "B", "C"};
    line.spans = {{1000.0, 100}, {2000.0, 150}};

    ubahn::app::LineView view;
    view.setLine(line);

    const double firstDistance = view.stationX(1) - view.stationX(0);
    const double secondDistance = view.stationX(2) - view.stationX(1);
    QVERIFY(std::abs(secondDistance / firstDistance - 2.0) < 0.001);
}

void LineViewTest::positionsTrainOnBothDirections() {
    ubahn::app::LineView view;
    view.setLine(lineWithStations(7));

    ubahn::api::TrainUpdate train;
    train.id = 3;
    train.edge = ubahn::api::TrackEdge::Span;
    train.dir = ubahn::api::Dir::I;
    train.station = 0;
    train.along = 0.5;
    auto state = stateWithTrain(train, 100);
    view.setState(state, 100.0);
    const QPointF directionI = view.trainPosition(3);
    QVERIFY(directionI.x() > view.stationX(0));
    QVERIFY(directionI.x() < view.stationX(1));

    train.dir = ubahn::api::Dir::II;
    train.station = 6;
    state = stateWithTrain(train, 100);
    view.setState(state, 100.0);
    const QPointF directionII = view.trainPosition(3);
    QVERIFY(directionII.x() < view.stationX(6));
    QVERIFY(directionII.x() > view.stationX(5));
    QVERIFY(directionII.y() > directionI.y());
}

void LineViewTest::positionsServiceEdges() {
    ubahn::app::LineView view;
    view.setLine(lineWithStations(7));

    for (const auto edge : {ubahn::api::TrackEdge::Platform, ubahn::api::TrackEdge::ToDeadEnd,
                            ubahn::api::TrackEdge::FromDeadEnd, ubahn::api::TrackEdge::ToDepot,
                            ubahn::api::TrackEdge::FromDepot, ubahn::api::TrackEdge::Depot}) {
        ubahn::api::TrainUpdate train;
        train.id = static_cast<int>(edge);
        train.edge = edge;
        train.dir = ubahn::api::Dir::I;
        train.station = 6;
        train.along = 0.5;
        const auto state = stateWithTrain(train, 100);
        view.setState(state, 100.0);
        const QPointF position = view.trainPosition(train.id);
        QVERIFY(std::isfinite(position.x()));
        QVERIFY(std::isfinite(position.y()));
    }
}

void LineViewTest::animatesUsingLatestVelocityWithoutChangingLabels() {
    ubahn::app::LineView view;
    view.setLine(lineWithStations(7));
    ubahn::api::TrainUpdate train;
    train.id = 3;
    train.station = 0;
    train.edge = ubahn::api::TrackEdge::Span;
    train.along = 0.2;
    train.v = 0.1;
    train.speedKmh = 36.0;
    auto state = stateWithTrain(train, 100);
    view.setState(state, 100.0);
    const double span = view.stationX(1) - view.stationX(0);
    const QPointF start = view.trainPosition(3);
    QGraphicsRectItem* body = nullptr;
    for (auto* item : view.scene()->items()) {
        if (item->data(1).toBool() && item->data(0).toInt() == 3) {
            if (auto* rectangle = qgraphicsitem_cast<QGraphicsRectItem*>(item)) {
                body = rectangle;
            }
        }
    }
    QVERIFY(body != nullptr);
    QCOMPARE(body->childItems().size(), 2);
    auto* label = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(body->childItems().front());
    QVERIFY(label != nullptr);
    const QString labelText = label->text();
    const QString tooltip = body->toolTip();
    const QPointF labelStart = label->scenePos();
    const auto itemCount = view.scene()->items().size();

    view.setModelTime(100.25);

    const QPointF moved = view.trainPosition(3);
    QVERIFY(std::abs(moved.x() - start.x() - span * 0.025) < 0.001);
    QVERIFY(std::abs(label->scenePos().x() - labelStart.x() - span * 0.025) < 0.001);
    QCOMPARE(label->text(), labelText);
    QCOMPARE(body->toolTip(), tooltip);
    QCOMPARE(view.scene()->items().size(), itemCount);

    // Movement data must update even when the label/tooltip fields are unchanged.
    train.along = 0.4;
    train.v = 0.02;
    state = stateWithTrain(train, 105);
    view.setState(state, 105.0);
    view.setModelTime(106.0);
    QVERIFY(std::abs(view.trainPosition(3).x() - (view.stationX(0) + span * 0.42)) < 0.001);
    view.setModelTime(1000.0);
    QCOMPARE(view.trainPosition(3).x(), view.stationX(1));

    train.speedKmh = 18.0;
    train.lateness = 23;
    train.flags.headingToDepot = true;
    view.setState(stateWithTrain(train, 1000), 1000.0);
    QVERIFY(body->toolTip().contains("23"));
    QStringList labels;
    for (auto* child : body->childItems()) {
        labels.push_back(qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child)->text());
    }
    QVERIFY(labels.contains(QString("4 →Д")));
    QVERIFY(labels.contains(QString("18 км/ч")));
}

QTEST_MAIN(LineViewTest)
#include "tst_LineView.moc"
