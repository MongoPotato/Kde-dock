#pragma once

// LayerShellWindow wraps the Wayland layer-shell protocol.
//
// Extends QQuickView so QML is loaded directly via setSource() and
// context properties are set via rootContext(). This ensures Qt's
// rendering pipeline uses OUR window, not one created internally by
// QQmlApplicationEngine.
//
// Key design:
//   Qt::BypassWindowManagerHint prevents Qt's Wayland QPA from applying
//   the xdg-shell protocol to the window. The window is created as a bare
//   wl_surface, which we then assign the wlr-layer-shell role.
//   On X11/XWayland the flag creates an override-redirect window that we
//   position at the screen edge manually.

#include <QQuickView>
#include <QString>

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

class LayerShellWindow : public QQuickView {
    Q_OBJECT

public:
    explicit LayerShellWindow();
    ~LayerShellWindow() override;

    void setAnchor(const QString &anchor);
    QString anchor() const { return m_anchor; }

    void setThickness(int px);
    int thickness() const { return m_thickness; }

    // Public so the C-style Wayland registry callback can write to it
    zwlr_layer_shell_v1 *m_layerShell = nullptr;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void detectWayland();
    void setupWaylandLayerSurface();
    void applyX11Geometry();

    QString m_anchor { QStringLiteral("bottom") };
    int m_thickness = 72;
    bool m_isWayland = false;
    bool m_shellApplied = false;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
};
