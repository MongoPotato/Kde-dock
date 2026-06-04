// KDock entry point.
// Uses LayerShellWindow (QQuickView subclass) directly so QML is loaded
// into our own window — not a window created internally by QQmlApplicationEngine.
// Qt::BypassWindowManagerHint in LayerShellWindow prevents the Wayland QPA
// from assigning xdg-shell, leaving the surface free for wlr-layer-shell.
//
// Context properties exposed to QML:
//   dockModel           → DockModel*           list of launcher entries
//   taskTracker         → TaskTracker*          open window state
//   config              → ConfigWatcher*        live JSON settings
//   settings            → SettingsController*   validated settings mutations
//   iconThemeDetector   → IconThemeDetector*    KDE theme auto-detection

#include "AppLibrary.h"
#include "ConfigWatcher.h"
#include "DockModel.h"
#include "IconProvider.h"
#include "IconThemeDetector.h"
#include "LayerShellWindow.h"
#include "SettingsController.h"
#include "TaskTracker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock"));
    app.setOrganizationName(QStringLiteral("kdock"));

    // Detect KDE icon theme first so all subsequent QIcon::fromTheme() calls
    // use the correct theme.
    IconThemeDetector iconThemeDetector;

    ConfigWatcher config;
    TaskTracker taskTracker;
    DockModel dockModel(&config);
    dockModel.setTaskTracker(&taskTracker);
    dockModel.setIconThemeDetector(&iconThemeDetector);

    AppLibrary appLibrary;

    SettingsController settingsController(&config, &iconThemeDetector);

    LayerShellWindow window;
    window.setAnchor(config.position());

    const int thickness = config.iconSize() + config.padding() * 2;
    window.setThickness(thickness);

    // Set an initial window size so the QML root item has geometry before
    // the layer-shell configure callback fires.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect geom = screen->geometry();
        const bool horizontal = (config.position() == QStringLiteral("bottom")
                              || config.position() == QStringLiteral("top"));
        window.resize(horizontal ? geom.width() : thickness,
                      horizontal ? thickness : geom.height());
    }

    // Enable blur behind if configured
    window.setBlurEnabled(config.blurEnabled());
    QObject::connect(&config, &ConfigWatcher::configChanged, &window,
                     [&]{ window.setBlurEnabled(config.blurEnabled()); });

    // Register custom image provider so QML can use "image://kdock/<appId>"
    window.engine()->addImageProvider(QStringLiteral("kdock"), new IconProvider());

    // Expose C++ objects to QML
    QQmlContext *ctx = window.rootContext();
    ctx->setContextProperty(QStringLiteral("dockModel"),         &dockModel);
    ctx->setContextProperty(QStringLiteral("taskTracker"),       &taskTracker);
    ctx->setContextProperty(QStringLiteral("config"),            &config);
    ctx->setContextProperty(QStringLiteral("settings"),          &settingsController);
    ctx->setContextProperty(QStringLiteral("iconThemeDetector"), &iconThemeDetector);
    ctx->setContextProperty(QStringLiteral("appLibrary"),        &appLibrary);

    // Resolve QML — search in order: installed path, next to exe, CWD
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList qmlCandidates = {
        QStringLiteral(QML_INSTALL_DIR) + QStringLiteral("/main.qml"),
        exeDir + QStringLiteral("/../qml/main.qml"),
        exeDir + QStringLiteral("/qml/main.qml"),
        QDir::currentPath() + QStringLiteral("/qml/main.qml"),
    };

    QString qmlPath;
    for (const QString &c : qmlCandidates) {
        if (QFile::exists(QDir::cleanPath(c))) {
            qmlPath = QDir::cleanPath(c);
            break;
        }
    }

    if (qmlPath.isEmpty()) {
        qWarning("kdock: cannot find main.qml — searched:\n  %s",
                 qPrintable(qmlCandidates.join(QStringLiteral("\n  "))));
        return 1;
    }

    window.setSource(QUrl::fromLocalFile(qmlPath));
    if (window.status() == QQuickView::Error) {
        for (const auto &err : window.errors())
            qWarning("kdock: QML error: %s", qPrintable(err.toString()));
        return 1;
    }

    window.show();
    return app.exec();
}
