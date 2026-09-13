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
#include <QRegion>
#include <QString>

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
class QScreen;

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

    // How much of the window, measured from the anchored edge, actually takes
    // pointer input while revealed. The window is taller than this so the
    // click-bounce animation isn't clipped, but that headroom paints nothing —
    // leaving it interactive turned it into an invisible band across the
    // screen that swallowed clicks meant for the window behind the dock.
    void setInteractiveThickness(int px);
    int interactiveThickness() const { return m_interactiveThickness; }

    // Apply thickness + exclusive zone to the running layer surface (no-op if not yet shown).
    void applyGeometryUpdate();

    // Auto-hide reveal state.
    //
    // While hidden (revealed=false) the surface KEEPS its full configured
    // size and only its input region is narrowed to a thin reveal-strip along
    // the anchored edge, so pointer events outside the strip fall through to
    // whatever is underneath and the screen edge isn't blocked.
    //
    // Resizing the surface instead — which is what this used to do — made the
    // compositor re-deliver pointer enter/leave every time the dock revealed
    // or hid. With the cursor parked in the reveal strip that fed straight
    // back into the hover test and the dock cycled show/hide indefinitely
    // (issue #3). A constant-size surface has no such feedback loop.
    Q_INVOKABLE void setRevealed(bool revealed);
    bool revealed() const { return m_revealed; }

    void setBlurEnabled(bool enabled);

    // Moves the dock onto a different output, e.g. when KDE's primary screen
    // changes or the screen the dock was on gets unplugged. Layer-shell binds
    // a surface to one wl_output for life, so on Wayland this tears down and
    // recreates the layer surface against the new screen's output; on X11
    // it's just a geometry re-apply. No-op if already on this screen.
    void reanchorToScreen(QScreen *targetScreen);

    // Public so the C-style Wayland registry callback can write to it
    zwlr_layer_shell_v1 *m_layerShell = nullptr;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void detectWayland();
    void setupWaylandLayerSurface();
    void applyX11Geometry();

    // Narrows the window's input region to the reveal strip (hidden) or to
    // the dock band (revealed). Uses QWindow::setMask(), which QtWayland maps
    // onto wl_surface::set_input_region and XCB onto the XShape input mask,
    // so the same call works on both backends.
    void applyInputMask();

    QString m_anchor { QStringLiteral("bottom") };
    int m_thickness = 72;
    int m_exclusiveZone = 72;
    int m_interactiveThickness = 0;   // 0 = whole window
    bool m_isWayland = false;
    bool m_shellApplied = false;
    bool m_blurEnabled = false;
    bool m_revealed = true;

    // Last region handed to setMask(), so a resize or a reveal that doesn't
    // actually change the input region costs no surface commit.
    QRegion m_appliedMask;
    bool m_maskApplied = false;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
};
