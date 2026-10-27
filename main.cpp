
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    // Create the Windows application.
    QApplication app(argc, argv);

    // Create KROBUS.
    MainWindow w;

    // Always show KROBUS when launched.
    w.show();

    // Keep KROBUS running until Quit KROBUS
    // is selected from the right-click menu.
    return app.exec();
}
