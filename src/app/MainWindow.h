#pragma once

#include "api/Types.h"

#include <QMainWindow>

class QLabel;

namespace ubahn::app {

class AppSession;
class PlaybackController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const api::SimParams& params, QWidget* parent = nullptr);

private:
    QLabel* placeholder_ = nullptr;
    AppSession* session_ = nullptr;
    PlaybackController* playback_ = nullptr;
};

} // namespace ubahn::app
