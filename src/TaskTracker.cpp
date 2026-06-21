// TaskTracker — KDE Plasma window tracking and control via the
// plasma-window-management Wayland protocol.
//
// Architecture:
//   1. detectWayland() binds org_kde_plasma_window_management from the
//      Wayland registry, sharing Qt's own wl_display connection (same
//      pattern as LayerShellWindow) so events are dispatched automatically
//      by Qt's existing event loop.
//   2. The management global sends window_with_uuid(id, uuid) for every
//      mapped window; get_window_by_uuid(uuid) binds a per-window
//      org_kde_plasma_window object.
//   3. Each window object reports its app_id and state (bitmask, includes
//      active/demands_attention/etc.) via events, and unmapped when closed.
//   4. activateWindow()/closeWindows() act directly on the bound window
//      object via set_state()/close() requests — no DBus round-trip.

#include "TaskTracker.h"

#include "qwayland-plasma-window-management.h"

#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

#include <cstring>

// Extract the last dotted component as lowercase: "org.kde.konsole" → "konsole"
static QString shortName(const QString &appId)
{
    const int dot = appId.lastIndexOf('.');
    return dot >= 0 ? appId.mid(dot + 1).toLower() : appId.toLower();
}

// ── Per-window Wayland object ─────────────────────────────────────────────────
// Wraps a single org_kde_plasma_window proxy. Created when the management
// global reports a newly-mapped window, destroyed when it reports unmapped.
class TaskTracker::PlasmaWindow : public QtWayland::org_kde_plasma_window {
public:
    PlasmaWindow(TaskTracker *tracker, const QString &uuid, ::org_kde_plasma_window *object)
        : QtWayland::org_kde_plasma_window(object)
        , m_tracker(tracker)
        , m_uuid(uuid)
    {}

    ~PlasmaWindow() override
    {
        // The protocol object is not auto-released on unmapped; we must
        // explicitly destroy() it once. Guard against the unmapped handler
        // having already done so.
        if (object())
            destroy();
    }

    QString  uuid() const { return m_uuid; }
    QString  appId() const { return m_appId; }
    uint32_t state() const { return m_state; }

protected:
    void org_kde_plasma_window_app_id_changed(const QString &app_id) override
    {
        qDebug("kdock [win]: uuid='%s' app_id_changed → '%s'", qPrintable(m_uuid), qPrintable(app_id));
        m_appId = app_id;
        m_tracker->onWindowAppIdChanged(this);
    }

    void org_kde_plasma_window_state_changed(uint32_t flags) override
    {
        qDebug("kdock [win]: uuid='%s' appId='%s' state_changed → 0x%x",
               qPrintable(m_uuid), qPrintable(m_appId), flags);
        m_state = flags;
        m_tracker->onWindowStateChanged(this);
    }

    void org_kde_plasma_window_unmapped() override
    {
        qDebug("kdock [win]: uuid='%s' appId='%s' unmapped", qPrintable(m_uuid), qPrintable(m_appId));
        m_tracker->onWindowUnmapped(this);
        delete this;
    }

private:
    TaskTracker *m_tracker;
    QString      m_uuid;
    QString      m_appId;
    uint32_t     m_state = 0;
};

// ── Window management global ──────────────────────────────────────────────────
// Binds org_kde_plasma_window_management and creates a PlasmaWindow for every
// mapped window it reports.
class TaskTracker::PlasmaWindowManagement : public QtWayland::org_kde_plasma_window_management {
public:
    PlasmaWindowManagement(TaskTracker *tracker, wl_registry *registry, uint32_t id, int version)
        : QtWayland::org_kde_plasma_window_management(registry, id, version)
        , m_tracker(tracker)
    {}

protected:
    void org_kde_plasma_window_management_window(uint32_t id) override
    {
        qDebug("kdock [wm]: window(id=%u) event (deprecated, no uuid — ignored)", id);
    }

    void org_kde_plasma_window_management_window_with_uuid(uint32_t id, const QString &uuid) override
    {
        qDebug("kdock [wm]: window_with_uuid(id=%u, uuid='%s')", id, qPrintable(uuid));

        if (uuid.isEmpty() || m_tracker->m_windows.contains(uuid)) {
            qDebug("kdock [wm]: skipping uuid='%s' (empty or already tracked)", qPrintable(uuid));
            return;
        }

        ::org_kde_plasma_window *rawWindow = get_window_by_uuid(uuid);
        qDebug("kdock [wm]: get_window_by_uuid('%s') → %s",
               qPrintable(uuid), rawWindow ? "object" : "NULL");
        if (!rawWindow)
            return;

        auto *window = new TaskTracker::PlasmaWindow(m_tracker, uuid, rawWindow);
        m_tracker->registerWindow(window, uuid);
    }

private:
    TaskTracker *m_tracker;
};

// ── Wayland registry binding ──────────────────────────────────────────────────
void TaskTracker::handleRegistryGlobal(void *data, wl_registry *registry,
                                        uint32_t name, const char *interface, uint32_t version)
{
    auto *self = static_cast<TaskTracker *>(data);

    // Log every global the compositor advertises so we can confirm whether
    // org_kde_plasma_window_management is exposed at all in this session,
    // and at what version, rather than guessing.
    qDebug("kdock [registry]: global name=%u interface='%s' version=%u",
           name, interface, version);

    if (strcmp(interface, org_kde_plasma_window_management_interface.name) == 0) {
        self->m_windowManagement = new PlasmaWindowManagement(
            self, registry, name, static_cast<int>(qMin(version, 16u)));
        qDebug("kdock [tasktracker]: bound org_kde_plasma_window_management version=%u",
               qMin(version, 16u));
    }
}

void TaskTracker::handleRegistryGlobalRemove(void *, wl_registry *, uint32_t) {}

void TaskTracker::detectWayland()
{
    static const wl_registry_listener registryListener = {
        &TaskTracker::handleRegistryGlobal,
        &TaskTracker::handleRegistryGlobalRemove,
    };

    QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface();
    if (!ni) return;

    auto *display = static_cast<wl_display *>(ni->nativeResourceForIntegration("wl_display"));
    if (!display) return;

    m_isWayland = true;

    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, this);
    // First roundtrip: discover globals and send the bind request for
    // org_kde_plasma_window_management (triggered from handleRegistryGlobal,
    // which runs synchronously while dispatching this roundtrip's events).
    wl_display_roundtrip(display);
    // Second roundtrip: the server only emits window_with_uuid for
    // already-mapped windows *after* it has processed our bind request,
    // which happens after the first roundtrip's sync point. Without this,
    // windows open before the dock starts are silently never reported.
    if (m_windowManagement)
        wl_display_roundtrip(display);
}

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
{
    detectWayland();

    qDebug("kdock [tasktracker]: isWayland=%s  windowManagementBound=%s",
           m_isWayland ? "true" : "false",
           m_windowManagement ? "true" : "false");
}

TaskTracker::~TaskTracker()
{
    qDeleteAll(m_windows);
    delete m_windowManagement;
}

// ── Window registration / lifecycle ───────────────────────────────────────────
void TaskTracker::registerWindow(PlasmaWindow *window, const QString &uuid)
{
    m_windows.insert(uuid, window);
}

void TaskTracker::onWindowAppIdChanged(PlasmaWindow *window)
{
    Q_UNUSED(window);
    rebuildRunningApps();
    refreshActiveAndUrgent();
}

void TaskTracker::onWindowStateChanged(PlasmaWindow *window)
{
    Q_UNUSED(window);
    refreshActiveAndUrgent();
}

void TaskTracker::onWindowUnmapped(PlasmaWindow *window)
{
    m_windows.remove(window->uuid());
    rebuildRunningApps();
    refreshActiveAndUrgent();
}

// ── Running apps ───────────────────────────────────────────────────────────────
void TaskTracker::rebuildRunningApps()
{
    QMap<QString, int> newCounts;
    for (PlasmaWindow *window : m_windows) {
        const QString appId = window->appId();
        // Don't track our own dock surface as a running app.
        if (appId.isEmpty() || shortName(appId) == QStringLiteral("kdock"))
            continue;
        newCounts[appId] += 1;
    }

    QSet<QString> changedApps;
    for (auto it = m_windowCounts.cbegin(); it != m_windowCounts.cend(); ++it)
        if (newCounts.value(it.key(), 0) != it.value())
            changedApps.insert(it.key());
    for (auto it = newCounts.cbegin(); it != newCounts.cend(); ++it)
        if (m_windowCounts.value(it.key(), 0) != it.value())
            changedApps.insert(it.key());

    m_windowCounts = newCounts;
    for (const QString &appId : changedApps)
        emit windowCountChanged(appId, m_windowCounts.value(appId, 0));

    QStringList newRunning = newCounts.keys();
    newRunning.sort();
    QStringList oldRunning = m_runningApps;
    oldRunning.sort();
    if (newRunning != oldRunning) {
        m_runningApps = newCounts.keys();
        qDebug("kdock [running]: apps changed → [%s]",
               qPrintable(m_runningApps.join(QStringLiteral(", "))));
        emit runningAppsChanged(m_runningApps);
    }
}

// ── Active app + urgency tracking ─────────────────────────────────────────────
void TaskTracker::refreshActiveAndUrgent()
{
    QString activeAppId;
    QSet<QString> urgentApps;

    for (PlasmaWindow *window : m_windows) {
        const QString appId = window->appId();
        if (appId.isEmpty())
            continue;
        const uint32_t state = window->state();
        if (state & QtWayland::org_kde_plasma_window_management::state_active)
            activeAppId = appId;
        if (state & QtWayland::org_kde_plasma_window_management::state_demands_attention)
            urgentApps.insert(appId);
    }

    if (activeAppId != m_activeAppId) {
        m_activeAppId = activeAppId;
        emit activeAppChanged(m_activeAppId);
    }

    for (const QString &appId : urgentApps)
        if (!m_urgentApps.contains(appId))
            emit windowUrgent(appId);
    for (const QString &appId : m_urgentApps)
        if (!urgentApps.contains(appId))
            emit windowNotUrgent(appId);
    m_urgentApps = urgentApps;
}

QStringList TaskTracker::runningApps() const { return m_runningApps; }
QString     TaskTracker::activeAppId()  const { return m_activeAppId; }

// ── Window lookup (fuzzy short-name match) ────────────────────────────────────
QList<TaskTracker::PlasmaWindow *> TaskTracker::windowsForApp(const QString &appId) const
{
    QList<PlasmaWindow *> result;
    const QString sn = shortName(appId);
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
        PlasmaWindow *window = it.value();
        if (window->appId() == appId || shortName(window->appId()) == sn)
            result.append(window);
    }
    return result;
}

bool TaskTracker::hasWindowForApp(const QString &appId) const
{
    return !windowsForApp(appId).isEmpty();
}

// ── Window actions ─────────────────────────────────────────────────────────────
void TaskTracker::activateWindow(const QString &appId)
{
    const QList<PlasmaWindow *> windows = windowsForApp(appId);
    if (windows.isEmpty()) {
        qDebug("kdock [activate]: no windows for appId='%s'  tracked=%d",
               qPrintable(appId), (int)m_windows.size());
        return;
    }

    int &idx = m_windowCycleIdx[appId];
    if (idx >= windows.size())
        idx = 0;
    PlasmaWindow *window = windows.at(idx);
    idx = (idx + 1) % windows.size();

    qDebug("kdock [activate]: uuid='%s' (%d/%d) for appId='%s'",
           qPrintable(window->uuid()), idx, (int)windows.size(), qPrintable(appId));

    window->set_state(QtWayland::org_kde_plasma_window_management::state_active,
                       QtWayland::org_kde_plasma_window_management::state_active);
}

void TaskTracker::closeWindows(const QString &appId)
{
    const QList<PlasmaWindow *> windows = windowsForApp(appId);
    qDebug("kdock [close]: closing %d window(s) for appId='%s'",
           (int)windows.size(), qPrintable(appId));
    for (PlasmaWindow *window : windows)
        window->close();
}
