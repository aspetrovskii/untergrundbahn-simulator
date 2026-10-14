#pragma once

#include "api/Types.h"

#include <QWidget>
#include <string>
#include <vector>

class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;

namespace ubahn::app {

class LineEditor : public QWidget {
    Q_OBJECT

public:
    explicit LineEditor(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    api::LineConfig line() const;
    std::vector<std::string> inputErrors() const;

private:
    void resizeStationTable(int stationCount);
    void setStationCell(int row, int column, const QString& text, bool editable = true);

    QSpinBox* stationCount_ = nullptr;
    QTableWidget* stations_ = nullptr;
    QSpinBox* fleetSize_ = nullptr;
    QSpinBox* dwellMin_ = nullptr;
    QSpinBox* headwayMin_ = nullptr;
    QSpinBox* clearTime_ = nullptr;
    QDoubleSpinBox* slowFactor_ = nullptr;
    QSpinBox* toDeadEnd_ = nullptr;
    QSpinBox* cabChange_ = nullptr;
    QSpinBox* fromDeadEnd_ = nullptr;
    QSpinBox* depotOut_ = nullptr;
    QSpinBox* depotIn_ = nullptr;
    QDoubleSpinBox* signalOffset_ = nullptr;
    QSpinBox* signalApproach_ = nullptr;
};

} // namespace ubahn::app
