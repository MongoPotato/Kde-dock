#pragma once

// TaskTracker bridges between KWin's window list and DockModel.
//
// KWin 6 Wayland API (confirmed via introspection):
//   /KWin  org.kde.KWin
//     getWindowInfo(string uuid) → a{sv}   — info for one window
//     queryWindowInfo()          → a{sv}   — info for window under cursor
//     signals: windowAdded(s), windowRemoved(s), windowActivated(s)
//   /org/kde/KWin/Windows/{uuid}  org.kde.KWin.Window
//     activate(), close(), minimize() …
//
// Windows are identified by UUID strings like {00f85a3f-…}.
// The canonical app ID comes from the "desktopFile" field in getWindowInfo.

#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class QDBusInterface;

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
    void onWindowAdded(const QString &uuid);
    void onWindowRemoved(const QString &uuid);
    void onWindowActivated(const QString &uuid);

private:
    QSet<QString>   enumerateWindowUuids() const;
    void            addWindowByUuid(const QString &uuid);
    void            removeWindowByUuid(const QString &uuid);
    QVariantMap     getWindowInfoByUuid(const QString &uuid) const;
    QString         windowToAppId(const QVariantMap &info) const;
    void            rebuildRunningApps();
    QStringList     windowsForApp(const QString &appId) const;

    QDBusInterface *m_kwin = nullptr;
    QTimer          m_pollTimer;

    QMap<QString, QString> m_windowAppIds;    // uuid  → appId
    QMap<QString, int>     m_windowCounts;    // appId → open-window count
    QMap<QString, int>     m_windowCycleIdx;  // appId → next cycle index
    QStringList            m_runningApps;
    QSet<QString>          m_urgentApps;
    QString                m_activeAppId;
};
