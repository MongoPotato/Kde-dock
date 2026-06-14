// TaskTracker bridges between KWin's window list and DockModel.
// We prefer DBus signals over polling when available; polling is the
// fallback for KWin versions that don't expose the relevant signals.
// appId matching heuristic: lowercase resourceClass → strip ".desktop"
// suffix → compare against known app IDs from the .desktop file index.

#include "TaskTracker.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QStandardPaths>

static constexpr int kPollIntervalMs = 500;

// Extract the last dotted component as a lowercase short name.
// "org.kde.konsole" → "konsole",  "konsole" → "konsole"
static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

static QString normaliseAppId(const QString &resourceClass)
{
    if (resourceClass.isEmpty())
        return {};
    QString id = resourceClass.toLower();

    // Try to find a matching .desktop file for this resource class
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        // Try exact match
        if (QFile::exists(dir + QChar('/') + id + QStringLiteral(".desktop")))
            return id;
        // Try with org.kde. prefix
        const QString kdeid = QStringLiteral("org.kde.") + id;
        if (QFile::exists(dir + QChar('/') + kdeid + QStringLiteral(".desktop")))
            return kdeid;
        // Try with org.mozilla. prefix
        const QString mozid = QStringLiteral("org.mozilla.") + id;
        if (QFile::exists(dir + QChar('/') + mozid + QStringLiteral(".desktop")))
            return mozid;
    }
    return id;
}

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
{
    m_kwin = new QDBusInterface(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QDBusConnection::sessionBus(),
        this);

    // Connect to KWin window signals when available
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("windowAdded"),
        this, SLOT(onWindowAdded(quint64)));

    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("windowRemoved"),
        this, SLOT(onWindowRemoved(quint64)));

    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("windowActivated"),
        this, SLOT(onWindowActivated(quint64)));

    // Polling fallback for KWin versions without signals
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::poll);
    m_pollTimer.start();

    poll();
}

TaskTracker::~TaskTracker() = default;

void TaskTracker::poll()
{
    refresh();
}

void TaskTracker::onWindowAdded(quint64 /*id*/)
{
    refresh();
}

void TaskTracker::onWindowRemoved(quint64 /*id*/)
{
    refresh();
}

void TaskTracker::onWindowActivated(quint64 id)
{
    const QString appId = m_windowAppIds.value(id);
    if (appId != m_activeAppId) {
        m_activeAppId = appId;
        emit activeAppChanged(m_activeAppId);
    }
}

QString TaskTracker::activeAppId() const
{
    return m_activeAppId;
}

void TaskTracker::refresh()
{
    if (!m_kwin->isValid())
        return;

    // Query KWin for all windows via the Scripting interface
    QDBusReply<QVariantList> reply = m_kwin->call(QStringLiteral("getWindowInfo"));

    QMap<QString, int> newCounts;
    QMap<quint64, QString> newWindowAppIds;
    QSet<QString> newUrgent;

    if (reply.isValid()) {
        for (const QVariant &v : reply.value()) {
            const QVariantMap info = v.toMap();
            const quint64 wid = info.value(QStringLiteral("id")).toULongLong();
            const QString appId = windowToAppId(info);
            if (!appId.isEmpty()) {
                newWindowAppIds[wid] = appId;
                newCounts[appId]++;

                if (info.value(QStringLiteral("demandsAttention")).toBool())
                    newUrgent.insert(appId);
            }
        }
    }

    // Emit urgent / not-urgent transitions
    for (const QString &id : newUrgent)
        if (!m_urgentApps.contains(id)) emit windowUrgent(id);
    for (const QString &id : std::as_const(m_urgentApps))
        if (!newUrgent.contains(id)) emit windowNotUrgent(id);
    m_urgentApps = newUrgent;

    m_windowAppIds = newWindowAppIds;

    // Emit window count changes
    const QStringList allIds = (m_windowCounts.keys() + newCounts.keys());
    for (const QString &id : allIds) {
        const int oldCount = m_windowCounts.value(id, 0);
        const int newCount = newCounts.value(id, 0);
        if (oldCount != newCount)
            emit windowCountChanged(id, newCount);
    }
    m_windowCounts = newCounts;

    const QStringList newRunning = newCounts.keys();
    if (newRunning != m_runningApps) {
        m_runningApps = newRunning;
        qDebug("kdock [running]: apps changed → [%s]",
               qPrintable(m_runningApps.join(QStringLiteral(", "))));
        emit runningAppsChanged(m_runningApps);
    }
}

QString TaskTracker::windowToAppId(const QVariantMap &info) const
{
    // Prefer resourceClass, fall back to resourceName
    QString cls = info.value(QStringLiteral("resourceClass")).toString();
    if (cls.isEmpty())
        cls = info.value(QStringLiteral("resourceName")).toString();
    return normaliseAppId(cls);
}

QStringList TaskTracker::runningApps() const
{
    return m_runningApps;
}

void TaskTracker::closeWindows(const QString &appId)
{
    for (auto it = m_windowAppIds.cbegin(); it != m_windowAppIds.cend(); ++it) {
        if (it.value() == appId) {
            m_kwin->call(QStringLiteral("closeWindow"), it.key());
        }
    }
}

QList<quint64> TaskTracker::windowsForApp(const QString &appId) const
{
    QList<quint64> result;
    const QString sn = shortName(appId);
    for (auto it = m_windowAppIds.cbegin(); it != m_windowAppIds.cend(); ++it) {
        if (it.value() == appId || shortName(it.value()) == sn)
            result.append(it.key());
    }
    return result;
}

bool TaskTracker::hasWindowForApp(const QString &appId) const
{
    return !windowsForApp(appId).isEmpty();
}

void TaskTracker::activateWindow(const QString &appId)
{
    const QList<quint64> windows = windowsForApp(appId);
    if (windows.isEmpty()) {
        qDebug("kdock [activate]: no windows found for appId=%s (shortName=%s)  tracked: %d windows",
               qPrintable(appId), qPrintable(shortName(appId)), (int)m_windowAppIds.size());
        return;
    }

    int &idx = m_windowCycleIndex[appId];
    if (idx >= windows.size())
        idx = 0;
    const int current = idx;
    idx = (idx + 1) % windows.size();
    const quint64 wid = windows.at(current);

    qDebug("kdock [activate]: wid=%llu (window %d/%d) for appId=%s",
           (unsigned long long)wid, current + 1, (int)windows.size(), qPrintable(appId));

    const QDBusMessage reply = m_kwin->call(QStringLiteral("activateWindow"),
                                             static_cast<qlonglong>(wid));
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("kdock [activate]: KWin DBus error: %s", qPrintable(reply.errorMessage()));
}
