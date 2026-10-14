#pragma once

#include "api/Types.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace ubahn::app {

class SummaryWidget : public QWidget {
    Q_OBJECT

public:
    explicit SummaryWidget(QWidget* parent = nullptr);

    void setLine(const api::LineConfig& line);
    void setSummary(const api::SimSummary& summary);

private:
    void addMetric(const QString& name, const QString& value);
    void updateStationHeadways(const api::SimSummary& summary);
    void updateStretchedWindows(const api::SimSummary& summary);
    QString stationName(int station) const;
    static QString seconds(double value);

    api::LineConfig line_;
    QLabel* status_ = nullptr;
    QTableWidget* metrics_ = nullptr;
    QTableWidget* stations_ = nullptr;
    QTableWidget* stretches_ = nullptr;
};

} // namespace ubahn::app
