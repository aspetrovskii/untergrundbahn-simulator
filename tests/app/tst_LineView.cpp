#include "LineView.h"

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

QTEST_MAIN(LineViewTest)
#include "tst_LineView.moc"
