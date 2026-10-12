#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>

namespace ubahn::app {

MainWindow::MainWindow(const api::SimParams& params, QWidget* parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle(tr("Untergrundbahn Simulator"));
    resize(1000, 650);

    auto* placeholder = new QLabel(tr("Главное окно симуляции"), this);
    placeholder->setObjectName("mainWindowPlaceholder");
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    const auto stationCount = params.line.stationNames.size();
    statusBar()->showMessage(tr("Линия настроена: станций — %1").arg(stationCount));
}

} // namespace ubahn::app
