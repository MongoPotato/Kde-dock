// TaskTracker — KWin 6 Wayland window tracking.
//
// KWin 6 exposes windows via:
//   • signals windowAdded/windowRemoved/windowActivated(string uuid)
//   • /KWin org.kde.KWin getWindowInfo(string uuid) → QVariantMap
//   • /org/kde/KWin/Windows/{uuid} org.kde.KWin.Window activate()/close()
//
// For startup enumeration we introspect /org/kde/KWin/Windows to discover
// which window objects are already registered, then call getWindowInfo for each.

#include "TaskTracker.h"

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

// Extract the last dotted component as lowercase: "org.kde.konsole" → "konsole"
static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

// Pull a QVariantMap out of a raw DBus reply argument (handles QDBusArgument wrapping).
static QVariantMap extractMap(const QVariant &v)
{
    if (v.canConvert<QVariantMap>())
        return v.value<QVariantMap>();
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return {};
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

    qDebug("kdock [tasktracker]: KWin DBus — valid=%s  error='%s'",
           m_kwin->isValid() ? "true" : "false",
           qPrintable(m_kwin->lastError().message()));

    // KWin 6 signals carry QString UUID arguments
    struct { const char *sig; const char *slot; } sigs[] = {
        {"windowAdded",     SLOT(onWindowAdded(QString))},
        {"windowRemoved",   SLOT(onWindowRemoved(QString))},
        {"windowActivated", SLOT(onWindowActivated(QString))},
    };
    for (const auto &s : sigs) {
        const bool ok = QDBusConnection::sessionBus().connect(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
            QStringLiteral("org.kde.KWin"), s.sig, this, s.slot);
        qDebug("kdock [tasktracker]: signal '%s' → connected=%s", s.sig, ok ? "YES" : "NO");
    }

    // Enumerate windows that already exist before we started listening
    const QSet<QString> existing = enumerateWindowUuids();
    qDebug("kdock [tasktracker]: found %d existing windows at startup", (int)existing.size());
    for (const QString &uuid : existing)
        addWindowByUuid(uuid);

    // Light poll every 2 s to resync in case a signal is missed
    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::poll);
    m_pollTimer.start();
}

TaskTracker::~TaskTracker() = default;

// ── Poll: full resync ─────────────────────────────────────────────────────────
void TaskTracker::poll()
{
    const QSet<QString> current = enumerateWindowUuids();

    // Remove windows that have disappeared
    for (const QString &uuid : QStringList(m_windowAppIds.keys()))
        if (!current.contains(uuid))
            removeWindowByUuid(uuid);

    // Add windows we haven't seen yet
    for (const QString &uuid : current)
        addWindowByUuid(uuid);
}

// ── Signal slots ──────────────────────────────────────────────────────────────
void TaskTracker::onWindowAdded(const QString &uuid)
{
    qDebug("kdock [tasktracker]: windowAdded uuid='%s'", qPrintable(uuid));
    addWindowByUuid(uuid);
}

void TaskTracker::onWindowRemoved(const QString &uuid)
{
    qDebug("kdock [tasktracker]: windowRemoved uuid='%s'", qPrintable(uuid));
    removeWindowByUuid(uuid);
}

void TaskTracker::onWindowActivated(const QString &uuid)
{
    const QString appId = m_windowAppIds.value(uuid);
    qDebug("kdock [tasktracker]: windowActivated uuid='%s' → appId='%s'",
           qPrintable(uuid), qPrintable(appId));
    if (appId != m_activeAppId) {
        m_activeAppId = appId;
        emit activeAppChanged(m_activeAppId);
    }
}

// ── Window enumeration ────────────────────────────────────────────────────────
// Introspect /org/kde/KWin/Windows to find all currently registered window objects.
// Each window is a child node named after its UUID: <node name="{…}"/>.
QSet<QString> TaskTracker::enumerateWindowUuids() const
{
    const QDBusMessage im = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/Windows"),
        QStringLiteral("org.freedesktop.DBus.Introspectable"),
        QStringLiteral("Introspect"));
    const QDBusMessage ir = QDBusConnection::sessionBus().call(im);

    QSet<QString> result;
    if (ir.type() != QDBusMessage::ReplyMessage || ir.arguments().isEmpty()) {
        qDebug("kdock [tasktracker]: enumerateWindowUuids failed — type=%d  error='%s'",
               (int)ir.type(), qPrintable(ir.errorMessage()));
        return result;
    }

    const QString xml = ir.arguments().first().toString();
    static const QRegularExpression re(QStringLiteral("<node name=\"(\\{[^}]+\\})\"/>"));
    QRegularExpressionMatchIterator it = re.globalMatch(xml);
    while (it.hasNext())
        result.insert(it.next().captured(1));
    return result;
}

// ── Per-window add / remove ───────────────────────────────────────────────────
void TaskTracker::addWindowByUuid(const QString &uuid)
{
    if (m_windowAppIds.contains(uuid))
        return;

    const QVariantMap info = getWindowInfoByUuid(uuid);
    if (info.isEmpty())
        return;

    const QString appId = windowToAppId(info);
    if (appId.isEmpty())
        return;

    qDebug("kdock [tasktracker]: track uuid='%s' → appId='%s'",
           qPrintable(uuid), qPrintable(appId));

    m_windowAppIds[uuid] = appId;
    m_windowCounts[appId] = m_windowCounts.value(appId, 0) + 1;
    emit windowCountChanged(appId, m_windowCounts[appId]);
    rebuildRunningApps();
}

void TaskTracker::removeWindowByUuid(const QString &uuid)
{
    const QString appId = m_windowAppIds.value(uuid);
    if (appId.isEmpty())
        return;

    qDebug("kdock [tasktracker]: untrack uuid='%s' appId='%s'",
           qPrintable(uuid), qPrintable(appId));

    m_windowAppIds.remove(uuid);
    const int newCount = m_windowCounts.value(appId, 1) - 1;
    if (newCount <= 0) {
        m_windowCounts.remove(appId);
        emit windowCountChanged(appId, 0);
    } else {
        m_windowCounts[appId] = newCount;
        emit windowCountChanged(appId, newCount);
    }
    rebuildRunningApps();
}

QVariantMap TaskTracker::getWindowInfoByUuid(const QString &uuid) const
{
    const QDBusMessage raw = m_kwin->call(QStringLiteral("getWindowInfo"), uuid);
    if (raw.type() != QDBusMessage::ReplyMessage || raw.arguments().isEmpty())
        return {};
    return extractMap(raw.arguments().first());
}

// ── App ID resolution ─────────────────────────────────────────────────────────
QString TaskTracker::windowToAppId(const QVariantMap &info) const
{
    // KWin 6: "desktopFile" is already the canonical .desktop stem (e.g. "org.kde.konsole")
    const QString df = info.value(QStringLiteral("desktopFile")).toString().toLower();
    if (!df.isEmpty())
        return df;

    // Fallback: resourceClass, then resourceName — verify against .desktop file system
    QString cls = info.value(QStringLiteral("resourceClass")).toString();
    if (cls.isEmpty())
        cls = info.value(QStringLiteral("resourceName")).toString();
    if (cls.isEmpty())
        return {};

    cls = cls.toLower();
    const QStringList dataDirs = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        if (QFile::exists(dir + '/' + cls + QStringLiteral(".desktop")))
            return cls;
        const QString kdeId = QStringLiteral("org.kde.") + cls;
        if (QFile::exists(dir + '/' + kdeId + QStringLiteral(".desktop")))
            return kdeId;
        const QString mozId = QStringLiteral("org.mozilla.") + cls;
        if (QFile::exists(dir + '/' + mozId + QStringLiteral(".desktop")))
            return mozId;
    }
    return cls;
}

// ── Running apps ──────────────────────────────────────────────────────────────
void TaskTracker::rebuildRunningApps()
{
    QStringList newRunning = m_windowCounts.keys();
    newRunning.sort();
    QStringList oldRunning = m_runningApps;
    oldRunning.sort();

    if (newRunning != oldRunning) {
        m_runningApps = m_windowCounts.keys();
        qDebug("kdock [running]: apps changed → [%s]",
               qPrintable(m_runningApps.join(QStringLiteral(", "))));
        emit runningAppsChanged(m_runningApps);
    }
}

QStringList TaskTracker::runningApps() const { return m_runningApps; }
QString     TaskTracker::activeAppId()  const { return m_activeAppId; }

// ── Window lookup (with short-name fuzzy match) ───────────────────────────────
QStringList TaskTracker::windowsForApp(const QString &appId) const
{
    QStringList result;
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

// ── Window actions ────────────────────────────────────────────────────────────
void TaskTracker::activateWindow(const QString &appId)
{
    const QStringList uuids = windowsForApp(appId);
    if (uuids.isEmpty()) {
        qDebug("kdock [activate]: no windows for appId='%s'  tracked=%d",
               qPrintable(appId), (int)m_windowAppIds.size());
        return;
    }

    int &idx = m_windowCycleIdx[appId];
    if (idx >= uuids.size()) idx = 0;
    const QString uuid = uuids.at(idx);
    idx = (idx + 1) % uuids.size();

    qDebug("kdock [activate]: uuid='%s' (%d/%d) for appId='%s'",
           qPrintable(uuid), idx, (int)uuids.size(), qPrintable(appId));

    // KWin 6: each window has its own DBus object with an activate() method
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/Windows/") + uuid,
        QStringLiteral("org.kde.KWin.Window"),
        QStringLiteral("activate"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("kdock [activate]: error — path='/org/kde/KWin/Windows/%s' : %s",
                 qPrintable(uuid), qPrintable(reply.errorMessage()));
}

void TaskTracker::closeWindows(const QString &appId)
{
    for (const QString &uuid : windowsForApp(appId)) {
        const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/org/kde/KWin/Windows/") + uuid,
            QStringLiteral("org.kde.KWin.Window"),
            QStringLiteral("close"));
        QDBusConnection::sessionBus().call(call);
    }
}
