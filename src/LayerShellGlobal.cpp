#include "LayerShellGlobal.h"

#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include "wayland-wlr-layer-shell-unstable-v1-client-protocol.h"
#include <wayland-client.h>

#include <QString>

#include <cstring>

namespace {

struct Binding {
    bool                 probed     = false;
    bool                 wayland    = false;
    zwlr_layer_shell_v1 *shell      = nullptr;
    uint32_t             version    = 0;
};

Binding g_binding;

wl_display *display()
{
    QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface();
    if (!ni) return nullptr;
    return static_cast<wl_display *>(ni->nativeResourceForIntegration("wl_display"));
}

void registryGlobal(void *, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version)
{
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) != 0)
        return;
    // Cap at 4: that is the newest version this client knows how to speak.
    const uint32_t bound = qMin(version, 4u);
    g_binding.shell = static_cast<zwlr_layer_shell_v1 *>(
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bound));
    g_binding.version = bound;
}

void registryGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener s_registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

void probe()
{
    if (g_binding.probed) return;
    g_binding.probed = true;

    wl_display *d = display();
    if (!d) return;
    g_binding.wayland = true;

    // Use a PRIVATE event queue for the registry round-trip.
    //
    // QtWayland reads the socket on its own thread. A plain
    // wl_display_roundtrip() on the default queue races that thread: it can
    // dispatch our registry events into Qt's queue instead of ours, so the
    // listener below never fires and we conclude — wrongly — that the
    // compositor has no layer shell. The dock then falls back to behaving
    // like an ordinary window: centred, and in alt-tab.
    //
    // A private queue makes the round-trip dispatch only our own events, so
    // the result doesn't depend on who wins the race.
    wl_event_queue *queue = wl_display_create_queue(d);
    if (!queue) return;

    wl_registry *registry = wl_display_get_registry(d);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);
    wl_registry_add_listener(registry, &s_registryListener, nullptr);
    wl_display_roundtrip_queue(d, queue);

    // Objects created from the shell inherit its queue, and nothing dispatches
    // the private one after this point — put the shell back on the default
    // queue so layer surfaces get their configure events like normal.
    if (g_binding.shell)
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(g_binding.shell), nullptr);

    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
}

} // namespace

namespace LayerShellGlobal {

bool isWayland()
{
    probe();
    return g_binding.wayland;
}

zwlr_layer_shell_v1 *shell()
{
    probe();
    return g_binding.shell;
}

uint32_t version()
{
    probe();
    return g_binding.version;
}

wl_surface *surfaceFor(QWindow *window)
{
    QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface();
    if (!ni || !window) return nullptr;
    return static_cast<wl_surface *>(ni->nativeResourceForWindow("wl_surface", window));
}

QString diagnostics()
{
    probe();
    if (!g_binding.wayland)
        return QStringLiteral("not a Wayland session (no wl_display)");
    if (!g_binding.shell)
        return QStringLiteral("Wayland, but the compositor did not offer "
                              "zwlr_layer_shell_v1");
    return QStringLiteral("Wayland, zwlr_layer_shell_v1 v%1").arg(g_binding.version);
}

void roundtrip()
{
    if (wl_display *d = display())
        wl_display_roundtrip(d);
}

} // namespace LayerShellGlobal
