// See LayerShellPopup.h for why the dock's menus need their own layer surface.

#include "LayerShellPopup.h"
#include "LayerShellGlobal.h"

#include <QExposeEvent>
#include <QList>
#include <QGuiApplication>
#include <QHideEvent>
#include <QResizeEvent>
#include <QScreen>
#include <qpa/qplatformnativeinterface.h>

#include "wayland-wlr-layer-shell-unstable-v1-client-protocol.h"
#include <wayland-client.h>

// ── Layer-surface callbacks ────────────────────────────────────────────────

static void popupConfigure(void *data, zwlr_layer_surface_v1 *surface,
                           uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    auto *self = static_cast<LayerShellPopup *>(data);
    if (width > 0 && height > 0) {
        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        QMetaObject::invokeMethod(self, [self, w, h]() { self->resize(w, h); },
                                  Qt::QueuedConnection);
    }
}

static void popupClosed(void *data, zwlr_layer_surface_v1 *)
{
    // The compositor asking us to go away bypasses every QML-side rule about
    // when a menu may close, so say so — otherwise a menu vanishing for this
    // reason is indistinguishable from one of our own timers firing.
    qDebug("kdock [popup]: compositor closed the layer surface");
    auto *self = static_cast<LayerShellPopup *>(data);
    QMetaObject::invokeMethod(self, [self]() { self->setVisible(false); },
                              Qt::QueuedConnection);
}

static const zwlr_layer_surface_v1_listener s_popupListener = {
    popupConfigure,
    popupClosed,
};

// Every live popup, so an exclusive one can dismiss the others when it opens.
static QList<LayerShellPopup *> s_popups;

// ── LayerShellPopup ────────────────────────────────────────────────────────

LayerShellPopup::LayerShellPopup(QWindow *parent)
    : QQuickWindow(parent)
{
    m_useLayerShell = LayerShellGlobal::available();
    if (!m_useLayerShell) {
        qWarning("kdock [popup]: wlr-layer-shell unavailable — menus fall back to "
                 "ordinary windows and will be stacked under the dock");
    }

    if (m_useLayerShell) {
        // Same reason as the dock: BypassWindowManagerHint stops Qt's Wayland
        // QPA from assigning xdg-shell, leaving a bare wl_surface we can give
        // the layer-shell role to ourselves.
        setFlags(Qt::BypassWindowManagerHint | Qt::FramelessWindowHint);
    } else {
        setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    }
    setColor(Qt::transparent);

    connect(this, &QWindow::activeChanged, this, &LayerShellPopup::popupActiveChanged);

    // Two menus should never be on screen at once: opening one closes any
    // other. Done here rather than in QML because the menus are separate
    // instances scattered across DockBar and every DockItem, with no shared
    // scope to coordinate through.
    connect(this, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible || !m_exclusive) return;
        for (LayerShellPopup *other : std::as_const(s_popups)) {
            if (other != this && other->m_exclusive && other->isVisible())
                other->setVisible(false);
        }
    });

    s_popups.append(this);
}

LayerShellPopup::~LayerShellPopup()
{
    s_popups.removeAll(this);
    destroyLayerSurface();
}

void LayerShellPopup::setExclusive(bool on)
{
    if (m_exclusive == on) return;
    m_exclusive = on;
    emit exclusiveChanged();
}

void LayerShellPopup::setPopupX(int x)
{
    if (m_popupX == x) return;
    m_popupX = x;
    if (m_useLayerShell) applyLayerGeometry();
    else                 setX(x);
    emit popupPositionChanged();
}

void LayerShellPopup::setPopupY(int y)
{
    if (m_popupY == y) return;
    m_popupY = y;
    if (m_useLayerShell) applyLayerGeometry();
    else                 setY(y);
    emit popupPositionChanged();
}

void LayerShellPopup::exposeEvent(QExposeEvent *event)
{
    QQuickWindow::exposeEvent(event);
    if (m_useLayerShell && isExposed() && !m_layerSurface)
        createLayerSurface();
}

void LayerShellPopup::hideEvent(QHideEvent *event)
{
    // Tear the role down on hide. Qt destroys and recreates the wl_surface
    // across hide/show, so a stale layer surface would outlive the surface it
    // was attached to and the next open would silently fail to appear.
    destroyLayerSurface();
    QQuickWindow::hideEvent(event);
}

void LayerShellPopup::resizeEvent(QResizeEvent *event)
{
    QQuickWindow::resizeEvent(event);
    // The menu's height depends on which entries are shown for this mode, so
    // it can change after the surface exists.
    if (m_layerSurface) applyLayerGeometry();
}

void LayerShellPopup::createLayerSurface()
{
    zwlr_layer_shell_v1 *shell = LayerShellGlobal::shell();
    wl_surface *surface = LayerShellGlobal::surfaceFor(this);
    if (!shell || !surface) return;

    // Bind to the output the popup is being opened on, so the margins below
    // are measured from the right screen's top-left corner.
    wl_output *output = nullptr;
    QScreen *target = QGuiApplication::screenAt(QPoint(m_popupX, m_popupY));
    if (!target) target = screen();
    if (target) {
        if (QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface())
            output = static_cast<wl_output *>(ni->nativeResourceForScreen("wl_output", target));
    }

    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        shell, surface, output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "kdock-popup");
    if (!m_layerSurface) {
        qWarning("kdock [popup]: could not create an OVERLAY layer surface — "
                 "the menu will be an ordinary window and may sit under the dock");
        return;
    }

    zwlr_layer_surface_v1_add_listener(m_layerSurface, &s_popupListener, this);

    // ON_DEMAND lets the menu take keyboard focus when it is clicked, which is
    // what makes Escape and focus-out dismissal work. It arrived in version 4;
    // on older compositors the menu simply stays click-only.
    if (LayerShellGlobal::version() >= 4) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            m_layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    }

    applyLayerGeometry();
}

void LayerShellPopup::destroyLayerSurface()
{
    if (!m_layerSurface) return;
    zwlr_layer_surface_v1_destroy(m_layerSurface);
    m_layerSurface = nullptr;
}

void LayerShellPopup::applyLayerGeometry()
{
    if (!m_layerSurface) return;

    wl_surface *surface = LayerShellGlobal::surfaceFor(this);
    if (!surface) return;

    // Anchoring to two adjacent edges and setting margins is how a layer
    // surface is placed at an arbitrary point: the margins become the offset
    // from the output's top-left corner.
    QScreen *target = QGuiApplication::screenAt(QPoint(m_popupX, m_popupY));
    if (!target) target = screen();
    const QPoint origin = target ? target->geometry().topLeft() : QPoint(0, 0);

    zwlr_layer_surface_v1_set_anchor(m_layerSurface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                                   | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_margin(m_layerSurface,
                                     m_popupY - origin.y(),  // top
                                     0,                      // right
                                     0,                      // bottom
                                     m_popupX - origin.x()); // left

    // -1 opts out of being pushed around by other surfaces' exclusive zones,
    // including the dock's own: the menu goes exactly where it was asked to.
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, -1);

    zwlr_layer_surface_v1_set_size(m_layerSurface,
                                   static_cast<uint32_t>(qMax(1, width())),
                                   static_cast<uint32_t>(qMax(1, height())));

    wl_surface_commit(surface);
    LayerShellGlobal::roundtrip();

    qDebug("kdock [popup]: OVERLAY surface at %d,%d size %dx%d (origin %d,%d)",
           m_popupX, m_popupY, width(), height(), origin.x(), origin.y());
}
