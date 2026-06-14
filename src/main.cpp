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

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QScreen>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock"));
    app.setOrganizationDomain(QStringLiteral("kdock"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KDE Plasma 6 floating dock"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(
        QStringLiteral("debug"),
        QStringLiteral("Enable verbose QML and Qt logging; print startup diagnostics.")));
    parser.process(app);
    const bool debugMode = parser.isSet(QStringLiteral("debug"));

    if (debugMode) {
        QLoggingCategory::setFilterRules(
            QStringLiteral("qml=true\n"
                           "qt.qml=true\n"
                           "qt.qml.binding=true\n"
                           "qt.quick=true\n"
                           "qt.quick.loader=true\n"
                           "qt.wayland=true"));
        qDebug("kdock [debug]: Qt %s | QML debug enabled", qVersion());
    }

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

    // baseThickness = visual strip height (reserved screen space / exclusive zone).
    // winThickness  = full window height: strip + hoverLiftPx so lifted icons
    //                 don't clip at the window edge.
    auto computeThicknesses = [&](int &base, int &win) {
        base = config.iconSize() + config.padding() * 2;
        // Extra space for hover lift (8px default) + click bounce peak (-22px) + margin
        win  = base + config.hoverLiftPx() + 32;
    };

    {
        int base, win;
        computeThicknesses(base, win);
        window.setExclusiveZone(base);
        window.setThickness(win);
    }

    // Set an initial window size so the QML root item has geometry before
    // the layer-shell configure callback fires.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect geom = screen->geometry();
        const bool horizontal = (config.position() == QStringLiteral("bottom")
                              || config.position() == QStringLiteral("top"));
        window.resize(horizontal ? geom.width() : window.thickness(),
                      horizontal ? window.thickness() : geom.height());
    }

    // Enable blur behind if configured
    window.setBlurEnabled(config.blurEnabled());
    QObject::connect(&config, &ConfigWatcher::configChanged, &window,
                     [&]() {
                         window.setBlurEnabled(config.blurEnabled());
                         int base, win;
                         computeThicknesses(base, win);
                         window.setExclusiveZone(base);
                         window.setThickness(win);
                         window.applyGeometryUpdate();
                     });

    // Register custom image provider so QML can use "image://kdock/<appId>"
    window.engine()->addImageProvider(QStringLiteral("kdock"), new IconProvider());

    // Always forward QML warnings to stderr.
    QObject::connect(window.engine(), &QQmlEngine::warnings,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &w : warnings)
            qWarning("kdock: QML: %s", qPrintable(w.toString()));
    });

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

    if (debugMode) {
        qDebug("kdock [debug]: pinned apps : [%s]",
               qPrintable(config.pinnedApps().join(QStringLiteral(", "))));
        qDebug("kdock [debug]: position    : %s", qPrintable(config.position()));
        qDebug("kdock [debug]: icon size   : %d px", config.iconSize());
        qDebug("kdock [debug]: model rows  : %d", dockModel.rowCount());
        qDebug("kdock [debug]: QML path    : %s", qPrintable(qmlPath));
    }

    window.setSource(QUrl::fromLocalFile(qmlPath));
    if (window.status() == QQuickView::Error) {
        for (const auto &err : window.errors())
            qWarning("kdock: QML error: %s", qPrintable(err.toString()));
        return 1;
    }

    if (debugMode)
        qDebug("kdock [debug]: QML loaded  : status=%d", static_cast<int>(window.status()));

    window.show();
    return app.exec();
}
