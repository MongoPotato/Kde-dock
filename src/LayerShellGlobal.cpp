#include "LayerShellGlobal.h"

#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include "wayland-wlr-layer-shell-unstable-v1-client-protocol.h"
#include <wayland-client.h>

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

    wl_registry *registry = wl_display_get_registry(d);
    wl_registry_add_listener(registry, &s_registryListener, nullptr);
    wl_display_roundtrip(d);
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

void roundtrip()
{
    if (wl_display *d = display())
        wl_display_roundtrip(d);
}

} // namespace LayerShellGlobal
