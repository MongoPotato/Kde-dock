#pragma once

// TaskTracker bridges between KWin's window list and DockModel.
// We prefer DBus signals over polling when available; polling is the
// fallback for KWin versions that don't expose the relevant signals.
// appId matching heuristic: lowercase resourceClass → strip ".desktop"
// suffix → compare against known app IDs from the .desktop file index.

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QDBusInterface;

class TaskTracker : public QObject {
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);
    ~TaskTracker();

    QStringList runningApps() const;
    Q_INVOKABLE void closeWindows(const QString &appId);

signals:
    void runningAppsChanged(const QStringList &appIds);
    void windowUrgent(const QString &appId);
    void windowCountChanged(const QString &appId, int count);

private slots:
    void poll();
    void onWindowAdded(quint64 id);
    void onWindowRemoved(quint64 id);

private:
    QString windowToAppId(const QVariantMap &info) const;
    void refresh();

    QDBusInterface *m_kwin = nullptr;
    QTimer m_pollTimer;
    QMap<quint64, QString> m_windowAppIds;  // windowId → appId
    QMap<QString, int> m_windowCounts;      // appId → count
    QStringList m_runningApps;
};
