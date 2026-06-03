#!/usr/bin/env bash
# uninstall.sh — stop and remove KDock completely.

set -euo pipefail

echo "==> Stopping KDock..."
systemctl --user disable --now kdock 2>/dev/null || true

echo "==> Removing files..."
rm -f "$HOME/.local/bin/kdock"
rm -f "$HOME/.config/systemd/user/kdock.service"
rm -f "$HOME/.config/autostart/kdock.desktop"
systemctl --user daemon-reload 2>/dev/null || true

echo "  Config and icons left in $HOME/.config/kdock/ — remove manually if desired."
echo "✓ KDock uninstalled."
