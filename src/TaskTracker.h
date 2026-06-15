#pragma once

// TaskTracker: KWin 6 Wayland window tracking via KWin JavaScript scripting.
//
// KWin's main DBus interface (/KWin, org.kde.KWin) does NOT export
// windowAdded/windowRemoved/windowActivated as DBus signals in KWin 6, and
// there is no /org/kde/KWin/Windows parent path to introspect for existing
// windows.
//
// The correct approach is to load a short JavaScript snippet into KWin's
// own scripting engine (org.kde.kwin.Scripting at /Scripting). The script
// has access to workspace.windowList() for existing windows and
// workspace.windowAdded/windowRemoved/windowActivated for live tracking.
// It calls back to our process via callDBus() to a bridge object we register
// at org.kde.kdock / /WindowTracker.

#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class QDBusInterface;

// Minimal QObject registered on the session bus as org.kde.kdock /WindowTracker.
// The KWin script calls its Q_SCRIPTABLE slots to report window events.
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

signals:
    void windowAdded(const QString &uuid, const QString &desktopFile);
    void windowRemoved(const QString &uuid);
    void windowActivated(const QString &uuid);
};

class TaskTracker : public QObject {
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);
    ~TaskTracker();

    QStringList runningApps() const;
    QString     activeAppId() const;

    Q_INVOKABLE void closeWindows(const QString &appId);
    Q_INVOKABLE void activateWindow(const QString &appId);

    bool hasWindowForApp(const QString &appId) const;

    Q_PROPERTY(QString activeAppId READ activeAppId NOTIFY activeAppChanged)

signals:
    void runningAppsChanged(const QStringList &appIds);
    void windowUrgent(const QString &appId);
    void windowNotUrgent(const QString &appId);
    void windowCountChanged(const QString &appId, int count);
    void activeAppChanged(const QString &appId);

private slots:
    void poll();
    void onWindowAdded(const QString &uuid, const QString &desktopFile);
    void onWindowRemoved(const QString &uuid);
    void onWindowActivated(const QString &uuid);

private:
    void        setupKWinScript();
    void        addWindow(const QString &uuid, const QString &desktopFile);
    void        removeWindow(const QString &uuid);
    void        rebuildRunningApps();
    QStringList windowsForApp(const QString &appId) const;

    QDBusInterface *m_kwin   = nullptr;
    KWinBridge     *m_bridge = nullptr;
    QTimer          m_pollTimer;
    bool            m_scriptLoaded = false;

    QMap<QString, QString> m_windowAppIds;   // uuid  → appId
    QMap<QString, int>     m_windowCounts;   // appId → open-window count
    QMap<QString, int>     m_windowCycleIdx; // appId → next cycle index
    QStringList            m_runningApps;
    QSet<QString>          m_urgentApps;
    QString                m_activeAppId;
};
