#pragma once

// Shared access to the compositor's zwlr_layer_shell_v1 global.
//
// Both the dock itself (LayerShellWindow) and its popups (LayerShellPopup)
// need to assign the layer-shell role to their surfaces, and a Wayland global
// only has to be bound once per client. Binding it here keeps the registry
// round-trip in one place and lets either class ask, before it creates its
// window, whether layer-shell is usable at all — which is what decides
// between the layer-shell path and the plain-window fallback.

#include <QString>

#include <cstdint>

struct zwlr_layer_shell_v1;
struct wl_surface;
class QWindow;

namespace LayerShellGlobal {

// True when running on Wayland at all (a display could be obtained).
bool isWayland();

// The bound layer-shell global, or nullptr when unavailable — not on Wayland,
// or a compositor that doesn't implement wlr-layer-shell. Binds on first call.
zwlr_layer_shell_v1 *shell();

// Interface version actually bound. Version 4 added ON_DEMAND keyboard
// interactivity, which popups need in order to take focus when clicked.
uint32_t version();

inline bool available() { return shell() != nullptr; }

// The wl_surface behind a QWindow, or nullptr if it has none yet.
wl_surface *surfaceFor(QWindow *window);

// Block until the compositor has processed everything sent so far.
void roundtrip();

// One line describing what the probe found. Diagnostics only.
QString diagnostics();

} // namespace LayerShellGlobal
