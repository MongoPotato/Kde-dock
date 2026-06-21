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

    // Exclusive zone is the screen space reserved from the desktop (visual strip).
    // Thickness is the full window height (visual strip + overflow for magnification).
    void setExclusiveZone(int px);
    int exclusiveZone() const { return m_exclusiveZone; }

    // Apply thickness + exclusive zone to the running layer surface (no-op if not yet shown).
    void applyGeometryUpdate();

    // Shrinks the real Wayland surface down to a thin reveal-strip at the
    // anchored edge when auto-hidden (revealed=false), or restores it to the
    // full configured thickness (revealed=true). Unlike the QML-side slide
    // transform — which only moves the visual content — this resizes the
    // actual surface/exclusive zone so the screen edge isn't blocked while hidden.
    Q_INVOKABLE void setRevealed(bool revealed);
    bool revealed() const { return m_revealed; }

    void setBlurEnabled(bool enabled);

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
    int m_exclusiveZone = 72;
    bool m_isWayland = false;
    bool m_shellApplied = false;
    bool m_blurEnabled = false;
    bool m_revealed = true;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
};
