# KDock

A lightweight, floating application dock for **KDE Plasma 6 on Wayland** —
the kind of dock you'd expect from macOS or a Linux dock app like Plank or
Latte, but built specifically against Plasma 6's Wayland session using
`wlr-layer-shell` and KWin's own scripting engine, with no X11 dependency.

![position: bottom](https://img.shields.io/badge/position-bottom%2Ftop%2Fleft%2Fright-blue)
![platform](https://img.shields.io/badge/platform-KDE%20Plasma%206%20Wayland-informational)

## Features

- Pin apps, and running-but-unpinned apps appear automatically (macOS-style).
- Click an icon to focus its window; click again to **cycle** through
  multiple open windows of the same app; click once more on the single,
  already-focused window to **minimize** it.
- **Middle-click** an icon to always open a brand-new window/instance.
- **Right-click** for a context menu: open new window, pin/unpin, close all
  windows, manage dock apps, dock settings.
- Live running/urgent (demands-attention) indicators per app.
- Scroll over the dock to resize icons on the fly.
- Hover effects: lift, magnify, glow — all configurable.
- Optional auto-hide, blur-behind, and adaptive colour tinting from the
  active app's icon.
- Live config reload — edit `dock.json` and the dock updates without a
  restart.
- Runs as a systemd user service (or XDG autostart fallback) so it
  survives logout/login.

## How it works (briefly)

Window activation/closing/minimizing on Plasma 6 Wayland has no public
DBus or Wayland-protocol API for ordinary clients — KDock drives KWin's own
JavaScript scripting engine (`org.kde.kwin.Scripting` over DBus) instead: a
persistent script tracks windows, and small one-shot scripts perform
activate/close/minimize directly inside KWin. See the comments at the top
of `src/TaskTracker.cpp` for the full rationale.

## Dependencies

Build-time (Debian/Ubuntu package names — adjust for your distro):

| Package                  | Purpose                                   |
|---------------------------|--------------------------------------------|
| `cmake` (≥ 3.20)          | Build system                              |
| `pkg-config`              | Library discovery                         |
| `g++` / `gcc`             | C++17 compiler                            |
| `qt6-base-dev`            | Qt6 Core/Gui/DBus                         |
| `qt6-declarative-dev`     | Qt Quick / QML                            |
| `qt6-wayland-dev`         | Qt WaylandClient (layer-shell surface)    |
| `qt6-base-private-dev`   | Private Gui headers (Wayland native iface)|
| `libwayland-dev`          | `wayland-client`/`wayland-scanner`        |

Install on **Ubuntu/Kubuntu**:

```bash
sudo apt install cmake pkg-config g++ qt6-base-dev qt6-declarative-dev \
                  qt6-wayland-dev qt6-base-private-dev libwayland-dev
```

Install on **Fedora**:

```bash
sudo dnf install cmake pkgconf-pkg-config gcc-c++ qt6-qtbase-devel \
                  qt6-qtdeclarative-devel qt6-qtwayland-devel \
                  qt6-qtbase-private-devel wayland-devel
```

Install on **Arch**:

```bash
sudo pacman -S cmake pkgconf base-devel qt6-base qt6-declarative \
               qt6-wayland wayland
```

Runtime requirements:

- **KDE Plasma 6** session running on **Wayland** (not X11 — KDock uses
  `wlr-layer-shell` and KWin's scripting DBus interface, neither of which
  exist under X11/KWin's X11 backend).
- `gtk-launch` (from `gtk2-utils`/`libgtk-3-bin`) or `kioclient5`/`xdg-open`
  as a fallback, used to launch apps from their `.desktop` files. At least
  one of these is present on virtually any KDE/GNOME install.
- `systemd` (optional) — only needed if you want KDock to autostart via a
  systemd user service rather than XDG autostart.

## Installing

```bash
git clone <this-repo>
cd kdock
./install.sh
```

`install.sh`:

1. Checks for `cmake`/`pkg-config`/Qt6 and prints the right install command
   if anything is missing.
2. Configures and builds with CMake (`Release`, installs to `~/.local`).
3. Copies `data/default_dock.json` to `~/.config/kdock/dock.json` **only**
   if you don't already have a config there — it never overwrites your
   settings.
4. Creates `~/.config/kdock/icons/` for custom icon overrides.
5. Installs and enables a systemd user service (`systemctl --user enable
   --now kdock`), or an XDG autostart `.desktop` file as a fallback if
   `systemctl` isn't available.

Preview what it would do without changing anything:

```bash
./install.sh --dry-run
```

### Manual build (no install)

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
./build/kdock            # run directly without installing
```

### Uninstalling

```bash
./uninstall.sh
```

Stops and disables the systemd service, and removes the installed binary,
service file, and autostart entry. Your config and custom icons in
`~/.config/kdock/` are left untouched — remove that directory yourself if
you want a clean slate.

## Usage

Once running, KDock appears as a floating strip anchored to one edge of
your screen (bottom by default).

| Action                          | Result                                          |
|----------------------------------|--------------------------------------------------|
| Left-click an icon               | Focus the app's window                          |
| Left-click again (multiple windows) | Cycle to the next window of that app         |
| Left-click again (one window, focused) | Minimize it                              |
| Middle-click                     | Always open a new window/instance               |
| Right-click an icon              | Context menu (open new window, pin/unpin, close all windows) |
| Right-click empty dock space     | Dock-level menu (manage apps, settings, reload config) |
| Scroll over the dock             | Resize icons live                               |

### Configuration

Settings live at `~/.config/kdock/dock.json` and are watched live — edit
the file (or use **Dock settings…** from the right-click menu) and changes
apply immediately, no restart needed.

Open the in-app settings panel via right-click → **Dock settings…** to
adjust icon size, hover lift/magnify, dock position and opacity, icon
background shape, blur-behind, adaptive colour tint, and auto-hide — or
edit `dock.json` directly:

```jsonc
{
  "pinned": ["org.kde.dolphin", "org.kde.konsole"],
  "position": "bottom",        // bottom | top | left | right
  "iconSize": 52,
  "padding": 8,
  "spacing": 6,
  "autohide": false,
  "magnify": true,
  "magnifyScale": 1.5,
  "background": { "color": "#1a1a2e", "opacity": 0.85, "radius": 14 }
  // see data/default_dock.json for every available key
}
```

Manage pinned apps (add/remove/reorder via drag) through right-click →
**Manage dock apps…**.

### Running with debug logging

```bash
kdock --debug
```

Enables verbose QML/Qt/Wayland logging plus startup diagnostics (pinned
apps, dock position, icon size, QML resolution path) — useful when filing
an issue or diagnosing window-tracking problems.

```bash
kdock --help       # all CLI options
kdock --version
```

## Running the test suite

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Tests run headless (`QT_QPA_PLATFORM=offscreen`) and cover `ConfigWatcher`,
`DockModel`, `IconProvider`, `IconThemeDetector`, `SettingsController`, and
`TaskTracker` (smoke-tested without a live KWin session).

## Troubleshooting

- **Dock doesn't appear / blank strip**: run `kdock --debug` from a
  terminal and check for `QML error:` lines — usually a missing QML module
  (see Dependencies above) or the wrong `qml/` path being resolved.
- **Clicking an icon launches a duplicate window instead of focusing the
  existing one**: confirm you're on a Wayland session (`echo
  $XDG_SESSION_TYPE` should say `wayland`) — KDock's window tracking
  requires KWin's scripting DBus interface, which isn't reachable the same
  way under X11.
- **Service won't start**: `systemctl --user status kdock` and `journalctl
  --user -u kdock -e` for logs.
