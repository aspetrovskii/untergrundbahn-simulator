#pragma once

#include "SimulationState.h"
#include "api/Types.h"

#include <QGraphicsView>
#include <QString>
#include <map>
#include <vector>

class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;
class QMouseEvent;
class QWheelEvent;

namespace ubahn::app {

class LineView : public QGraphicsView {
    Q_OBJECT

public:
    explicit LineView(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    void setState(const SimulationState& state, double modelTime);
    void setModelTime(double modelTime);
    void fitToLine();

    double stationX(int station) const;
    QPointF trainPosition(int trainId) const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct TrainGraphics {
        QGraphicsRectItem* body = nullptr;
        QGraphicsSimpleTextItem* number = nullptr;
        QGraphicsSimpleTextItem* speed = nullptr;
    };

    void drawStaticLine();
    void drawTerminal(int station, bool leftSide);
    void updateTrainAppearance(const api::TrainUpdate& train);
    QPointF positionFor(const api::TrainUpdate& train, api::SimTime updateTime,
                        double modelTime) const;
    QPointF deadEndPoint(int station) const;
    QPointF depotPoint(int station) const;
    double trackY(api::Dir dir) const;
    QString trainDescription(const api::TrainUpdate& train) const;

    QGraphicsScene* scene_ = nullptr;
    api::LineConfig line_;
    std::vector<double> stationPositions_;
    std::map<int, TrainGraphics> trainItems_;
    std::map<int, api::TrainUpdate> trainStates_;
    std::map<int, api::SimTime> trainUpdateTimes_;
};

} // namespace ubahn::app
