// KDock entry point
// We use QGuiApplication rather than QApplication because we only need
// QtQuick rendering — the full widget stack would pull in X11 dependencies
// we explicitly want to avoid.
//
// Context properties injected into QML:
//   "dockModel"   → DockModel*       list of launchers
//   "taskTracker" → TaskTracker*     open window state
//   "config"      → ConfigWatcher*   live settings

#include "ConfigWatcher.h"
#include "DockModel.h"
#include "IconProvider.h"
#include "LayerShellWindow.h"
#include "TaskTracker.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock"));
    app.setOrganizationName(QStringLiteral("kdock"));

    ConfigWatcher config;
    TaskTracker taskTracker;
    DockModel dockModel(&config);
    dockModel.setTaskTracker(&taskTracker);

    QQmlApplicationEngine engine;

    // Register custom image provider so QML can use "image://kdock/<appId>"
    engine.addImageProvider(QStringLiteral("kdock"), new IconProvider());

    // Expose C++ objects to QML
    engine.rootContext()->setContextProperty(QStringLiteral("dockModel"), &dockModel);
    engine.rootContext()->setContextProperty(QStringLiteral("taskTracker"), &taskTracker);
    engine.rootContext()->setContextProperty(QStringLiteral("config"), &config);

    // Resolve QML directory — installed location first, then build tree
    const QString qmlInstallDir = QStringLiteral(QML_INSTALL_DIR);
    const QString qmlMain = qmlInstallDir + QStringLiteral("/main.qml");
    const QString qmlFallback = QStringLiteral("qml/main.qml");

    const QString qmlPath = QFile::exists(qmlMain) ? qmlMain : qmlFallback;
    engine.load(QUrl::fromLocalFile(qmlPath));

    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
