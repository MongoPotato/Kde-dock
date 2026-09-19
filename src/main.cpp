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
#include "LayerShellGlobal.h"
#include "LayerShellPopup.h"
#include "LayerShellWindow.h"
#include "SettingsController.h"
#include "SingleInstance.h"
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
    // Pick the Wayland QPA plugin when we're plainly in a Wayland session.
    // kdock needs wlr-layer-shell and KWin's scripting service; under XWayland
    // it gets neither and degrades into a centred, alt-tabbable window. Qt's
    // own -platform argument still overrides this, and an explicit
    // QT_QPA_PLATFORM in the environment is left alone.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }

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
        QStringLiteral("Enable verbose QML and Qt logging; print startup diagnostics. "
                       "Implies --replace, so it takes over from the installed dock "
                       "instead of running alongside it.")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("replace"),
        QStringLiteral("Stop any running kdock (including the systemd service) and "
                       "take its place.")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("qml-path"),
        QStringLiteral("Load QML from this directory instead of the usual search "
                       "order. Use it to run a development build against the "
                       "installed QML, or the reverse."),
        QStringLiteral("dir")));
    parser.process(app);
    const bool debugMode = parser.isSet(QStringLiteral("debug"));
    // Debugging a dock while the installed one is still on screen is useless,
    // so --debug always replaces.
    const bool replaceMode = debugMode || parser.isSet(QStringLiteral("replace"));

    // Must happen before anything touches KWin or the config: two instances
    // would otherwise fight over the same KWin script name and DBus objects.
    QString instanceError;
    SingleInstance *instance = SingleInstance::acquire(replaceMode, &instanceError);
    if (!instance) {
        qWarning("kdock: %s", qPrintable(instanceError));
        return 1;
    }
    if (instance->stoppedSystemdUnit()) {
        qInfo("kdock: stopped the installed kdock.service to take over. "
              "Restore it later with:  systemctl --user start kdock");
    }

    if (debugMode) {
        QLoggingCategory::setFilterRules(
            QStringLiteral("qml=true\n"
                           "qt.qml=true\n"
                           "qt.qml.binding=true\n"
                           "qt.quick=true\n"
                           "qt.quick.loader=true\n"
                           "qt.wayland=true"));
        qDebug("kdock [debug]: Qt %s | QML debug enabled", qVersion());
        qDebug("kdock [debug]: platform    : %s", qPrintable(app.platformName()));
        qDebug("kdock [debug]: env         : WAYLAND_DISPLAY=%s DISPLAY=%s QT_QPA_PLATFORM=%s",
               qgetenv("WAYLAND_DISPLAY").constData(),
               qgetenv("DISPLAY").constData(),
               qgetenv("QT_QPA_PLATFORM").constData());
        qDebug("kdock [debug]: layer shell : %s",
               qPrintable(LayerShellGlobal::diagnostics()));
    }

    // Detect KDE icon theme first so all subsequent QIcon::fromTheme() calls
    // use the correct theme.
    IconThemeDetector iconThemeDetector;

    ConfigWatcher config;
    TaskTracker taskTracker;
    taskTracker.setVerbose(debugMode);
    DockModel dockModel(&config);
    dockModel.setTaskTracker(&taskTracker);
    dockModel.setIconThemeDetector(&iconThemeDetector);

    AppLibrary appLibrary;

    SettingsController settingsController(&config, &iconThemeDetector);

    // ContextMenu.qml instantiates this as `LayerPopup`. It has to be
    // registered before any QML is loaded.
    qmlRegisterType<LayerShellPopup>("KDock", 1, 0, "LayerPopup");

    LayerShellWindow window;
    window.setAnchor(config.position());
    window.setLayer(config.layer());

    // Resolve which physical screen the dock belongs on: a non-negative
    // screenIndex pins it to that screen (falling back to the primary screen
    // if it's not currently connected), otherwise it always follows KDE's
    // configured primary screen — including across hotplug.
    auto resolvePreferredScreen = [&]() -> QScreen * {
        const int idx = config.screenIndex();
        const QList<QScreen *> screens = QGuiApplication::screens();
        if (idx >= 0 && idx < screens.size())
            return screens.at(idx);
        return QGuiApplication::primaryScreen();
    };

    if (QScreen *initialScreen = resolvePreferredScreen())
        window.setScreen(initialScreen);

    // All three numbers come from ConfigWatcher so nothing here can drift out
    // of step with what QML actually paints:
    //   dockReservedThickness — screen space the compositor keeps clear, and
    //                           the part of the window that takes input
    //   dockWindowThickness   — full window height, click-bounce headroom
    //                           included; that headroom paints nothing and is
    //                           neither reserved nor interactive
    // Auto-hide reserves nothing: a dock that gets out of the way by itself
    // has no business permanently carving out the screen edge.
    auto applyDockGeometry = [&]() {
        // Only "never" reserves space. Both auto-hide and dodge need windows
        // to be allowed into the strip — reserving it would mean nothing ever
        // overlaps the dock and dodge could never trigger.
        const bool reserving = config.reserveSpace()
                            && config.autohideMode() == QStringLiteral("never");
        window.setExclusiveZone(reserving ? config.dockReservedThickness() : 0);
        window.setInteractiveThickness(config.dockReservedThickness());
        window.setThickness(config.dockWindowThickness());

        // So a maximised window on another monitor doesn't hide this dock.
        taskTracker.setDockScreenName(window.screen() ? window.screen()->name()
                                                      : QString());
    };
    applyDockGeometry();

    // Set an initial window size so the QML root item has geometry before
    // the layer-shell configure callback fires.
    if (QScreen *screen = window.screen()) {
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
                         applyDockGeometry();
                         window.applyGeometryUpdate();
                     });

    // Re-anchor to the right screen whenever KDE's primary screen changes,
    // or a screen is plugged/unplugged — covers both "moved the primary
    // screen in Display settings" and "unplugged the screen the dock was on".
    auto reanchorScreen = [&]() {
        if (QScreen *target = resolvePreferredScreen()) {
            window.reanchorToScreen(target);
            // The dodge rectangle is screen-relative, so it moves with the dock.
            applyDockGeometry();
        }
    };
    QObject::connect(qApp, &QGuiApplication::primaryScreenChanged, &window, reanchorScreen);
    QObject::connect(qApp, &QGuiApplication::screenAdded,   &window, reanchorScreen);
    QObject::connect(qApp, &QGuiApplication::screenRemoved, &window, reanchorScreen);

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
    ctx->setContextProperty(QStringLiteral("dockWindow"),        &window);

    // Resolve QML — search in order: installed path, next to exe, CWD
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString qmlOverride = parser.value(QStringLiteral("qml-path"));
    const QStringList qmlCandidates = qmlOverride.isEmpty()
      ? QStringList{
        QStringLiteral(QML_INSTALL_DIR) + QStringLiteral("/main.qml"),
        exeDir + QStringLiteral("/../qml/main.qml"),
        exeDir + QStringLiteral("/qml/main.qml"),
        QDir::currentPath() + QStringLiteral("/qml/main.qml"),
      }
      : QStringList{ qmlOverride + QStringLiteral("/main.qml") };

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
        qDebug("kdock [debug]: layer       : %s", qPrintable(config.layer()));
        qDebug("kdock [debug]: autohide    : %s", qPrintable(config.autohideMode()));
        qDebug("kdock [debug]: icon size   : %d px", config.iconSize());
        qDebug("kdock [debug]: dock painted: %d px", config.dockVisualThickness());
        qDebug("kdock [debug]: reserved    : %d px%s",
               window.exclusiveZone(),
               window.exclusiveZone() != 0 ? ""
                 : config.autohideMode() != QStringLiteral("never")
                     ? "  (nothing reserved: the dock hides, so windows may use the strip)"
                     : "  (reserveSpace off)");
        qDebug("kdock [debug]: interactive : %d px of a %d px window",
               window.interactiveThickness(), window.thickness());
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

    if (debugMode) {
        // After show(), so this reports what actually happened rather than
        // what was intended.
        qDebug("kdock [debug]: surface     : %s",
               window.usingLayerSurface()
                   ? "layer-shell (anchored, out of alt-tab, reserves space)"
                   : "ORDINARY WINDOW — not anchored, appears in alt-tab");
    }

    return app.exec();
}
