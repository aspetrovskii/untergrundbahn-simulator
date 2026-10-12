#pragma once

#include "api/Types.h"

#include <QMainWindow>

namespace ubahn::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const api::SimParams& params, QWidget* parent = nullptr);
};

} // namespace ubahn::app
