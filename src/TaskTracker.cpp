// TaskTracker bridges between KWin's window list and DockModel.
// We prefer DBus signals over polling when available; polling is the
// fallback for KWin versions that don't expose the relevant signals.
// appId matching heuristic: lowercase resourceClass → strip ".desktop"
// suffix → compare against known app IDs from the .desktop file index.

#include "TaskTracker.h"

#include <QDBusArgument>
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

    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dataDirs) {
        if (QFile::exists(dir + QChar('/') + id + QStringLiteral(".desktop")))
            return id;
        const QString kdeid = QStringLiteral("org.kde.") + id;
        if (QFile::exists(dir + QChar('/') + kdeid + QStringLiteral(".desktop")))
            return kdeid;
        const QString mozid = QStringLiteral("org.mozilla.") + id;
        if (QFile::exists(dir + QChar('/') + mozid + QStringLiteral(".desktop")))
            return mozid;
    }
    return id;
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

// Pull a QVariantList out of a raw DBus reply argument.
static QVariantList extractList(const QVariant &v)
{
    if (v.canConvert<QVariantList>())
        return v.value<QVariantList>();
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        QVariantList l;
        v.value<QDBusArgument>() >> l;
        return l;
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

    qDebug("kdock [tasktracker]: KWin DBus — valid=%s  service='%s'  error='%s'",
           m_kwin->isValid() ? "true" : "false",
           qPrintable(m_kwin->service()),
           qPrintable(m_kwin->lastError().message()));

    // Dump the /KWin interface so we can see what methods KWin 6 actually exposes.
    {
        const QDBusMessage im = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
        const QDBusMessage ir = QDBusConnection::sessionBus().call(im);
        if (!ir.arguments().isEmpty()) {
            const QString xml = ir.arguments().first().toString();
            qDebug("kdock [tasktracker]: /KWin introspect (%d chars total, showing first 3000):\n%s",
                   (int)xml.size(), qPrintable(xml.left(3000)));
        } else {
            qDebug("kdock [tasktracker]: /KWin introspect failed — type=%d  error='%s'",
                   (int)ir.type(), qPrintable(ir.errorMessage()));
        }
    }

    // Connect signals — KWin 6 uses qlonglong IDs; try both types.
    struct SigEntry { const char *name; const char *slotU64; const char *slotI64; };
    const SigEntry sigs[] = {
        {"windowAdded",     SLOT(onWindowAdded(quint64)),     SLOT(onWindowAdded(qlonglong))},
        {"windowRemoved",   SLOT(onWindowRemoved(quint64)),   SLOT(onWindowRemoved(qlonglong))},
        {"windowActivated", SLOT(onWindowActivated(quint64)), SLOT(onWindowActivated(qlonglong))},
    };
    for (const auto &s : sigs) {
        bool ok = QDBusConnection::sessionBus().connect(
                      QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                      QStringLiteral("org.kde.KWin"), s.name, this, s.slotU64);
        if (!ok)
            ok = QDBusConnection::sessionBus().connect(
                     QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                     QStringLiteral("org.kde.KWin"), s.name, this, s.slotI64);
        qDebug("kdock [tasktracker]: signal '%s' → connected=%s", s.name, ok ? "YES" : "NO");
    }

    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::poll);
    m_pollTimer.start();

    poll();
}

TaskTracker::~TaskTracker() = default;

void TaskTracker::poll()     { refresh(); }
void TaskTracker::onWindowAdded(quint64)   { refresh(); }
void TaskTracker::onWindowAdded(qlonglong) { refresh(); }
void TaskTracker::onWindowRemoved(quint64)   { refresh(); }
void TaskTracker::onWindowRemoved(qlonglong) { refresh(); }

void TaskTracker::onWindowActivated(quint64 id)
{
    const QString appId = m_windowAppIds.value(id);
    if (appId != m_activeAppId) {
        m_activeAppId = appId;
        emit activeAppChanged(m_activeAppId);
    }
}

void TaskTracker::onWindowActivated(qlonglong id)
{
    onWindowActivated(static_cast<quint64>(id));
}

QString TaskTracker::activeAppId() const { return m_activeAppId; }

// ─────────────────────────────────────────────────────────────────────────────
// refresh() — three strategies, each logged clearly so we can diagnose failures
// ─────────────────────────────────────────────────────────────────────────────
void TaskTracker::refresh()
{
    if (!m_kwin->isValid()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qDebug("kdock [tasktracker]: KWin interface not valid — cannot track windows  error='%s'",
                   qPrintable(m_kwin->lastError().message()));
        }
        return;
    }

    static int refreshCount = 0;
    const bool verbose = (++refreshCount <= 5);

    QMap<QString, int>    newCounts;
    QMap<quint64, QString> newWindowAppIds;
    QSet<QString>         newUrgent;
    bool                  gotAnyData = false;

    // ── Strategy A: getWindowInfo() no-args (KWin 5 / some KWin 6 builds) ──
    {
        const QDBusMessage raw = m_kwin->call(QStringLiteral("getWindowInfo"));
        if (verbose)
            qDebug("kdock [tasktracker]: [A] getWindowInfo() → type=%d  error='%s'  args=%d",
                   (int)raw.type(), qPrintable(raw.errorName()),
                   (int)raw.arguments().size());

        if (raw.type() == QDBusMessage::ReplyMessage && !raw.arguments().isEmpty()) {
            const QVariantList windows = extractList(raw.arguments().first());
            if (verbose)
                qDebug("kdock [tasktracker]: [A] window list size=%d", (int)windows.size());
            for (const QVariant &v : windows) {
                const QVariantMap info = extractMap(v);
                processWindowInfo(info, newWindowAppIds, newCounts, newUrgent, verbose);
            }
            gotAnyData = true;
        }
    }

    // ── Strategy B: clientList() → getWindowInfo(id) per window ──────────────
    if (!gotAnyData) {
        const QDBusMessage clRaw = m_kwin->call(QStringLiteral("clientList"));
        if (verbose)
            qDebug("kdock [tasktracker]: [B] clientList() → type=%d  error='%s'  args=%d",
                   (int)clRaw.type(), qPrintable(clRaw.errorName()),
                   (int)clRaw.arguments().size());

        if (clRaw.type() == QDBusMessage::ReplyMessage && !clRaw.arguments().isEmpty()) {
            const QVariant &a0 = clRaw.arguments().first();
            if (verbose)
                qDebug("kdock [tasktracker]: [B] clientList arg0 typeName='%s'  toString='%s'",
                       a0.typeName(), qPrintable(a0.toString().left(200)));

            // Extract as list — IDs may be uint or qlonglong
            const QVariantList ids = extractList(a0);
            if (verbose)
                qDebug("kdock [tasktracker]: [B] clientList returned %d IDs", (int)ids.size());

            for (const QVariant &idVar : ids) {
                const quint64 wid = idVar.toULongLong();
                // Try getWindowInfo with uint first, then qlonglong
                QDBusMessage wiRaw = m_kwin->call(QStringLiteral("getWindowInfo"),
                                                   static_cast<uint>(wid));
                if (wiRaw.type() != QDBusMessage::ReplyMessage)
                    wiRaw = m_kwin->call(QStringLiteral("getWindowInfo"),
                                          static_cast<qlonglong>(wid));

                if (wiRaw.type() == QDBusMessage::ReplyMessage && !wiRaw.arguments().isEmpty()) {
                    const QVariantMap info = extractMap(wiRaw.arguments().first());
                    processWindowInfo(info, newWindowAppIds, newCounts, newUrgent, verbose);
                    gotAnyData = true;
                } else if (verbose) {
                    qDebug("kdock [tasktracker]: [B] getWindowInfo(%llu) → error='%s'",
                           (unsigned long long)wid, qPrintable(wiRaw.errorName()));
                }
            }
        }
    }

    // ── Strategy C: queryWindowInfo (gets active window — limited but reveals format) ─
    if (!gotAnyData && verbose) {
        const QDBusMessage qwRaw = m_kwin->call(QStringLiteral("queryWindowInfo"));
        qDebug("kdock [tasktracker]: [C] queryWindowInfo() → type=%d  error='%s'  args=%d",
               (int)qwRaw.type(), qPrintable(qwRaw.errorName()),
               (int)qwRaw.arguments().size());
        if (qwRaw.type() == QDBusMessage::ReplyMessage && !qwRaw.arguments().isEmpty()) {
            const QVariantMap info = extractMap(qwRaw.arguments().first());
            qDebug("kdock [tasktracker]: [C] queryWindowInfo keys: [%s]",
                   qPrintable(info.keys().join(QStringLiteral(", "))));
            for (auto it = info.cbegin(); it != info.cend(); ++it)
                qDebug("kdock [tasktracker]:   %s = %s",
                       qPrintable(it.key()), qPrintable(it.value().toString()));
        }
    }

    if (verbose)
        qDebug("kdock [tasktracker]: refresh #%d done — trackedWindows=%d  gotAnyData=%s",
               refreshCount, (int)newWindowAppIds.size(), gotAnyData ? "true" : "false");

    // ── Emit state changes ────────────────────────────────────────────────────
    for (const QString &id : newUrgent)
        if (!m_urgentApps.contains(id)) emit windowUrgent(id);
    for (const QString &id : std::as_const(m_urgentApps))
        if (!newUrgent.contains(id)) emit windowNotUrgent(id);
    m_urgentApps = newUrgent;

    m_windowAppIds = newWindowAppIds;

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

void TaskTracker::processWindowInfo(const QVariantMap &info,
                                    QMap<quint64, QString> &windowAppIds,
                                    QMap<QString, int> &counts,
                                    QSet<QString> &urgent,
                                    bool verbose)
{
    if (info.isEmpty()) return;

    const quint64 wid   = info.value(QStringLiteral("id")).toULongLong();
    const QString cls   = info.value(QStringLiteral("resourceClass")).toString();
    const QString name  = info.value(QStringLiteral("resourceName")).toString();
    const QString appId = windowToAppId(info);

    if (verbose)
        qDebug("kdock [tasktracker]:   window wid=%llu  resourceClass='%s'  resourceName='%s'  → appId='%s'",
               (unsigned long long)wid, qPrintable(cls), qPrintable(name), qPrintable(appId));

    if (!appId.isEmpty()) {
        windowAppIds[wid] = appId;
        counts[appId]++;
        if (info.value(QStringLiteral("demandsAttention")).toBool())
            urgent.insert(appId);
    }
}

QString TaskTracker::windowToAppId(const QVariantMap &info) const
{
    QString cls = info.value(QStringLiteral("resourceClass")).toString();
    if (cls.isEmpty())
        cls = info.value(QStringLiteral("resourceName")).toString();
    return normaliseAppId(cls);
}

QStringList TaskTracker::runningApps() const { return m_runningApps; }

void TaskTracker::closeWindows(const QString &appId)
{
    for (auto it = m_windowAppIds.cbegin(); it != m_windowAppIds.cend(); ++it) {
        if (it.value() == appId || shortName(it.value()) == shortName(appId))
            m_kwin->call(QStringLiteral("closeWindow"), static_cast<qlonglong>(it.key()));
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
        qDebug("kdock [activate]: no windows found for appId='%s' (shortName='%s')  totalTracked=%d",
               qPrintable(appId), qPrintable(shortName(appId)), (int)m_windowAppIds.size());
        if (!m_windowAppIds.isEmpty()) {
            QStringList all;
            for (auto it = m_windowAppIds.cbegin(); it != m_windowAppIds.cend(); ++it)
                all << QStringLiteral("wid=%1→'%2'").arg(it.key()).arg(it.value());
            qDebug("kdock [activate]: tracked: [%s]", qPrintable(all.join(QStringLiteral(", "))));
        }
        return;
    }

    int &idx = m_windowCycleIndex[appId];
    if (idx >= windows.size()) idx = 0;
    const int current = idx;
    idx = (idx + 1) % windows.size();
    const quint64 wid = windows.at(current);

    qDebug("kdock [activate]: wid=%llu (window %d/%d) for appId='%s'",
           (unsigned long long)wid, current + 1, (int)windows.size(), qPrintable(appId));

    const QDBusMessage reply = m_kwin->call(QStringLiteral("activateWindow"),
                                             static_cast<qlonglong>(wid));
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("kdock [activate]: KWin DBus error: %s", qPrintable(reply.errorMessage()));
}
