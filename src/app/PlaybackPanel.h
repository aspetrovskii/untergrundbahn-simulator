#pragma once

#include "api/Types.h"

#include <QFrame>
#include <optional>

class QComboBox;
class QLabel;
class QPushButton;

namespace ubahn::app {

class PlaybackPanel : public QFrame {
    Q_OBJECT

public:
    explicit PlaybackPanel(QWidget* parent = nullptr);

    void setInitialValues(double rate, int stepSec);
    void setModelState(api::SimTime modelTime, const std::optional<api::ClockUpdate>& clock);

signals:
    void rateChanged(double rate);
    void stepChanged(int stepSec);
    void seekForwardRequested(ubahn::api::SimTime target);
    void stopRequested();

private:
    void togglePause();
    void selectRate();
    void requestSeek();
    void requestStop();
    void updateWindowText(const std::optional<api::ClockUpdate>& clock);

    QLabel* clockLabel_ = nullptr;
    QLabel* windowLabel_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QComboBox* rateCombo_ = nullptr;
    QComboBox* stepCombo_ = nullptr;
    QComboBox* seekCombo_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    api::SimTime modelTime_ = 0;
    double rate_ = 1.0;
    double resumeRate_ = 1.0;
};

} // namespace ubahn::app
