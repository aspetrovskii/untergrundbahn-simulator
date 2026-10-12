#include "MainWindow.h"
#include "SetupDialog.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    ubahn::app::SetupDialog setupDialog;
    if (setupDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    ubahn::app::MainWindow mainWindow(setupDialog.selectedParams());
    mainWindow.show();
    return application.exec();
}
