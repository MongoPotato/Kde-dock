#pragma once

// LayerShellWindow wraps the Wayland layer-shell protocol.
// On X11 / XWayland this falls back to a frameless always-on-top window
// at the requested edge using setWindowFlags and QScreen geometry.
// The fallback is detected at runtime: if wl_display is null we are on X11.

#include <QQuickWindow>
#include <QString>

struct wl_surface;
struct wl_display;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

class LayerShellWindow : public QQuickWindow {
    Q_OBJECT

    Q_PROPERTY(QString anchor READ anchor WRITE setAnchor NOTIFY anchorChanged)
    Q_PROPERTY(int thickness READ thickness WRITE setThickness NOTIFY geometrySettingsChanged)
    Q_PROPERTY(int length READ length WRITE setLength NOTIFY geometrySettingsChanged)

public:
    explicit LayerShellWindow(QWindow *parent = nullptr);
    ~LayerShellWindow();

    QString anchor() const;
    void setAnchor(const QString &anchor);

    int thickness() const;
    void setThickness(int px);

    int length() const;
    void setLength(int px);

    void applyLayerShell();

    // Public so that C-style Wayland registry callbacks can set it
    zwlr_layer_shell_v1 *m_layerShell = nullptr;

signals:
    void anchorChanged();
    void geometrySettingsChanged();

protected:
    void exposeEvent(QExposeEvent *event) override;

private:
    void applyX11Fallback();
    void setupWaylandLayerSurface();

    QString m_anchor{QStringLiteral("bottom")};
    int m_thickness = 72;
    int m_length = 0;
    bool m_layerShellApplied = false;
    bool m_isWayland = false;

    zwlr_layer_surface_v1 *m_layerSurface = nullptr;
};
