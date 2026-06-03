// ConfigWatcher owns the dock.json file lifecycle.
// Atomic write (write to .tmp then rename) prevents a half-written
// config if the process is killed during a save.
//
// All path expansion uses QStandardPaths::writableLocation(
//   QStandardPaths::AppConfigLocation) so XDG_CONFIG_HOME is respected.

#include "ConfigWatcher.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

ConfigWatcher::ConfigWatcher(QObject *parent)
    : QObject(parent)
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QDir().mkpath(configDir + QStringLiteral("/icons"));

    m_configPath = configDir + QStringLiteral("/dock.json");

    if (!QFile::exists(m_configPath)) {
        copyDefaultConfig();
    }

    load();

    m_watcher.addPath(m_configPath);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigWatcher::onFileChanged);
}

void ConfigWatcher::copyDefaultConfig()
{
    // Try installed location first, then source tree
    QStringList candidates = {
        QStringLiteral(QML_INSTALL_DIR "/../default_dock.json"),
        QStringLiteral("data/default_dock.json"),
    };
    for (const QString &src : candidates) {
        if (QFile::exists(src)) {
            QFile::copy(src, m_configPath);
            return;
        }
    }

    // Write a minimal built-in default if no file found
    QJsonObject root;
    root[QStringLiteral("pinned")] = QJsonArray{
        QStringLiteral("org.kde.dolphin"),
        QStringLiteral("org.kde.konsole"),
        QStringLiteral("org.mozilla.firefox"),
    };
    root[QStringLiteral("position")] = QStringLiteral("bottom");
    root[QStringLiteral("iconSize")] = 52;
    root[QStringLiteral("padding")] = 8;
    root[QStringLiteral("spacing")] = 6;

    QSaveFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

void ConfigWatcher::load()
{
    QFile f(m_configPath);
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("kdock: config parse error: %s", qPrintable(err.errorString()));
        return;
    }
    m_config = doc.object();
}

void ConfigWatcher::onFileChanged(const QString &path)
{
    load();
    emit configChanged();
    // Re-add path in case the editor replaced the file (rename semantics)
    if (!m_watcher.files().contains(path))
        m_watcher.addPath(path);
}

// ── Accessors ──────────────────────────────────────────────────────────────

QString ConfigWatcher::position() const
{
    return m_config.value(QStringLiteral("position")).toString(QStringLiteral("bottom"));
}

int ConfigWatcher::iconSize() const
{
    return m_config.value(QStringLiteral("iconSize")).toInt(52);
}

int ConfigWatcher::padding() const
{
    return m_config.value(QStringLiteral("padding")).toInt(8);
}

int ConfigWatcher::spacing() const
{
    return m_config.value(QStringLiteral("spacing")).toInt(6);
}

int ConfigWatcher::screenIndex() const
{
    return m_config.value(QStringLiteral("screenIndex")).toInt(0);
}

bool ConfigWatcher::autohide() const
{
    return m_config.value(QStringLiteral("autohide")).toBool(false);
}

bool ConfigWatcher::magnify() const
{
    return m_config.value(QStringLiteral("magnify")).toBool(true);
}

double ConfigWatcher::magnifyScale() const
{
    return m_config.value(QStringLiteral("magnifyScale")).toDouble(1.5);
}

int ConfigWatcher::magnifyRadius() const
{
    return m_config.value(QStringLiteral("magnifyRadius")).toInt(120);
}

QString ConfigWatcher::backgroundColor() const
{
    const QJsonObject bg = m_config.value(QStringLiteral("background")).toObject();
    return bg.value(QStringLiteral("color")).toString(QStringLiteral("#1a1a2e"));
}

double ConfigWatcher::backgroundOpacity() const
{
    const QJsonObject bg = m_config.value(QStringLiteral("background")).toObject();
    return bg.value(QStringLiteral("opacity")).toDouble(0.85);
}

int ConfigWatcher::backgroundRadius() const
{
    const QJsonObject bg = m_config.value(QStringLiteral("background")).toObject();
    return bg.value(QStringLiteral("radius")).toInt(14);
}

bool ConfigWatcher::runningIndicatorVisible() const
{
    const QJsonObject ind = m_config.value(QStringLiteral("runningIndicator")).toObject();
    return ind.value(QStringLiteral("visible")).toBool(true);
}

QString ConfigWatcher::runningIndicatorColor() const
{
    const QJsonObject ind = m_config.value(QStringLiteral("runningIndicator")).toObject();
    return ind.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"));
}

int ConfigWatcher::runningIndicatorSize() const
{
    const QJsonObject ind = m_config.value(QStringLiteral("runningIndicator")).toObject();
    return ind.value(QStringLiteral("size")).toInt(4);
}

QStringList ConfigWatcher::pinnedApps() const
{
    const QJsonArray arr = m_config.value(QStringLiteral("pinned")).toArray();
    QStringList result;
    result.reserve(arr.size());
    for (const QJsonValue &v : arr)
        result << v.toString();
    return result;
}

void ConfigWatcher::setPinnedApps(const QStringList &apps)
{
    QJsonArray arr;
    for (const QString &app : apps)
        arr.append(app);
    m_config[QStringLiteral("pinned")] = arr;
}

void ConfigWatcher::save()
{
    QSaveFile f(m_configPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("kdock: cannot write config: %s", qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(m_config).toJson());
    f.commit();
}

QString ConfigWatcher::configPath() const
{
    return m_configPath;
}
