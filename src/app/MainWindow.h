#pragma once

#include "api/Types.h"

#include <QMainWindow>

namespace ubahn::app {

class AppSession;
class LineView;
class PlaybackController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const api::SimParams& params, QWidget* parent = nullptr);

private:
    LineView* lineView_ = nullptr;
    AppSession* session_ = nullptr;
    PlaybackController* playback_ = nullptr;
};

} // namespace ubahn::app
