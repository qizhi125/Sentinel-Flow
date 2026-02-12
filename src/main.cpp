#include <QApplication>
#include <QFont>
#include <QStringList>
#include "presentation/views/MainWindow.h"
#include "presentation/views/styles/ThemeManager.h"
#include "common/types/NetworkTypes.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    qRegisterMetaType<ParsedPacket>("ParsedPacket");
    qRegisterMetaType<QVector<ParsedPacket>>("QVector<ParsedPacket>");
    qRegisterMetaType<Alert>("Alert");

    app.setApplicationName("Sentinel-Flow");
    app.setApplicationVersion("6.0.0");

    ThemeManager::applyTheme(app, true);

    MainWindow w;
    w.show();

    return app.exec();
}