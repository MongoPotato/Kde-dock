// DockModel is the single source of truth for what appears on the dock.
// Pinned entries come from config; running entries are injected by
// TaskTracker. The merge rule: pinned apps always shown, running-but-not-
// pinned apps appended at the end (like macOS Dock behaviour).

#include "DockModel.h"
#include "ConfigWatcher.h"
#include "TaskTracker.h"

#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

// Resolve a human-readable name and icon from the system .desktop database.
static std::pair<QString, QString> desktopInfo(const QString &appId)
{
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        const QString path = dir + QChar('/') + appId + QStringLiteral(".desktop");
        if (QFile::exists(path)) {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            const QString name = ini.value(QStringLiteral("Name"), appId).toString();
            const QString icon = ini.value(QStringLiteral("Icon"), appId).toString();
            return {name, icon};
        }
    }
    return {appId, appId};
}

DockModel::DockModel(ConfigWatcher *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
{
    connect(m_config, &ConfigWatcher::configChanged, this, &DockModel::onConfigChanged);
    rebuild();
}

void DockModel::setTaskTracker(TaskTracker *tracker)
{
    m_tracker = tracker;
    connect(tracker, &TaskTracker::runningAppsChanged, this, &DockModel::onRunningAppsChanged);
    connect(tracker, &TaskTracker::windowUrgent, this, &DockModel::onWindowUrgent);
    connect(tracker, &TaskTracker::windowCountChanged, this, &DockModel::onWindowCountChanged);
}

int DockModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant DockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const DockEntry &e = m_entries.at(index.row());
    switch (role) {
    case AppIdRole:       return e.appId;
    case DisplayNameRole: return e.displayName;
    case IconNameRole:    return e.iconName;
    case IsPinnedRole:    return e.pinned;
    case IsRunningRole:   return e.running;
    case WindowCountRole: return e.windowCount;
    case IsUrgentRole:    return e.urgent;
    }
    return {};
}

QHash<int, QByteArray> DockModel::roleNames() const
{
    return {
        {AppIdRole,       "appId"},
        {DisplayNameRole, "displayName"},
        {IconNameRole,    "iconName"},
        {IsPinnedRole,    "isPinned"},
        {IsRunningRole,   "isRunning"},
        {WindowCountRole, "windowCount"},
        {IsUrgentRole,    "isUrgent"},
    };
}

void DockModel::rebuild()
{
    beginResetModel();
    m_entries.clear();

    const QStringList pinned = m_config->pinnedApps();
    QSet<QString> seen;

    for (const QString &id : pinned) {
        DockEntry e = makeEntry(id, true);
        e.running = m_runningApps.contains(id);
        e.windowCount = m_windowCounts.value(id, 0);
        e.urgent = m_urgentApps.contains(id);
        m_entries.append(e);
        seen.insert(id);
    }

    // Append running-but-not-pinned apps at the end
    for (const QString &id : std::as_const(m_runningApps)) {
        if (!seen.contains(id)) {
            DockEntry e = makeEntry(id, false);
            e.running = true;
            e.windowCount = m_windowCounts.value(id, 0);
            e.urgent = m_urgentApps.contains(id);
            m_entries.append(e);
        }
    }

    endResetModel();
}

DockModel::DockEntry DockModel::makeEntry(const QString &appId, bool pinned) const
{
    auto [name, icon] = desktopInfo(appId);
    DockEntry e;
    e.appId = appId;
    e.displayName = name;
    e.iconName = icon;
    e.pinned = pinned;
    return e;
}

int DockModel::indexOf(const QString &appId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).appId == appId)
            return i;
    return -1;
}

void DockModel::pinApp(const QString &appId)
{
    QStringList pinned = m_config->pinnedApps();
    if (!pinned.contains(appId)) {
        pinned.append(appId);
        m_config->setPinnedApps(pinned);
        m_config->save();
    }
}

void DockModel::unpinApp(const QString &appId)
{
    QStringList pinned = m_config->pinnedApps();
    if (pinned.removeAll(appId)) {
        m_config->setPinnedApps(pinned);
        m_config->save();
    }
}

void DockModel::launchApp(const QString &appId)
{
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        const QString path = dir + QChar('/') + appId + QStringLiteral(".desktop");
        if (QFile::exists(path)) {
            QProcess::startDetached(QStringLiteral("kioclient5"), {QStringLiteral("exec"), path});
            return;
        }
    }
    // Fallback: try to launch by appId directly
    QProcess::startDetached(appId, {});
}

void DockModel::closeApp(const QString &appId)
{
    if (m_tracker)
        m_tracker->closeWindows(appId);
}

void DockModel::onConfigChanged()
{
    rebuild();
}

void DockModel::onRunningAppsChanged(const QStringList &appIds)
{
    m_runningApps = appIds;
    rebuild();
}

void DockModel::onWindowUrgent(const QString &appId)
{
    m_urgentApps.insert(appId);
    const int idx = indexOf(appId);
    if (idx >= 0) {
        m_entries[idx].urgent = true;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {IsUrgentRole});
    }
}

void DockModel::onWindowCountChanged(const QString &appId, int count)
{
    m_windowCounts[appId] = count;
    const int idx = indexOf(appId);
    if (idx >= 0) {
        m_entries[idx].windowCount = count;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {WindowCountRole});
    }
}
