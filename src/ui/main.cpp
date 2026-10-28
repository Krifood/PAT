#include <QApplication>

#include "ui/MainWindow.h"

#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PAT"));
    QApplication::setApplicationVersion(QStringLiteral(PAT_APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/pat/app-icon.png")));
    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
