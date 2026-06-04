#pragma once

// ConfigWatcher owns the dock.json file lifecycle.
// Atomic write (write to .tmp then rename) prevents a half-written
// config if the process is killed during a save.
//
// All path expansion uses QStandardPaths::writableLocation(
//   QStandardPaths::AppConfigLocation) so XDG_CONFIG_HOME is respected.

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class ConfigWatcher : public QObject {
    Q_OBJECT

    // ── Position & geometry ───────────────────────────────────────────────
    Q_PROPERTY(QString position READ position NOTIFY configChanged)
    Q_PROPERTY(int iconSize READ iconSize NOTIFY configChanged)
    Q_PROPERTY(int padding READ padding NOTIFY configChanged)
    Q_PROPERTY(int spacing READ spacing NOTIFY configChanged)
    Q_PROPERTY(int screenIndex READ screenIndex NOTIFY configChanged)

    // ── Dock bar background ───────────────────────────────────────────────
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY configChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity NOTIFY configChanged)
    Q_PROPERTY(int backgroundRadius READ backgroundRadius NOTIFY configChanged)

    // ── Per-icon background ───────────────────────────────────────────────
    Q_PROPERTY(QString iconBgShape READ iconBgShape NOTIFY configChanged)
    Q_PROPERTY(QString iconBgColor READ iconBgColor NOTIFY configChanged)
    Q_PROPERTY(double iconBgOpacity READ iconBgOpacity NOTIFY configChanged)
    Q_PROPERTY(int iconBgPadding READ iconBgPadding NOTIFY configChanged)
    Q_PROPERTY(int iconBgRadius READ iconBgRadius NOTIFY configChanged)
    Q_PROPERTY(QString iconBgBorderColor READ iconBgBorderColor NOTIFY configChanged)
    Q_PROPERTY(int iconBgBorderWidth READ iconBgBorderWidth NOTIFY configChanged)

    // ── Hover effects ─────────────────────────────────────────────────────
    Q_PROPERTY(int hoverLiftPx READ hoverLiftPx NOTIFY configChanged)
    Q_PROPERTY(double hoverScaleBoost READ hoverScaleBoost NOTIFY configChanged)
    Q_PROPERTY(double hoverGlowOpacity READ hoverGlowOpacity NOTIFY configChanged)
    Q_PROPERTY(QString hoverGlowColor READ hoverGlowColor NOTIFY configChanged)

    // ── Scroll-to-zoom ────────────────────────────────────────────────────
    Q_PROPERTY(int scrollStepPx READ scrollStepPx NOTIFY configChanged)
    Q_PROPERTY(int scrollMinSize READ scrollMinSize NOTIFY configChanged)
    Q_PROPERTY(int scrollMaxSize READ scrollMaxSize NOTIFY configChanged)

    // ── Behaviour ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool autohide READ autohide NOTIFY configChanged)
    Q_PROPERTY(bool magnify READ magnify NOTIFY configChanged)
    Q_PROPERTY(double magnifyScale READ magnifyScale NOTIFY configChanged)
    Q_PROPERTY(int magnifyRadius READ magnifyRadius NOTIFY configChanged)

    // ── Running indicator ─────────────────────────────────────────────────
    Q_PROPERTY(bool runningIndicatorVisible READ runningIndicatorVisible NOTIFY configChanged)
    Q_PROPERTY(QString runningIndicatorColor READ runningIndicatorColor NOTIFY configChanged)
    Q_PROPERTY(int runningIndicatorSize READ runningIndicatorSize NOTIFY configChanged)

    // ── Pinned apps ───────────────────────────────────────────────────────
    Q_PROPERTY(QStringList pinnedApps READ pinnedApps NOTIFY configChanged)

public:
    explicit ConfigWatcher(QObject *parent = nullptr);

    // ── Accessors ─────────────────────────────────────────────────────────
    QString position() const;
    int iconSize() const;
    int padding() const;
    int spacing() const;
    int screenIndex() const;

    QString backgroundColor() const;
    double backgroundOpacity() const;
    int backgroundRadius() const;

    QString iconBgShape() const;
    QString iconBgColor() const;
    double iconBgOpacity() const;
    int iconBgPadding() const;
    int iconBgRadius() const;
    QString iconBgBorderColor() const;
    int iconBgBorderWidth() const;

    int hoverLiftPx() const;
    double hoverScaleBoost() const;
    double hoverGlowOpacity() const;
    QString hoverGlowColor() const;

    int scrollStepPx() const;
    int scrollMinSize() const;
    int scrollMaxSize() const;

    bool autohide() const;
    bool magnify() const;
    double magnifyScale() const;
    int magnifyRadius() const;

    bool runningIndicatorVisible() const;
    QString runningIndicatorColor() const;
    int runningIndicatorSize() const;

    QStringList pinnedApps() const;

    // ── Mutators (called by SettingsController / QML) ─────────────────────
    Q_INVOKABLE void setIconSize(int px);
    Q_INVOKABLE void setMagnifyScale(double s);
    Q_INVOKABLE void setHoverLiftPx(int px);
    Q_INVOKABLE void setBackgroundOpacity(double v);
    Q_INVOKABLE void setAutohide(bool on);
    Q_INVOKABLE void setIconBgShape(const QString &shape);
    Q_INVOKABLE void setIconBgOpacity(double v);

    Q_INVOKABLE void setPinnedApps(const QStringList &apps);
    Q_INVOKABLE void save();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void resetToDefaults();

    QString configPath() const;

signals:
    void configChanged();

private slots:
    void onFileChanged(const QString &path);

private:
    void load();
    void copyDefaultConfig();
    void writeValue(const QString &key, const QJsonValue &value);
    template<typename T>
    void writeNestedValue(const QString &section, const QString &key, const T &value);

    QFileSystemWatcher m_watcher;
    QJsonObject m_config;
    QString m_configPath;
};
