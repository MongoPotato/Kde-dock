#pragma once

// DockModel is the single source of truth for what appears on the dock.
// Pinned entries come from config; running entries are injected by
// TaskTracker. The merge rule: pinned apps always shown, running-but-not-
// pinned apps appended at the end (like macOS Dock behaviour).

#include <QAbstractListModel>
#include <QColor>
#include <QStringList>

class ConfigWatcher;
class IconThemeDetector;
class TaskTracker;

class DockModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        IconNameRole,
        IsPinnedRole,
        IsRunningRole,
        WindowCountRole,
        IsUrgentRole,
    };
    Q_ENUM(Roles)

    explicit DockModel(ConfigWatcher *config, QObject *parent = nullptr);

    void setTaskTracker(TaskTracker *tracker);
    void setIconThemeDetector(IconThemeDetector *detector);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void pinApp(const QString &appId);
    Q_INVOKABLE void unpinApp(const QString &appId);
    Q_INVOKABLE void moveApp(int fromIndex, int toIndex);
    Q_INVOKABLE void launchApp(const QString &appId);
    Q_INVOKABLE void activateApp(const QString &appId);
    Q_INVOKABLE void closeApp(const QString &appId);
    Q_INVOKABLE void addAppDialog();
    Q_INVOKABLE void clearUrgency(const QString &appId);

    // Query helpers used by ContextMenu.qml and AppPickerPanel
    Q_INVOKABLE bool    isAppRunning(const QString &appId) const;
    Q_INVOKABLE bool    isAppPinned(const QString &appId) const;
    Q_INVOKABLE int     windowCountForApp(const QString &appId) const;
    Q_INVOKABLE QString displayNameForApp(const QString &appId) const;
    Q_INVOKABLE QString iconNameForApp(const QString &appId) const;
    Q_INVOKABLE QColor  iconDominantColor(const QString &iconName) const;

public slots:
    void onConfigChanged();
    void onRunningAppsChanged(const QStringList &appIds);
    void onWindowUrgent(const QString &appId);
    void onWindowNotUrgent(const QString &appId);
    void onWindowCountChanged(const QString &appId, int count);
    void refreshAllIcons();

private:
    struct DockEntry {
        QString appId;
        QString displayName;
        QString iconName;
        bool pinned = false;
        bool running = false;
        int windowCount = 0;
        bool urgent = false;
    };

    void rebuild();
    DockEntry makeEntry(const QString &appId, bool pinned) const;
    int indexOf(const QString &appId) const;

    ConfigWatcher *m_config;
    TaskTracker *m_tracker = nullptr;
    QList<DockEntry> m_entries;
    QStringList m_runningApps;
    QMap<QString, int> m_windowCounts;
    QSet<QString> m_urgentApps;
};
