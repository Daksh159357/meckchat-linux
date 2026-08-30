#include <QApplication>
#include "ui/mainwindow.h"
#include "meckchat/core/logger.h"
#include "meckchat/core/config.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("MeckChat");
    app.setApplicationDisplayName("MeckChat Linux Desktop");
    app.setOrganizationName("MeckChat");
    app.setApplicationVersion("0.1.0");

    MeckChat::Core::Logger::init();
    MeckChat::Core::AppConfig::instance().load();

    MeckChat::UI::MainWindow window;
    window.show();

    return app.exec();
}
