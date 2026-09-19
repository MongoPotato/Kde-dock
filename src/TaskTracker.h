#pragma once

// TaskTracker: KDE Plasma window tracking and control via KWin's own
// JavaScript scripting engine.
//
// Two protocol-level approaches were tried and ruled out before this one:
//  - org.kde.KWin's DBus interface (/KWin) has no per-window activate/close
//    mechanism at all (confirmed via a full Introspect dump).
//  - The plasma-window-management Wayland protocol
//    (org_kde_plasma_window_management) is not even advertised to ordinary
//    Wayland clients on KWin 6 — it's restricted to the shell — confirmed by
//    dumping every registry global the compositor offers.
//
// KWin scripts run *inside* KWin itself (loaded via org.kde.kwin.Scripting
// over DBus), so they aren't subject to either restriction: they have full
// access to workspace.windowList()/windowAdded/windowRemoved/windowActivated
// for tracking, and to workspace.activeWindow = window / window.closeWindow()
// for control. We load one persistent script for live tracking, and execute
// small one-shot scripts (load → start → unload) to perform activate/close,
// referencing windows by the same internalId already used for tracking.

#include <QMap>
#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

class QDBusInterface;

// Minimal QObject registered on the session bus as org.kde.kdock /WindowTracker.
// The persistent KWin tracking script calls its Q_SCRIPTABLE slots to report
// window events.
class KWinBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdock.WindowTracker")
public:
    explicit KWinBridge(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    Q_SCRIPTABLE void reportWindowAdded(const QString &uuid, const QString &desktopFile)
    { emit windowAdded(uuid, desktopFile); }

    Q_SCRIPTABLE void reportWindowRemoved(const QString &uuid)
    { emit windowRemoved(uuid); }

    Q_SCRIPTABLE void reportWindowActivated(const QString &uuid)
    { emit windowActivated(uuid); }

    Q_SCRIPTABLE void reportWindowUrgent(const QString &uuid, bool urgent)
    { emit windowUrgentChanged(uuid, urgent); }

    // Full-window state, reported per window on every change — the same
    // shape as the add/remove/urgency reports above, deliberately.
    Q_SCRIPTABLE void reportWindowFullSize(const QString &uuid, bool fullSize,
                                           const QString &outputName)
    { emit windowFullSizeChanged(uuid, fullSize, outputName); }

    // "Show desktop" hides windows without minimising them, so it can't be
    // inferred from the per-window state and has to be reported separately.
    Q_SCRIPTABLE void reportShowingDesktop(bool showing)
    { emit showingDesktopChanged(showing); }

    // Proof of life from inside KWin. Silence from the reports above is
    // ambiguous without it: no full-size window looks exactly like no script.
    Q_SCRIPTABLE void reportScriptAlive(int windowCount)
    { emit scriptAlive(windowCount); }

signals:
    void windowAdded(const QString &uuid, const QString &desktopFile);
    void windowRemoved(const QString &uuid);
    void windowActivated(const QString &uuid);
    void windowUrgentChanged(const QString &uuid, bool urgent);
    void windowFullSizeChanged(const QString &uuid, bool fullSize, const QString &outputName);
    void showingDesktopChanged(bool showing);
    void scriptAlive(int windowCount);
};

class TaskTracker : public QObject {
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);
    ~TaskTracker() override;

    QStringList runningApps() const;
    QString     activeAppId() const;

    Q_INVOKABLE void closeWindows(const QString &appId);
    Q_INVOKABLE void activateWindow(const QString &appId);
    Q_INVOKABLE void minimizeWindow(const QString &appId);

    bool hasWindowForApp(const QString &appId) const;

    Q_PROPERTY(QString activeAppId READ activeAppId NOTIFY activeAppChanged)

    // True while a full-size (maximised or fullscreen) window is on screen on
    // the dock's own output, and the desktop isn't being shown. Drives dodge
    // mode: the dock only gets out of the way when something really is
    // covering it.
    Q_PROPERTY(bool dockObstructed READ dockObstructed NOTIFY dockObstructionChanged)

    bool dockObstructed() const { return m_dockObstructed; }

    // Name of the output the dock is on, so a maximised window on a second
    // monitor doesn't hide it. Empty means "don't filter by screen".
    void setDockScreenName(const QString &name);

    // Turns on the periodic dodge-state dump used by --debug.
    void setVerbose(bool on);

signals:
    void runningAppsChanged(const QStringList &appIds);
    void windowUrgent(const QString &appId);
    void windowNotUrgent(const QString &appId);
    void windowCountChanged(const QString &appId, int count);
    void activeAppChanged(const QString &appId);
    void dockObstructionChanged();

private slots:
    void poll();
    void onWindowAdded(const QString &uuid, const QString &desktopFile);
    void onWindowRemoved(const QString &uuid);
    void onWindowActivated(const QString &uuid);
    void onWindowUrgentChanged(const QString &uuid, bool urgent);
    void onWindowFullSizeChanged(const QString &uuid, bool fullSize, const QString &outputName);
    void onShowingDesktopChanged(bool showing);

private:
    void        setupKWinScript();
    void        runKWinSnippet(const QString &jsBody);
    void        addWindow(const QString &uuid, const QString &desktopFile);
    void        removeWindow(const QString &uuid);
    void        rebuildRunningApps();
    void        recomputeObstruction();
    QStringList windowsForApp(const QString &appId) const;

    QDBusInterface *m_scripting = nullptr;
    KWinBridge     *m_bridge    = nullptr;
    QTimer          m_pollTimer;
    QTimer          m_stateTimer;
    bool            m_verbose     = false;
    bool            m_scriptAlive = false;
    bool            m_scriptLoaded  = false;
    bool            m_bridgeRegistered = false;
    bool            m_dockObstructed = false;
    bool            m_showingDesktop = false;
    QString         m_dockScreenName;
    QHash<QString, QString> m_fullSizeWindows;   // uuid → output name
    int             m_actionCounter = 0;

    QMap<QString, QString> m_windowAppIds;   // uuid  → appId
    QMap<QString, int>     m_windowCounts;   // appId → open-window count
    QMap<QString, int>     m_windowCycleIdx; // appId → next cycle index
    QStringList            m_runningApps;
    QSet<QString>          m_urgentApps;     // uuids currently demanding attention
    QString                m_activeAppId;
};
