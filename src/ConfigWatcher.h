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

    Q_PROPERTY(QString position READ position NOTIFY configChanged)
    Q_PROPERTY(int iconSize READ iconSize NOTIFY configChanged)
    Q_PROPERTY(int padding READ padding NOTIFY configChanged)
    Q_PROPERTY(int spacing READ spacing NOTIFY configChanged)
    Q_PROPERTY(int screenIndex READ screenIndex NOTIFY configChanged)
    Q_PROPERTY(bool autohide READ autohide NOTIFY configChanged)
    Q_PROPERTY(bool magnify READ magnify NOTIFY configChanged)
    Q_PROPERTY(double magnifyScale READ magnifyScale NOTIFY configChanged)
    Q_PROPERTY(int magnifyRadius READ magnifyRadius NOTIFY configChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY configChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity NOTIFY configChanged)
    Q_PROPERTY(int backgroundRadius READ backgroundRadius NOTIFY configChanged)
    Q_PROPERTY(bool runningIndicatorVisible READ runningIndicatorVisible NOTIFY configChanged)
    Q_PROPERTY(QString runningIndicatorColor READ runningIndicatorColor NOTIFY configChanged)
    Q_PROPERTY(int runningIndicatorSize READ runningIndicatorSize NOTIFY configChanged)
    Q_PROPERTY(QStringList pinnedApps READ pinnedApps NOTIFY configChanged)

public:
    explicit ConfigWatcher(QObject *parent = nullptr);

    QString position() const;
    int iconSize() const;
    int padding() const;
    int spacing() const;
    int screenIndex() const;
    bool autohide() const;
    bool magnify() const;
    double magnifyScale() const;
    int magnifyRadius() const;
    QString backgroundColor() const;
    double backgroundOpacity() const;
    int backgroundRadius() const;
    bool runningIndicatorVisible() const;
    QString runningIndicatorColor() const;
    int runningIndicatorSize() const;
    QStringList pinnedApps() const;

    Q_INVOKABLE void setPinnedApps(const QStringList &apps);
    Q_INVOKABLE void save();

    QString configPath() const;

signals:
    void configChanged();

private slots:
    void onFileChanged(const QString &path);

private:
    void load();
    void copyDefaultConfig();

    QFileSystemWatcher m_watcher;
    QJsonObject m_config;
    QString m_configPath;
};
