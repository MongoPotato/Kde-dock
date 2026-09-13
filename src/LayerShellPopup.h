#pragma once

// LayerShellPopup: a QQuickWindow that puts itself on the layer-shell OVERLAY
// layer instead of being an xdg-toplevel.
//
// Why this exists. The dock's context menus used to be plain QML Windows
// declared inside the dock's QQuickView. QML gives such a Window the view's
// window as its transientParent, and the view's surface is a layer surface —
// so KWin stacked the menu together with the dock's layer rather than above
// it, and the menu came up UNDERNEATH the dock (issue #2). No amount of
// raise()/WindowStaysOnTopHint fixes that: an xdg-toplevel cannot be stacked
// above a layer surface that isn't on a lower layer than it.
//
// A menu belongs on the OVERLAY layer, above every other layer including the
// dock's own, which is exactly what wlr-layer-shell is for. Positioning uses
// anchor TOP|LEFT plus margins, the standard way to place a layer surface at
// an arbitrary point, and an exclusive zone of -1 so other panels' reserved
// space doesn't push the menu around.
//
// If layer-shell isn't available (not Wayland, or a compositor without it)
// the class stays a perfectly ordinary window positioned with x/y, which is
// the behaviour it replaces.

#include <QQuickWindow>

struct zwlr_layer_surface_v1;

class LayerShellPopup : public QQuickWindow {
    Q_OBJECT

    // Top-left corner in global screen coordinates. Separate from x/y because
    // a layer surface is placed by margins, not by the window's own position,
    // and Qt's x/y are meaningless for one.
    Q_PROPERTY(int popupX READ popupX WRITE setPopupX NOTIFY popupPositionChanged)
    Q_PROPERTY(int popupY READ popupY WRITE setPopupY NOTIFY popupPositionChanged)

    // False when the plain-window fallback is in use, so QML can keep relying
    // on focus-based dismissal where that still works.
    Q_PROPERTY(bool usingLayerShell READ usingLayerShell CONSTANT)

    // QWindow::active is a revisioned property, and revisions of a base class
    // registered in another module aren't exposed through this one — QML sees
    // no onActiveChanged on LayerPopup. Re-export it under our own name.
    Q_PROPERTY(bool popupActive READ isActive NOTIFY popupActiveChanged)

public:
    explicit LayerShellPopup(QWindow *parent = nullptr);
    ~LayerShellPopup() override;

    int popupX() const { return m_popupX; }
    int popupY() const { return m_popupY; }
    void setPopupX(int x);
    void setPopupY(int y);

    bool usingLayerShell() const { return m_useLayerShell; }

signals:
    void popupPositionChanged();
    void popupActiveChanged();

protected:
    void exposeEvent(QExposeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void createLayerSurface();
    void destroyLayerSurface();
    void applyLayerGeometry();

    bool m_useLayerShell = false;
    int  m_popupX = 0;
    int  m_popupY = 0;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
};
