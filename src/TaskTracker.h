#pragma once

// TaskTracker bridges between KWin's window list and DockModel.
// We prefer DBus signals over polling when available; polling is the
// fallback for KWin versions that don't expose the relevant signals.
// appId matching heuristic: lowercase resourceClass → strip ".desktop"
// suffix → compare against known app IDs from the .desktop file index.

#include <QList>
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
    QString activeAppId() const;
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
    // Both quint64 and qlonglong variants so DBus signals connect regardless
    // of whether KWin emits unsigned or signed 64-bit window IDs.
    void onWindowAdded(quint64 id);
    void onWindowAdded(qlonglong id);
    void onWindowRemoved(quint64 id);
    void onWindowRemoved(qlonglong id);
    void onWindowActivated(quint64 id);
    void onWindowActivated(qlonglong id);

private:
    QString windowToAppId(const QVariantMap &info) const;
    void refresh();
    QList<quint64> windowsForApp(const QString &appId) const;
    void processWindowInfo(const QVariantMap &info,
                           QMap<quint64, QString> &windowAppIds,
                           QMap<QString, int> &counts,
                           QSet<QString> &urgent,
                           bool verbose);

    QDBusInterface *m_kwin = nullptr;
    QTimer m_pollTimer;
    QMap<quint64, QString> m_windowAppIds;  // windowId → appId
    QMap<QString, int> m_windowCounts;      // appId → count
    QStringList m_runningApps;
    QSet<QString> m_urgentApps;
    QString m_activeAppId;
    QMap<QString, int> m_windowCycleIndex;  // appId → next window index for cycling
};
