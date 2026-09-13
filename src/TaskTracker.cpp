// TaskTracker — KDE Plasma window tracking and control via KWin's own
// JavaScript scripting engine. See TaskTracker.h for why this approach was
// chosen over the two DBus/Wayland-protocol approaches that were tried and
// ruled out first.
//
// Architecture:
//   1. A persistent KWin script is loaded once. It enumerates existing
//      windows via workspace.windowList(), then tracks live changes via
//      workspace.windowAdded/windowRemoved/windowActivated and each window's
//      demandsAttentionChanged signal. Every event is relayed back to us via
//      callDBus() into KWinBridge, registered at org.kde.kdock /WindowTracker.
//   2. Each window is identified by its internalId (a UUID string), which is
//      stable for the window's lifetime and shared between the persistent
//      script's reports and the one-shot action scripts below.
//   3. activateWindow()/closeWindows() do not reach into KWin via any DBus
//      object — none exists for this — they instead generate a tiny JS
//      snippet that re-finds the target window(s) by internalId from
//      workspace.windowList() and calls workspace.activeWindow = window /
//      window.closeWindow() directly inside KWin's own process, then the
//      snippet is loaded, started, unloaded and deleted (fire-and-forget;
//      no inbound RPC into the running script is needed since execution is
//      synchronous).

#include "TaskTracker.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QFile>
#include <QStandardPaths>

// Extract the last dotted component as lowercase: "org.kde.konsole" → "konsole"
static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

// KWin script injected into the compositor. Runs in KWin's JS engine for the
// lifetime of the dock. Reports every window add/remove/activate/urgency
// change back to our process via callDBus().
static const char kWinScript[] = R"js(
(function() {
    var svc   = 'org.kde.kdock';
    var path  = '/WindowTracker';
    var iface = 'org.kde.kdock.WindowTracker';

    // Substituted by TaskTracker::setupKWinScript(): the screen rectangle the
    // dock occupies, or null when dodge mode isn't in use and no obstruction
    // tracking is wanted.
    var dockRect = __DOCK_RECT__;
    var lastObstructed = null;

    function reportUrgent(w) {
        callDBus(svc, path, iface, 'reportWindowUrgent', String(w.internalId), !!w.demandsAttention);
    }

    function reportAdded(w) {
        if (!w) return;
        var df = (w.desktopFileName !== undefined) ? w.desktopFileName : '';
        if (df === '') df = (w.resourceClass !== undefined) ? w.resourceClass : '';
        if (df === '') return;
        callDBus(svc, path, iface, 'reportWindowAdded', String(w.internalId), df);

        if (w.demandsAttentionChanged)
            w.demandsAttentionChanged.connect(function() { reportUrgent(w); });
        if (w.demandsAttention)
            reportUrgent(w);
    }

    // Enumerate windows already open when kdock starts
    var wins = workspace.windowList ? workspace.windowList() : [];
    for (var i = 0; i < wins.length; i++) { reportAdded(wins[i]); }

    workspace.windowAdded.connect(reportAdded);

    workspace.windowRemoved.connect(function(w) {
        if (!w) return;
        callDBus(svc, path, iface, 'reportWindowRemoved', String(w.internalId));
        recomputeObstruction();
    });

    workspace.windowActivated.connect(function(w) {
        callDBus(svc, path, iface, 'reportWindowActivated', w ? String(w.internalId) : '');
        recomputeObstruction();
    });

    // ── Dock obstruction, for dodge mode ──────────────────────────────────
    // Reports a single boolean: does any window the user can currently see
    // overlap the rectangle the dock occupies? That is what lets the dock get
    // out of the way only when something is actually in its way, rather than
    // hiding on a timer or guessing from the application name.
    //
    // Only the transition is reported, not every geometry change, so dragging
    // a window around costs at most one DBus call each way.
    function onCurrentDesktop(w) {
        if (w.onAllDesktops) return true;
        if (!w.desktops || !workspace.currentDesktop) return true;
        for (var d = 0; d < w.desktops.length; d++)
            if (w.desktops[d] === workspace.currentDesktop) return true;
        return false;
    }

    function coversDock(w) {
        if (!w || !dockRect) return false;
        // Skip anything that isn't a real, visible application window: our own
        // layer surface, the desktop, panels, and minimised windows.
        if (w.minimized || w.skipTaskbar || w.desktopWindow) return false;
        if (w.normalWindow === false) return false;
        if (!onCurrentDesktop(w)) return false;
        var g = w.frameGeometry;
        if (!g) return false;
        return g.x < dockRect.x + dockRect.w && g.x + g.width  > dockRect.x
            && g.y < dockRect.y + dockRect.h && g.y + g.height > dockRect.y;
    }

    function recomputeObstruction() {
        if (!dockRect) return;
        var list = workspace.windowList ? workspace.windowList() : [];
        var hit = false;
        for (var k = 0; k < list.length; k++) {
            if (coversDock(list[k])) { hit = true; break; }
        }
        if (hit !== lastObstructed) {
            lastObstructed = hit;
            callDBus(svc, path, iface, 'reportDockObstructed', hit);
        }
    }

    function watchGeometry(w) {
        if (!w || !dockRect) return;
        if (w.frameGeometryChanged) w.frameGeometryChanged.connect(recomputeObstruction);
        if (w.minimizedChanged)     w.minimizedChanged.connect(recomputeObstruction);
        if (w.desktopsChanged)      w.desktopsChanged.connect(recomputeObstruction);
        if (w.fullScreenChanged)    w.fullScreenChanged.connect(recomputeObstruction);
    }

    for (var j = 0; j < wins.length; j++) { watchGeometry(wins[j]); }
    workspace.windowAdded.connect(function(w) { watchGeometry(w); recomputeObstruction(); });
    if (workspace.currentDesktopChanged)
        workspace.currentDesktopChanged.connect(recomputeObstruction);
    recomputeObstruction();
})();
)js";

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
{
    m_scripting = new QDBusInterface(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"),
        QDBusConnection::sessionBus(),
        this);

    qDebug("kdock [tasktracker]: org.kde.kwin.Scripting valid=%s",
           m_scripting->isValid() ? "true" : "false");

    setupKWinScript();

    // Safety-net poll: if the script hasn't delivered any windows after 3 s,
    // retry setup. After that, back off to every 10 s.
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::poll);
    m_pollTimer.start();
}

TaskTracker::~TaskTracker() = default;

void TaskTracker::setDockRect(const QRect &rect)
{
    if (m_dockRect == rect) return;
    m_dockRect = rect;

    // The old script is watching the old rectangle; replace it wholesale.
    if (m_dockRect.isEmpty() && m_dockObstructed) {
        m_dockObstructed = false;
        emit dockObstructionChanged();
    }
    setupKWinScript();
}

void TaskTracker::onDockObstructedChanged(bool obstructed)
{
    if (m_dockObstructed == obstructed) return;
    m_dockObstructed = obstructed;
    qDebug("kdock [tasktracker]: dock %s", obstructed ? "obstructed by a window"
                                                      : "clear of windows");
    emit dockObstructionChanged();
}

// ── Persistent tracking script setup ──────────────────────────────────────────
void TaskTracker::setupKWinScript()
{
    if (!m_bridge) {
        m_bridge = new KWinBridge(this);
        connect(m_bridge, &KWinBridge::windowAdded,        this, &TaskTracker::onWindowAdded);
        connect(m_bridge, &KWinBridge::windowRemoved,      this, &TaskTracker::onWindowRemoved);
        connect(m_bridge, &KWinBridge::windowActivated,    this, &TaskTracker::onWindowActivated);
        connect(m_bridge, &KWinBridge::windowUrgentChanged, this, &TaskTracker::onWindowUrgentChanged);
        connect(m_bridge, &KWinBridge::dockObstructedChanged, this, &TaskTracker::onDockObstructedChanged);
    }

    // Register once. setupKWinScript() also runs whenever the dodge rectangle
    // changes, and re-registering an already-registered name just fails noisily.
    if (!m_bridgeRegistered) {
        const bool svcOk = QDBusConnection::sessionBus()
                               .registerService(QStringLiteral("org.kde.kdock"));
        const bool objOk = QDBusConnection::sessionBus()
                               .registerObject(QStringLiteral("/WindowTracker"), m_bridge,
                                               QDBusConnection::ExportScriptableSlots);
        qDebug("kdock [tasktracker]: DBus bridge — service=%s  object=%s",
               svcOk ? "OK" : "FAILED (may already be registered)",
               objOk ? "OK" : "FAILED");
        m_bridgeRegistered = svcOk && objOk;
    }

    const QString scriptPath = QStringLiteral("/tmp/kdock_tracker.js");
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("kdock [tasktracker]: cannot write KWin script to '%s'", qPrintable(scriptPath));
        return;
    }
    // Bake the watched rectangle into the script: KWin scripts have no inbound
    // RPC, so a changed dock geometry means rewriting and reloading the script.
    const QString rectLiteral = m_dockRect.isEmpty()
        ? QStringLiteral("null")
        : QStringLiteral("{x:%1,y:%2,w:%3,h:%4}")
              .arg(m_dockRect.x()).arg(m_dockRect.y())
              .arg(m_dockRect.width()).arg(m_dockRect.height());

    QString script = QString::fromLatin1(kWinScript);
    script.replace(QStringLiteral("__DOCK_RECT__"), rectLiteral);

    f.write(script.toUtf8());
    f.close();

    if (!m_scripting->isValid())
        return;

    // Unload any previous instance (ignore errors)
    m_scripting->call(QStringLiteral("unloadScript"), QStringLiteral("kdock-tracker"));

    const QDBusMessage loadReply = m_scripting->call(
        QStringLiteral("loadScript"), scriptPath, QStringLiteral("kdock-tracker"));

    qDebug("kdock [tasktracker]: loadScript → type=%d  error='%s'",
           (int)loadReply.type(), qPrintable(loadReply.errorName()));

    m_scriptLoaded = (loadReply.type() == QDBusMessage::ReplyMessage);

    if (m_scriptLoaded) {
        // KWin 6 requires start() after loadScript to actually execute loaded
        // scripts. start() only runs scripts that haven't been started yet,
        // so calling it again later (from runKWinSnippet) does not re-run
        // this persistent script.
        const QDBusMessage startReply = m_scripting->call(QStringLiteral("start"));
        qDebug("kdock [tasktracker]: start() → type=%d  error='%s'",
               (int)startReply.type(), qPrintable(startReply.errorName()));
    }
}

// ── Poll: retry script if nothing was tracked yet ────────────────────────────
void TaskTracker::poll()
{
    if (m_windowAppIds.isEmpty()) {
        qDebug("kdock [tasktracker]: poll — no windows tracked yet (scriptLoaded=%s) — retrying",
               m_scriptLoaded ? "true" : "false");
        setupKWinScript();
        m_pollTimer.setInterval(10000);
    }
}

// ── One-shot action scripts ───────────────────────────────────────────────────
void TaskTracker::runKWinSnippet(const QString &jsBody)
{
    if (!m_scripting || !m_scripting->isValid()) {
        qWarning("kdock [snippet]: org.kde.kwin.Scripting not available");
        return;
    }

    const QString name = QStringLiteral("kdock-action-%1").arg(++m_actionCounter);
    const QString scriptPath = QStringLiteral("/tmp/%1.js").arg(name);

    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("kdock [snippet]: cannot write '%s'", qPrintable(scriptPath));
        return;
    }
    f.write(jsBody.toUtf8());
    f.close();

    const QDBusMessage loadReply = m_scripting->call(QStringLiteral("loadScript"), scriptPath, name);
    qDebug("kdock [snippet]: loadScript('%s') → type=%d  error='%s'",
           qPrintable(name), (int)loadReply.type(), qPrintable(loadReply.errorName()));

    const QDBusMessage startReply = m_scripting->call(QStringLiteral("start"));
    qDebug("kdock [snippet]: start() → type=%d  error='%s'",
           (int)startReply.type(), qPrintable(startReply.errorName()));

    m_scripting->call(QStringLiteral("unloadScript"), name);
    QFile::remove(scriptPath);
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

void TaskTracker::onWindowUrgentChanged(const QString &uuid, bool urgent)
{
    const QString appId = m_windowAppIds.value(uuid);
    if (appId.isEmpty())
        return;

    auto appIsUrgent = [this](const QString &id) {
        for (const QString &winUuid : windowsForApp(id))
            if (m_urgentApps.contains(winUuid))
                return true;
        return false;
    };

    const bool wasUrgent = appIsUrgent(appId);

    if (urgent)
        m_urgentApps.insert(uuid);
    else
        m_urgentApps.remove(uuid);

    const bool isUrgent = appIsUrgent(appId);

    qDebug("kdock [tasktracker]: windowUrgentChanged uuid='%s' urgent=%s → appId='%s' isUrgent=%s",
           qPrintable(uuid), urgent ? "true" : "false",
           qPrintable(appId), isUrgent ? "true" : "false");

    if (isUrgent && !wasUrgent)
        emit windowUrgent(appId);
    else if (!isUrgent && wasUrgent)
        emit windowNotUrgent(appId);
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

    if (m_urgentApps.contains(uuid))
        onWindowUrgentChanged(uuid, false);

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

// ── Window actions ────────────────────────────────────────────────────────────
// Both actions re-find the target window(s) inside KWin's own JS engine by
// internalId — there is no per-window DBus object to call into directly.
void TaskTracker::activateWindow(const QString &appId)
{
    const QStringList uuids = windowsForApp(appId);
    if (uuids.isEmpty()) {
        qDebug("kdock [activate]: no windows for appId='%s'  tracked=%d",
               qPrintable(appId), (int)m_windowAppIds.size());
        return;
    }

    int &idx = m_windowCycleIdx[appId];
    if (idx >= uuids.size())
        idx = 0;
    const QString uuid = uuids.at(idx);
    idx = (idx + 1) % uuids.size();

    qDebug("kdock [activate]: uuid='%s' (%d/%d) for appId='%s'",
           qPrintable(uuid), idx, (int)uuids.size(), qPrintable(appId));

    const QString js = QStringLiteral(
        "(function() {"
        "  var wins = workspace.windowList();"
        "  for (var i = 0; i < wins.length; i++) {"
        "    if (String(wins[i].internalId) === '%1') {"
        "      workspace.activeWindow = wins[i];"
        "      break;"
        "    }"
        "  }"
        "})();").arg(uuid);

    runKWinSnippet(js);
}

void TaskTracker::closeWindows(const QString &appId)
{
    const QStringList uuids = windowsForApp(appId);
    qDebug("kdock [close]: closing %d window(s) for appId='%s'",
           (int)uuids.size(), qPrintable(appId));
    if (uuids.isEmpty())
        return;

    QStringList quoted;
    for (const QString &uuid : uuids)
        quoted << QStringLiteral("'%1'").arg(uuid);

    const QString js = QStringLiteral(
        "(function() {"
        "  var ids = [%1];"
        "  var wins = workspace.windowList();"
        "  for (var i = 0; i < wins.length; i++) {"
        "    if (ids.indexOf(String(wins[i].internalId)) !== -1) {"
        "      wins[i].closeWindow();"
        "    }"
        "  }"
        "})();").arg(quoted.join(QStringLiteral(",")));

    runKWinSnippet(js);
}

void TaskTracker::minimizeWindow(const QString &appId)
{
    const QStringList uuids = windowsForApp(appId);
    qDebug("kdock [minimize]: minimizing %d window(s) for appId='%s'",
           (int)uuids.size(), qPrintable(appId));
    if (uuids.isEmpty())
        return;

    QStringList quoted;
    for (const QString &uuid : uuids)
        quoted << QStringLiteral("'%1'").arg(uuid);

    const QString js = QStringLiteral(
        "(function() {"
        "  var ids = [%1];"
        "  var wins = workspace.windowList();"
        "  for (var i = 0; i < wins.length; i++) {"
        "    if (ids.indexOf(String(wins[i].internalId)) !== -1) {"
        "      wins[i].minimized = true;"
        "    }"
        "  }"
        "})();").arg(quoted.join(QStringLiteral(",")));

    runKWinSnippet(js);
}
