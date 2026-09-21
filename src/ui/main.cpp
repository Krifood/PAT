#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PAT"));
    QApplication::setApplicationVersion(QStringLiteral(PAT_APP_VERSION));
    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
