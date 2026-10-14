#include "BoardWidget.h"

#include <QGridLayout>
#include <QLabel>
#include <QString>
#include <algorithm>

namespace ubahn::app {
namespace {

QLabel* makeValueLabel(const char* objectName, QWidget* parent) {
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumWidth(90);
    return label;
}

} // namespace

BoardWidget::BoardWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("boardWidget");
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(4);

    stationName_ = new QLabel(this);
    stationName_->setObjectName("boardStationName");
    QFont titleFont = stationName_->font();
    titleFont.setBold(true);
    stationName_->setFont(titleFont);
    layout->addWidget(stationName_, 0, 0, 1, 4);

    layout->addWidget(new QLabel(tr("Направление"), this), 1, 0);
    layout->addWidget(new QLabel(tr("С прибытия"), this), 1, 1, Qt::AlignCenter);
    layout->addWidget(new QLabel(tr("Стоянка"), this), 1, 2, Qt::AlignCenter);
    layout->addWidget(new QLabel(tr("Сдвиг"), this), 1, 3, Qt::AlignCenter);

    const std::array<QString, 2> directions = {tr("I · A → B"), tr("II · B → A")};
    const std::array<const char*, 2> arrivalNames = {"boardArrivalI", "boardArrivalII"};
    const std::array<const char*, 2> dwellNames = {"boardDwellI", "boardDwellII"};
    const std::array<const char*, 2> shiftNames = {"boardShiftI", "boardShiftII"};
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const int row = static_cast<int>(index) + 2;
        layout->addWidget(new QLabel(directions[index], this), row, 0);
        rows_[index].arrival = makeValueLabel(arrivalNames[index], this);
        rows_[index].dwell = makeValueLabel(dwellNames[index], this);
        rows_[index].shift = makeValueLabel(shiftNames[index], this);
        layout->addWidget(rows_[index].arrival, row, 1);
        layout->addWidget(rows_[index].dwell, row, 2);
        layout->addWidget(rows_[index].shift, row, 3);
    }
}

void BoardWidget::setStationName(const std::string& name) {
    stationName_->setText(tr("Табло станции «%1»").arg(QString::fromStdString(name)));
}

void BoardWidget::setBoards(const api::BoardUpdate* directionI, const api::BoardUpdate* directionII,
                            api::SimTime modelTime) {
    updateRow(rows_[0], directionI, modelTime);
    updateRow(rows_[1], directionII, modelTime);
}

void BoardWidget::updateRow(BoardLabels& labels, const api::BoardUpdate* board,
                            api::SimTime modelTime) {
    if (board == nullptr) {
        labels.arrival->setText(tr("—"));
        labels.dwell->setText(tr("—"));
        labels.shift->setText(tr("—"));
        return;
    }

    if (board->lastArrival == api::kNoTime) {
        labels.arrival->setText(tr("—"));
    } else {
        labels.arrival->setText(secondsText(modelTime - board->lastArrival));
    }

    if (board->occupied && board->dwellSince != api::kNoTime) {
        labels.dwell->setText(secondsText(modelTime - board->dwellSince));
    } else {
        labels.dwell->setText(secondsText(board->dwellShown));
    }
    labels.shift->setText(shiftText(board->shift));
}

QString BoardWidget::secondsText(api::SimTime seconds) {
    return tr("%1 с").arg(std::max<api::SimTime>(0, seconds));
}

QString BoardWidget::shiftText(int32_t seconds) {
    if (seconds > 0) {
        return tr("+%1 с").arg(seconds);
    }
    return tr("%1 с").arg(seconds);
}

} // namespace ubahn::app
