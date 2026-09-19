#!/usr/bin/env bash
# install.sh — build and install KDock to ~/.local, then set up persistence.
#
# What this script does:
#   1. Check for required build deps (cmake, qt6-base-dev, etc.)
#   2. cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
#   3. cmake --build build -j$(nproc)
#   4. cmake --install build
#   5. Copy default_dock.json to ~/.config/kdock/dock.json if not present
#   6. Create ~/.config/kdock/icons/ directory
#   7. Install systemd user unit to ~/.config/systemd/user/kdock.service
#      and run: systemctl --user daemon-reload && systemctl --user enable --now kdock
#   8. Install XDG autostart .desktop as fallback
#   9. Print success message with "kdock --help" reminder

set -euo pipefail

PREFIX="$HOME/.local"
CONFIG_DIR="$HOME/.config/kdock"
SYSTEMD_DIR="$HOME/.config/systemd/user"
AUTOSTART_DIR="$HOME/.config/autostart"

# ── Dry-run support ────────────────────────────────────────────────────────
DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=true
    echo "==> DRY RUN — commands will be printed but not executed"
fi

run() {
    if $DRY_RUN; then
        echo "  [dry-run] $*"
    else
        "$@"
    fi
}

# ── Dependency check ───────────────────────────────────────────────────────
echo "==> Checking build dependencies..."
MISSING=()
for cmd in cmake pkg-config; do
    if ! command -v "$cmd" &>/dev/null; then
        MISSING+=("$cmd")
    fi
done

if ! pkg-config --exists Qt6Core 2>/dev/null && ! qmake6 --version &>/dev/null; then
    MISSING+=("qt6-base-dev")
fi

if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "ERROR: Missing dependencies: ${MISSING[*]}"
    echo "Install them with:"
    echo "  sudo pacman -S cmake qt6-base qt6-declarative qt6-wayland kwindowsystem extra-cmake-modules"
    echo "  # or on Fedora/Ubuntu — see README.md"
    exit 1
fi

# ── Build ──────────────────────────────────────────────────────────────────
echo "==> Building KDock..."
run cmake -B build -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release
run cmake --build build -j"$(nproc)"
run cmake --install build

# ── Config directory ───────────────────────────────────────────────────────
echo "==> Setting up config directory..."
run mkdir -p "$CONFIG_DIR/icons"
if [ ! -f "$CONFIG_DIR/dock.json" ]; then
    run cp data/default_dock.json "$CONFIG_DIR/dock.json"
    echo "    Created default config at $CONFIG_DIR/dock.json"
else
    echo "    Config already exists — leaving it untouched."
fi

# ── systemd user service ───────────────────────────────────────────────────
echo "==> Installing systemd user service..."
run mkdir -p "$SYSTEMD_DIR"
if $DRY_RUN; then
    echo "  [dry-run] sed 's|%h|$HOME|g' data/kdock.service > $SYSTEMD_DIR/kdock.service"
else
    sed "s|%h|$HOME|g" data/kdock.service > "$SYSTEMD_DIR/kdock.service"
fi

if command -v systemctl &>/dev/null; then
    run systemctl --user daemon-reload
    run systemctl --user enable --now kdock
    echo "    KDock enabled and started via systemd."
else
    echo "    systemctl not found — skipping systemd setup."
fi

# ── XDG autostart fallback ─────────────────────────────────────────────────
echo "==> Installing XDG autostart fallback..."
run mkdir -p "$AUTOSTART_DIR"
if $DRY_RUN; then
    echo "  [dry-run] sed 's|%h|$HOME|g' data/kdock.desktop > $AUTOSTART_DIR/kdock.desktop"
else
    sed "s|%h|$HOME|g" data/kdock.desktop > "$AUTOSTART_DIR/kdock.desktop"
fi

echo ""
echo "✓ KDock installed successfully!"
echo "  Config:  $CONFIG_DIR/dock.json"
echo "  Icons:   $CONFIG_DIR/icons/"
echo "  Status:  systemctl --user status kdock"
