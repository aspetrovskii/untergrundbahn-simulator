#pragma once

#include "api/Types.h"

#include <QFrame>
#include <array>
#include <string>

class QLabel;
class QString;

namespace ubahn::app {

class BoardWidget : public QFrame {
    Q_OBJECT

public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void setStationName(const std::string& name);
    void setBoards(const api::BoardUpdate* directionI, const api::BoardUpdate* directionII,
                   api::SimTime modelTime);

private:
    struct BoardLabels {
        QLabel* arrival = nullptr;
        QLabel* dwell = nullptr;
        QLabel* shift = nullptr;
    };

    void updateRow(BoardLabels& labels, const api::BoardUpdate* board, api::SimTime modelTime);
    static QString secondsText(api::SimTime seconds);
    static QString shiftText(int32_t seconds);

    QLabel* stationName_ = nullptr;
    std::array<BoardLabels, 2> rows_;
};

} // namespace ubahn::app
