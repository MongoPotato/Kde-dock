// TaskTracker — KWin 6 Wayland window tracking via KWin scripting.
//
// Architecture:
//   1. We register a DBus object (KWinBridge) at org.kde.kdock /WindowTracker.
//   2. We write a small JavaScript file and ask KWin to load it as a script.
//   3. The script runs inside KWin's JS engine, calls workspace.windowList()
//      for existing windows, and connects to workspace.windowAdded/Removed/
//      Activated for live tracking. It reports each event back via callDBus().
//   4. For window activation we call activate() on the per-window DBus object
//      that KWin registers at /org/kde/KWin/Windows/{uuid}.

#include "TaskTracker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

// Extract the last dotted component as lowercase: "org.kde.konsole" → "konsole"
static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

// DBus object path segments may only contain [A-Za-z0-9_], but internalId is a
// QUuid string like "{e5ffd913-...}". KWin registers per-window objects using
// the braces/hyphens stripped (QUuid::Id128 form), so strip them before
// building the path.
static QString dbusPathId(const QString &uuid)
{
    QString id = uuid;
    id.remove(QLatin1Char('{'));
    id.remove(QLatin1Char('}'));
    id.remove(QLatin1Char('-'));
    return id;
}

// KWin script injected into the compositor. Runs in KWin's JS engine.
// It calls back to our process via callDBus whenever window state changes.
static const char kWinScript[] = R"js(
(function() {
    var svc   = 'org.kde.kdock';
    var path  = '/WindowTracker';
    var iface = 'org.kde.kdock.WindowTracker';

    print('kdock-tracker: script starting, callDBus type=' + typeof callDBus);

    function reportAdded(w) {
        if (!w) return;
        var df = (w.desktopFileName !== undefined) ? w.desktopFileName : '';
        if (df === '') df = (w.resourceClass !== undefined) ? w.resourceClass : '';
        print('kdock-tracker: reportAdded id=' + String(w.internalId) + ' df=' + df);
        if (df === '') return;
        callDBus(svc, path, iface, 'reportWindowAdded', String(w.internalId), df);
    }

    // Enumerate windows already open when kdock starts
    var wins = workspace.windowList ? workspace.windowList() : [];
    print('kdock-tracker: existing windows=' + wins.length);
    for (var i = 0; i < wins.length; i++) { reportAdded(wins[i]); }

    workspace.windowAdded.connect(reportAdded);

    workspace.windowRemoved.connect(function(w) {
        if (!w) return;
        print('kdock-tracker: windowRemoved id=' + String(w.internalId));
        callDBus(svc, path, iface, 'reportWindowRemoved', String(w.internalId));
    });

    workspace.windowActivated.connect(function(w) {
        print('kdock-tracker: windowActivated id=' + (w ? String(w.internalId) : 'null'));
        callDBus(svc, path, iface, 'reportWindowActivated', w ? String(w.internalId) : '');
    });

    print('kdock-tracker: script ready, signals connected');
})();
)js";

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

    // One-time full introspection dump of /KWin: lists every method AND every
    // child object node, which tells us definitively whether per-window
    // objects exist and what they're actually named.
    {
        const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            qDebug("kdock [probe]: /KWin Introspect XML:\n%s",
                   qPrintable(reply.arguments().first().toString()));
        } else {
            qDebug("kdock [probe]: /KWin Introspect failed: %s",
                   qPrintable(reply.errorMessage()));
        }
    }

    setupKWinScript();

    // Safety-net poll: if the script hasn't delivered any windows after 3 s,
    // retry setup. After that, back off to every 10 s.
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::poll);
    m_pollTimer.start();
}

TaskTracker::~TaskTracker() = default;

// ── Script setup ──────────────────────────────────────────────────────────────
void TaskTracker::setupKWinScript()
{
    // ── Step 1: register bridge on session bus ────────────────────────────────
    if (!m_bridge) {
        m_bridge = new KWinBridge(this);
        connect(m_bridge, &KWinBridge::windowAdded,     this, &TaskTracker::onWindowAdded);
        connect(m_bridge, &KWinBridge::windowRemoved,   this, &TaskTracker::onWindowRemoved);
        connect(m_bridge, &KWinBridge::windowActivated, this, &TaskTracker::onWindowActivated);
    }

    const bool svcOk = QDBusConnection::sessionBus()
                           .registerService(QStringLiteral("org.kde.kdock"));
    const bool objOk = QDBusConnection::sessionBus()
                           .registerObject(QStringLiteral("/WindowTracker"), m_bridge,
                                           QDBusConnection::ExportScriptableSlots);
    qDebug("kdock [tasktracker]: DBus bridge — service=%s  object=%s",
           svcOk ? "OK" : "FAILED (may already be registered)",
           objOk ? "OK" : "FAILED");

    // ── Step 2: write the JS file ─────────────────────────────────────────────
    const QString scriptPath = QStringLiteral("/tmp/kdock_tracker.js");
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("kdock [tasktracker]: cannot write KWin script to '%s'", qPrintable(scriptPath));
        return;
    }
    f.write(kWinScript);
    f.close();
    qDebug("kdock [tasktracker]: KWin script written to '%s'", qPrintable(scriptPath));

    // ── Step 3: load the script via KWin scripting interface ──────────────────
    QDBusInterface scripting(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"),
        QDBusConnection::sessionBus());

    qDebug("kdock [tasktracker]: org.kde.kwin.Scripting valid=%s",
           scripting.isValid() ? "true" : "false");

    // Unload any previous instance (ignore errors)
    scripting.call(QStringLiteral("unloadScript"), QStringLiteral("kdock-tracker"));

    const QDBusMessage loadReply = scripting.call(
        QStringLiteral("loadScript"), scriptPath, QStringLiteral("kdock-tracker"));

    qDebug("kdock [tasktracker]: loadScript → type=%d  error='%s'  result='%s'",
           (int)loadReply.type(),
           qPrintable(loadReply.errorName()),
           loadReply.arguments().isEmpty()
               ? "(none)" : qPrintable(loadReply.arguments().first().toString()));

    m_scriptLoaded = (loadReply.type() == QDBusMessage::ReplyMessage);

    if (m_scriptLoaded) {
        // KWin 6 requires start() after loadScript to actually execute loaded scripts.
        const QDBusMessage startReply = scripting.call(QStringLiteral("start"));
        qDebug("kdock [tasktracker]: start() → type=%d  error='%s'",
               (int)startReply.type(),
               qPrintable(startReply.errorName()));
    }
}

// ── Poll: retry script if nothing was tracked yet ────────────────────────────
void TaskTracker::poll()
{
    if (m_windowAppIds.isEmpty()) {
        qDebug("kdock [tasktracker]: poll — no windows tracked yet (scriptLoaded=%s) — retrying",
               m_scriptLoaded ? "true" : "false");
        setupKWinScript();
        // Back off to 10 s after the first retry
        m_pollTimer.setInterval(10000);
    }
}

// ── Bridge callbacks ──────────────────────────────────────────────────────────
void TaskTracker::onWindowAdded(const QString &uuid, const QString &desktopFile)
{
    qDebug("kdock [tasktracker]: windowAdded uuid='%s'  desktopFile='%s'",
           qPrintable(uuid), qPrintable(desktopFile));
    addWindow(uuid, desktopFile);
}

void TaskTracker::onWindowRemoved(const QString &uuid)
{
    qDebug("kdock [tasktracker]: windowRemoved uuid='%s'", qPrintable(uuid));
    removeWindow(uuid);
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

// ── Window add / remove ───────────────────────────────────────────────────────
void TaskTracker::addWindow(const QString &uuid, const QString &desktopFile)
{
    if (m_windowAppIds.contains(uuid))
        return;

    // desktopFile is already the canonical app ID (e.g. "org.kde.konsole")
    QString appId = desktopFile.toLower();
    if (appId.isEmpty())
        return;

    // Don't track our own dock surface as a running app.
    if (shortName(appId) == QStringLiteral("kdock"))
        return;

    // Verify against the .desktop file system; fall back to short name if needed
    if (!appId.isEmpty()) {
        const QStringList dataDirs = QStandardPaths::standardLocations(
            QStandardPaths::ApplicationsLocation);
        bool found = false;
        for (const QString &dir : dataDirs) {
            if (QFile::exists(dir + '/' + appId + QStringLiteral(".desktop")))
                { found = true; break; }
        }
        if (!found) {
            // Try with org.kde. prefix for bare names like "konsole"
            for (const QString &dir : dataDirs) {
                const QString kdeid = QStringLiteral("org.kde.") + appId;
                if (QFile::exists(dir + '/' + kdeid + QStringLiteral(".desktop")))
                    { appId = kdeid; found = true; break; }
            }
        }
    }

    m_windowAppIds[uuid] = appId;
    m_windowCounts[appId] = m_windowCounts.value(appId, 0) + 1;
    emit windowCountChanged(appId, m_windowCounts[appId]);
    rebuildRunningApps();
}

void TaskTracker::removeWindow(const QString &uuid)
{
    const QString appId = m_windowAppIds.value(uuid);
    if (appId.isEmpty())
        return;

    m_windowAppIds.remove(uuid);
    const int n = m_windowCounts.value(appId, 1) - 1;
    if (n <= 0) {
        m_windowCounts.remove(appId);
        emit windowCountChanged(appId, 0);
    } else {
        m_windowCounts[appId] = n;
        emit windowCountChanged(appId, n);
    }
    rebuildRunningApps();
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

// ── Window lookup (fuzzy short-name match) ────────────────────────────────────
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

// Dump getWindowInfo(uuid) on /KWin so we can see what fields KWin actually
// reports for a window — used to hunt for the correct per-window DBus path.
static void dumpWindowInfo(QDBusInterface *kwin, const QString &uuid)
{
    if (!kwin) return;
    const QDBusMessage reply = kwin->call(QStringLiteral("getWindowInfo"), uuid);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qDebug("kdock [probe]: getWindowInfo('%s') → type=%d error='%s'",
               qPrintable(uuid), (int)reply.type(), qPrintable(reply.errorMessage()));
        return;
    }
    QVariantMap map;
    const QVariant v = reply.arguments().first();
    if (v.canConvert<QDBusArgument>())
        v.value<QDBusArgument>() >> map;
    else
        map = v.toMap();

    qDebug("kdock [probe]: getWindowInfo('%s') → %d keys", qPrintable(uuid), (int)map.size());
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        qDebug("kdock [probe]:   %s = %s", qPrintable(it.key()), qPrintable(it.value().toString()));
}

// Try several plausible (service, path, interface) combinations for the
// per-window object and report which ones respond to Introspect. Returns the
// first path that answers, or an empty string if none do.
static QString probeWindowObjectPath(const QString &uuidBraced, const QString &idHex)
{
    const QStringList candidates = {
        QStringLiteral("/org/kde/KWin/Windows/") + idHex,
        QStringLiteral("/org/kde/KWin/Window/") + idHex,
        QStringLiteral("/KWin/Windows/") + idHex,
        QStringLiteral("/org/kde/KWin/Windows/") + uuidBraced,
        QStringLiteral("/windows/") + idHex,
    };

    for (const QString &path : candidates) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), path,
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
        const bool ok = (reply.type() == QDBusMessage::ReplyMessage);
        qDebug("kdock [probe]: Introspect '%s' → %s%s",
               qPrintable(path), ok ? "OK" : "FAIL",
               ok ? "" : qPrintable(QStringLiteral(" (") + reply.errorMessage() + QStringLiteral(")")));
        if (ok)
            return path;
    }
    return {};
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

    dumpWindowInfo(m_kwin, uuid);
    const QString foundPath = probeWindowObjectPath(uuid, dbusPathId(uuid));

    if (foundPath.isEmpty()) {
        qWarning("kdock [activate]: no per-window DBus object found for uuid='%s' — "
                  "see kdock [probe] lines above for what KWin actually exposes",
                  qPrintable(uuid));
        return;
    }

    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), foundPath,
        QStringLiteral("org.kde.KWin.Window"),
        QStringLiteral("activate"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("kdock [activate]: error for uuid='%s' path='%s': %s",
                 qPrintable(uuid), qPrintable(foundPath), qPrintable(reply.errorMessage()));
    else
        qDebug("kdock [activate]: success — uuid='%s' path='%s'",
               qPrintable(uuid), qPrintable(foundPath));
}

void TaskTracker::closeWindows(const QString &appId)
{
    for (const QString &uuid : windowsForApp(appId)) {
        const QString foundPath = probeWindowObjectPath(uuid, dbusPathId(uuid));
        if (foundPath.isEmpty()) {
            qWarning("kdock [close]: no per-window DBus object found for uuid='%s'",
                      qPrintable(uuid));
            continue;
        }
        const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), foundPath,
            QStringLiteral("org.kde.KWin.Window"),
            QStringLiteral("close"));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
        if (reply.type() == QDBusMessage::ErrorMessage)
            qWarning("kdock [close]: error for uuid='%s' path='%s': %s",
                     qPrintable(uuid), qPrintable(foundPath), qPrintable(reply.errorMessage()));
    }
}
