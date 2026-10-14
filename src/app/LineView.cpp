#include "LineView.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QToolTip>
#include <QVariant>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace ubahn::app {
namespace {

constexpr double kSceneWidth = 1440.0;
constexpr double kSceneHeight = 520.0;
constexpr double kLineLeft = 220.0;
constexpr double kLineWidth = 1000.0;
constexpr double kTrackIY = 210.0;
constexpr double kTrackIIY = 350.0;
constexpr double kTerminalY = 280.0;
constexpr double kTrainWidth = 36.0;
constexpr double kTrainHeight = 20.0;

QColor modeColor(api::TrainMode mode) {
    switch (mode) {
    case api::TrainMode::Stopped:
        return QColor(210, 55, 55);
    case api::TrainMode::Service:
        return QColor(115, 115, 115);
    case api::TrainMode::Slowed:
        return QColor(230, 145, 35);
    case api::TrainMode::CatchUp:
        return QColor(45, 115, 210);
    case api::TrainMode::OnTime:
        return QColor(45, 155, 75);
    }
    return QColor(115, 115, 115);
}

QString modeName(api::TrainMode mode) {
    switch (mode) {
    case api::TrainMode::Stopped:
        return LineView::tr("Остановлен");
    case api::TrainMode::Service:
        return LineView::tr("Служебный");
    case api::TrainMode::Slowed:
        return LineView::tr("Замедление");
    case api::TrainMode::CatchUp:
        return LineView::tr("Нагон");
    case api::TrainMode::OnTime:
        return LineView::tr("По графику");
    }
    return LineView::tr("Прочее");
}

QString edgeName(api::TrackEdge edge) {
    switch (edge) {
    case api::TrackEdge::Span:
        return LineView::tr("перегон");
    case api::TrackEdge::Platform:
        return LineView::tr("платформа");
    case api::TrackEdge::ToDeadEnd:
        return LineView::tr("в тупик");
    case api::TrackEdge::FromDeadEnd:
        return LineView::tr("из тупика");
    case api::TrackEdge::ToDepot:
        return LineView::tr("в депо");
    case api::TrackEdge::FromDepot:
        return LineView::tr("из депо");
    case api::TrackEdge::Depot:
        return LineView::tr("депо");
    }
    return LineView::tr("прочее");
}

} // namespace

LineView::LineView(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setObjectName("lineView");
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QColor(248, 248, 248));
    scene_->setSceneRect(0.0, 0.0, kSceneWidth, kSceneHeight);
}

void LineView::setLine(const api::LineConfig& line) {
    line_ = line;
    stationPositions_.clear();
    trainItems_.clear();
    trainStates_.clear();
    scene_->clear();

    double totalLength = 0.0;
    for (const auto& span : line_.spans) {
        totalLength += span.lengthM;
    }
    if (totalLength <= 0.0 || line_.stationNames.empty()) {
        return;
    }

    double passedLength = 0.0;
    stationPositions_.push_back(kLineLeft);
    for (const auto& span : line_.spans) {
        passedLength += span.lengthM;
        stationPositions_.push_back(kLineLeft + kLineWidth * passedLength / totalLength);
    }

    drawStaticLine();
    QTimer::singleShot(0, this, &LineView::fitToLine);
}

void LineView::setState(const SimulationState& state, double modelTime) {
    if (stationPositions_.empty()) {
        return;
    }
    for (const auto& [id, train] : state.trains()) {
        trainStates_[id] = train;
        updateTrain(train, state.trainUpdateTime(id), modelTime);
    }
}

void LineView::fitToLine() {
    if (!scene_->items().empty()) {
        fitInView(scene_->itemsBoundingRect().adjusted(-30.0, -30.0, 30.0, 30.0),
                  Qt::KeepAspectRatio);
    }
}

double LineView::stationX(int station) const {
    if (station < 0 || station >= static_cast<int>(stationPositions_.size())) {
        return 0.0;
    }
    return stationPositions_[static_cast<std::size_t>(station)];
}

QPointF LineView::trainPosition(int trainId) const {
    const auto item = trainItems_.find(trainId);
    return item == trainItems_.end() ? QPointF{} : item->second.body->scenePos();
}

void LineView::mousePressEvent(QMouseEvent* event) {
    QGraphicsItem* clicked = itemAt(event->position().toPoint());
    if (clicked && clicked->data(1).toBool()) {
        const int trainId = clicked->data(0).toInt();
        const auto train = trainStates_.find(trainId);
        if (train != trainStates_.end()) {
            QToolTip::showText(event->globalPosition().toPoint(), trainDescription(train->second),
                               this);
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void LineView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double nextScale = transform().m11() * factor;
    if (nextScale >= 0.25 && nextScale <= 8.0) {
        scale(factor, factor);
    }
    event->accept();
}

void LineView::drawStaticLine() {
    const QPen trackPen(QColor(70, 70, 70), 5.0, Qt::SolidLine, Qt::RoundCap);
    scene_->addLine(stationPositions_.front(), kTrackIY, stationPositions_.back(), kTrackIY,
                    trackPen);
    scene_->addLine(stationPositions_.front(), kTrackIIY, stationPositions_.back(), kTrackIIY,
                    trackPen);

    auto* pathILabel = scene_->addSimpleText(tr("Путь I  A → B"));
    pathILabel->setPos(kLineLeft, kTrackIY - 42.0);
    auto* pathIILabel = scene_->addSimpleText(tr("Путь II  B → A"));
    pathIILabel->setPos(kLineLeft, kTrackIIY + 18.0);

    const QPen stationPen(QColor(35, 35, 35), 2.0);
    const QBrush stationBrush(Qt::white);
    for (std::size_t station = 0; station < stationPositions_.size(); ++station) {
        const double x = stationPositions_[station];
        scene_->addLine(x, kTrackIY, x, kTrackIIY, QPen(QColor(185, 185, 185), 1.0));
        scene_->addEllipse(x - 7.0, kTrackIY - 7.0, 14.0, 14.0, stationPen, stationBrush);
        scene_->addEllipse(x - 7.0, kTrackIIY - 7.0, 14.0, 14.0, stationPen, stationBrush);

        auto* name = scene_->addSimpleText(QString::fromStdString(line_.stationNames[station]));
        name->setTransformOriginPoint(name->boundingRect().bottomLeft());
        name->setRotation(-35.0);
        name->setPos(x - 8.0, kTrackIY - 55.0);
    }

    drawTerminal(0, true);
    drawTerminal(static_cast<int>(stationPositions_.size()) - 1, false);
}

void LineView::drawTerminal(int station, bool leftSide) {
    const double x = stationX(station);
    const QPointF deadEnd = deadEndPoint(station);
    const QPointF depot = depotPoint(station);
    const QPen servicePen(QColor(95, 95, 95), 3.0, Qt::DashLine, Qt::RoundCap);

    scene_->addLine(QLineF(QPointF(x, kTrackIY), deadEnd), servicePen);
    scene_->addLine(QLineF(deadEnd, QPointF(x, kTrackIIY)), servicePen);
    scene_->addLine(QLineF(QPointF(x, kTrackIY), depot), servicePen);
    scene_->addLine(QLineF(QPointF(x, kTrackIIY), depot), servicePen);
    scene_->addEllipse(deadEnd.x() - 8.0, deadEnd.y() - 8.0, 16.0, 16.0, QPen(Qt::darkGray));
    scene_->addRect(depot.x() - 22.0, depot.y() - 15.0, 44.0, 30.0, QPen(Qt::darkGray),
                    QBrush(QColor(225, 225, 225)));

    const QString terminalName = leftSide ? tr("A") : tr("B");
    auto* deadEndLabel = scene_->addSimpleText(tr("Тупик %1").arg(terminalName));
    deadEndLabel->setPos(deadEnd.x() - deadEndLabel->boundingRect().width() / 2.0,
                         deadEnd.y() - 36.0);
    auto* depotLabel = scene_->addSimpleText(tr("Депо %1").arg(terminalName));
    depotLabel->setPos(depot.x() - depotLabel->boundingRect().width() / 2.0, depot.y() + 20.0);
}

void LineView::updateTrain(const api::TrainUpdate& train, api::SimTime updateTime,
                           double modelTime) {
    auto item = trainItems_.find(train.id);
    if (item == trainItems_.end()) {
        TrainGraphics graphics;
        graphics.body =
            scene_->addRect(-kTrainWidth / 2.0, -kTrainHeight / 2.0, kTrainWidth, kTrainHeight,
                            QPen(QColor(35, 35, 35), 1.5), QBrush(modeColor(train.mode)));
        graphics.number = scene_->addSimpleText(QString{});
        graphics.speed = scene_->addSimpleText(QString{});
        for (QGraphicsItem* graphicsItem : {static_cast<QGraphicsItem*>(graphics.body),
                                            static_cast<QGraphicsItem*>(graphics.number),
                                            static_cast<QGraphicsItem*>(graphics.speed)}) {
            graphicsItem->setData(0, train.id);
            graphicsItem->setData(1, true);
        }
        item = trainItems_.emplace(train.id, graphics).first;
    }

    TrainGraphics& graphics = item->second;
    const QPointF position = positionFor(train, updateTime, modelTime);
    graphics.body->setBrush(modeColor(train.mode));
    graphics.body->setPos(position);

    QString number = QString::number(train.id + 1);
    if (train.flags.headingToDepot) {
        number += tr(" →Д");
    }
    graphics.number->setText(number);
    graphics.number->setPos(position.x() - graphics.number->boundingRect().width() / 2.0,
                            position.y() - graphics.number->boundingRect().height() / 2.0);
    graphics.speed->setText(tr("%1 км/ч").arg(train.speedKmh, 0, 'f', 0));
    graphics.speed->setPos(position.x() + kTrainWidth / 2.0 + 5.0,
                           position.y() - graphics.speed->boundingRect().height() / 2.0);

    const QString description = trainDescription(train);
    graphics.body->setToolTip(description);
    graphics.number->setToolTip(description);
    graphics.speed->setToolTip(description);
}

QPointF LineView::positionFor(const api::TrainUpdate& train, api::SimTime updateTime,
                              double modelTime) const {
    const double elapsed = std::max(0.0, modelTime - static_cast<double>(updateTime));
    const double along = std::clamp(train.along + train.v * elapsed, 0.0, 1.0);
    const double station = stationX(train.station);

    QPointF start;
    QPointF end;
    switch (train.edge) {
    case api::TrackEdge::Span: {
        const int nextStation = train.dir == api::Dir::I ? train.station + 1 : train.station - 1;
        start = {station, trackY(train.dir)};
        end = {stationX(nextStation), trackY(train.dir)};
        break;
    }
    case api::TrackEdge::Platform:
        return {station, trackY(train.dir)};
    case api::TrackEdge::ToDeadEnd:
        start = {station, trackY(train.dir)};
        end = deadEndPoint(train.station);
        break;
    case api::TrackEdge::FromDeadEnd:
        start = deadEndPoint(train.station);
        end = {station, trackY(train.dir)};
        break;
    case api::TrackEdge::ToDepot:
        start = {station, trackY(train.dir)};
        end = depotPoint(train.station);
        break;
    case api::TrackEdge::FromDepot:
        start = depotPoint(train.station);
        end = {station, trackY(train.dir)};
        break;
    case api::TrackEdge::Depot:
        return depotPoint(train.station);
    }
    return start + (end - start) * along;
}

QPointF LineView::deadEndPoint(int station) const {
    const bool leftSide = station == 0;
    return {stationX(station) + (leftSide ? -75.0 : 75.0), kTerminalY};
}

QPointF LineView::depotPoint(int station) const {
    const bool leftSide = station == 0;
    return {stationX(station) + (leftSide ? -155.0 : 155.0), kTerminalY};
}

double LineView::trackY(api::Dir dir) const {
    return dir == api::Dir::I ? kTrackIY : kTrackIIY;
}

QString LineView::trainDescription(const api::TrainUpdate& train) const {
    const QString direction = train.dir == api::Dir::I ? tr("I") : tr("II");
    return tr("Поезд %1\nРежим: %2\nНаправление: %3\nПоложение: %4\nОпоздание: %5 с")
        .arg(train.id + 1)
        .arg(modeName(train.mode))
        .arg(direction)
        .arg(edgeName(train.edge))
        .arg(train.lateness);
}

} // namespace ubahn::app
