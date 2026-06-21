#pragma once

// TaskTracker: KDE Plasma window tracking and control via the
// plasma-window-management Wayland protocol (org_kde_plasma_window_management).
//
// KWin 6's DBus interface (org.kde.KWin at /KWin) has no per-window
// activate/close mechanism — confirmed via a full Introspect dump, which
// lists only desktop-level methods (killWindow is an interactive
// cursor-pick action, getWindowInfo is read-only; there is no
// /org/kde/KWin/Windows/<id> object at any path). plasma-window-management
// is KDE's purpose-built Wayland protocol for exactly this: each mapped
// window is bound as its own Wayland object (org_kde_plasma_window)
// exposing set_state()/close() requests and app_id_changed/state_changed/
// unmapped events — entirely over Wayland, no DBus involved.

#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>

struct wl_registry;

class TaskTracker : public QObject {
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);
    ~TaskTracker() override;

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

private:
    // Defined in TaskTracker.cpp — wrap the generated QtWayland:: classes.
    class PlasmaWindowManagement;
    class PlasmaWindow;
    friend class PlasmaWindowManagement;
    friend class PlasmaWindow;

    static void handleRegistryGlobal(void *data, wl_registry *registry,
                                      uint32_t name, const char *interface, uint32_t version);
    static void handleRegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name);

    void detectWayland();
    void registerWindow(PlasmaWindow *window, const QString &uuid);
    void onWindowAppIdChanged(PlasmaWindow *window);
    void onWindowStateChanged(PlasmaWindow *window);
    void onWindowUnmapped(PlasmaWindow *window);

    void rebuildRunningApps();
    void refreshActiveAndUrgent();
    QList<PlasmaWindow *> windowsForApp(const QString &appId) const;

    PlasmaWindowManagement *m_windowManagement = nullptr;
    bool m_isWayland = false;

    QMap<QString, PlasmaWindow *> m_windows;       // uuid    → window
    QMap<QString, int>            m_windowCounts;  // appId   → open-window count
    QMap<QString, int>            m_windowCycleIdx; // appId  → next cycle index
    QStringList                   m_runningApps;
    QSet<QString>                 m_urgentApps;
    QString                       m_activeAppId;
};
