// DockModel is the single source of truth for what appears on the dock.
// Pinned entries come from config; running entries are injected by
// TaskTracker. The merge rule: pinned apps always shown, running-but-not-
// pinned apps appended at the end (like macOS Dock behaviour).

#include "DockModel.h"
#include "ConfigWatcher.h"
#include "IconThemeDetector.h"
#include "TaskTracker.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

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
    connect(tracker, &TaskTracker::runningAppsChanged,  this, &DockModel::onRunningAppsChanged);
    connect(tracker, &TaskTracker::windowUrgent,        this, &DockModel::onWindowUrgent);
    connect(tracker, &TaskTracker::windowNotUrgent,     this, &DockModel::onWindowNotUrgent);
    connect(tracker, &TaskTracker::windowCountChanged,  this, &DockModel::onWindowCountChanged);
}

void DockModel::setIconThemeDetector(IconThemeDetector *detector)
{
    connect(detector, &IconThemeDetector::themeChanged, this, &DockModel::refreshAllIcons);
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
        e.running = appMatchesRunning(id);
        e.windowCount = windowCountForRunning(id);
        e.urgent = m_urgentApps.contains(id);
        m_entries.append(e);
        seen.insert(id);
    }

    // Append running-but-not-pinned apps at the end.
    // Skip if a pinned entry already covers this app via short-name match.
    for (const QString &id : std::as_const(m_runningApps)) {
        bool alreadySeen = seen.contains(id);
        if (!alreadySeen) {
            const QString sn = shortName(id);
            for (const QString &s : std::as_const(seen))
                if (shortName(s) == sn) { alreadySeen = true; break; }
        }
        if (!alreadySeen) {
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

// Fuzzy indexOf: exact match first, then short-name fallback.
// Handles tracker ids like "konsole" matching dock ids like "org.kde.konsole".
int DockModel::indexOfFuzzy(const QString &appId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).appId == appId)
            return i;
    const QString sn = shortName(appId);
    for (int i = 0; i < m_entries.size(); ++i)
        if (shortName(m_entries.at(i).appId) == sn)
            return i;
    return -1;
}

bool DockModel::appMatchesRunning(const QString &appId) const
{
    if (m_runningApps.contains(appId))
        return true;
    const QString sn = shortName(appId);
    for (const QString &ra : m_runningApps)
        if (shortName(ra) == sn)
            return true;
    return false;
}

int DockModel::windowCountForRunning(const QString &appId) const
{
    if (m_windowCounts.contains(appId))
        return m_windowCounts.value(appId, 0);
    const QString sn = shortName(appId);
    for (auto it = m_windowCounts.cbegin(); it != m_windowCounts.cend(); ++it)
        if (shortName(it.key()) == sn)
            return it.value();
    return 0;
}

void DockModel::pinApp(const QString &appId)
{
    QStringList pinned = m_config->pinnedApps();
    if (!pinned.contains(appId)) {
        pinned.append(appId);
        m_config->setPinnedApps(pinned);
        m_config->save();
        rebuild();  // apply immediately; don't wait for async file-watcher
    }
}

void DockModel::unpinApp(const QString &appId)
{
    QStringList pinned = m_config->pinnedApps();
    if (pinned.removeAll(appId)) {
        m_config->setPinnedApps(pinned);
        m_config->save();
        rebuild();  // apply immediately; don't wait for async file-watcher
    }
}

void DockModel::launchApp(const QString &appId)
{
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        const QString path = dir + QChar('/') + appId + QStringLiteral(".desktop");
        if (QFile::exists(path)) {
            qDebug("kdock [launch]: %s → found .desktop at %s", qPrintable(appId), qPrintable(path));
            if (QProcess::startDetached(QStringLiteral("gtk-launch"), {appId})) {
                qDebug("kdock [launch]: launched via gtk-launch");
                return;
            }
            if (QProcess::startDetached(QStringLiteral("kioclient5"), {QStringLiteral("exec"), path})) {
                qDebug("kdock [launch]: launched via kioclient5");
                return;
            }
            QProcess::startDetached(QStringLiteral("xdg-open"), {path});
            qDebug("kdock [launch]: launched via xdg-open");
            return;
        }
    }
    qDebug("kdock [launch]: no .desktop for %s — tried: %s — falling back to binary",
           qPrintable(appId),
           qPrintable(dataDirs.join(QStringLiteral(", "))));
    QProcess::startDetached(appId, {});
}

// Immediately mark an appId as running in the model without waiting for the
// next TaskTracker poll cycle. This makes the running indicator appear at once
// and ensures subsequent clicks try to activate rather than re-launch.
void DockModel::markRunning(const QString &appId)
{
    if (!m_runningApps.contains(appId))
        m_runningApps.append(appId);
    const int idx = indexOfFuzzy(appId);
    if (idx >= 0 && !m_entries.at(idx).running) {
        m_entries[idx].running = true;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {IsRunningRole});
    }
}

void DockModel::activateApp(const QString &appId)
{
    const bool hasWindows = m_tracker && m_tracker->hasWindowForApp(appId);
    const bool looksRunning = appMatchesRunning(appId);

    qDebug("kdock [activate]: app=%s  hasWindows=%s  looksRunning=%s  runningApps=[%s]",
           qPrintable(appId),
           hasWindows ? "true" : "false",
           looksRunning ? "true" : "false",
           qPrintable(m_runningApps.join(QStringLiteral(", "))));

    if (hasWindows) {
        // KWin has a window ID — cycle through open windows to bring one to front
        m_tracker->activateWindow(appId);
        return;
    }

    // No tracked window: launch (gtk-launch focuses existing GApplication instances).
    // Always mark as running immediately so the indicator lights up and the next
    // click tries activation instead of opening another duplicate window.
    launchApp(appId);
    markRunning(appId);
}

void DockModel::closeApp(const QString &appId)
{
    if (m_tracker)
        m_tracker->closeWindows(appId);
}

void DockModel::addAppDialog()
{
    // Open the applications directory so the user can browse and drag .desktop entries.
    // kmenuedit is the KDE-native editor; xdg-open is the universal fallback.
    const QString appsDir = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation).value(0);
    if (!QProcess::startDetached(QStringLiteral("kmenuedit"), {}))
        if (!QProcess::startDetached(QStringLiteral("dolphin"), {appsDir}))
            QProcess::startDetached(QStringLiteral("xdg-open"), {appsDir});
}

bool DockModel::isAppRunning(const QString &appId) const
{
    const int idx = indexOf(appId);
    return idx >= 0 && m_entries.at(idx).running;
}

bool DockModel::isAppPinned(const QString &appId) const
{
    const int idx = indexOf(appId);
    return idx >= 0 && m_entries.at(idx).pinned;
}

int DockModel::windowCountForApp(const QString &appId) const
{
    return windowCountForRunning(appId);
}

QString DockModel::displayNameForApp(const QString &appId) const
{
    const int idx = indexOf(appId);
    return idx >= 0 ? m_entries.at(idx).displayName : appId;
}

QString DockModel::iconNameForApp(const QString &appId) const
{
    const int idx = indexOf(appId);
    return idx >= 0 ? m_entries.at(idx).iconName : appId;
}

QColor DockModel::iconDominantColor(const QString &iconName) const
{
    static QMap<QString, QColor> cache;
    if (cache.contains(iconName))
        return cache.value(iconName);

    const QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) {
        cache[iconName] = QColor(60, 70, 100);
        return cache[iconName];
    }

    const QImage img = icon.pixmap(QSize(32, 32)).toImage();
    int r = 0, g = 0, b = 0, count = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            const QColor c = QColor::fromRgba(img.pixel(x, y));
            if (c.alpha() > 100) {
                r += c.red(); g += c.green(); b += c.blue(); ++count;
            }
        }
    }
    const QColor result = (count > 0) ? QColor(r/count, g/count, b/count)
                                      : QColor(60, 70, 100);
    cache[iconName] = result;
    return result;
}

void DockModel::moveApp(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex) return;

    QStringList pinned = m_config->pinnedApps();
    if (fromIndex < 0 || fromIndex >= pinned.size()) return;
    if (toIndex   < 0 || toIndex   >= pinned.size()) return;

    pinned.move(fromIndex, toIndex);
    m_config->setPinnedApps(pinned);
    m_config->save();
    rebuild();
}

void DockModel::clearUrgency(const QString &appId)
{
    m_urgentApps.remove(appId);
    const int idx = indexOfFuzzy(appId);
    if (idx >= 0 && m_entries.at(idx).urgent) {
        m_entries[idx].urgent = false;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {IsUrgentRole});
    }
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
    const int idx = indexOfFuzzy(appId);
    if (idx >= 0) {
        m_entries[idx].urgent = true;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {IsUrgentRole});
    }
}

void DockModel::onWindowNotUrgent(const QString &appId)
{
    clearUrgency(appId);
}

void DockModel::onWindowCountChanged(const QString &appId, int count)
{
    m_windowCounts[appId] = count;
    const int idx = indexOfFuzzy(appId);
    if (idx >= 0) {
        m_entries[idx].windowCount = count;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {WindowCountRole});
    }
}

void DockModel::refreshAllIcons()
{
    if (m_entries.isEmpty())
        return;
    emit dataChanged(index(0), index(m_entries.size() - 1), {IconNameRole});
}
