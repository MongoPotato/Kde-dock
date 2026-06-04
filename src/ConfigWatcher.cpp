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

    if (!QFile::exists(m_configPath))
        copyDefaultConfig();

    load();

    m_watcher.addPath(m_configPath);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ConfigWatcher::onFileChanged);
}

void ConfigWatcher::copyDefaultConfig()
{
    const QStringList candidates = {
        QStringLiteral(QML_INSTALL_DIR "/../default_dock.json"),
        QStringLiteral("data/default_dock.json"),
    };
    for (const QString &src : candidates) {
        if (QFile::exists(src)) {
            QFile::copy(src, m_configPath);
            return;
        }
    }

    // Built-in minimal default when no data file is found
    resetToDefaults();
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
    if (!m_watcher.files().contains(path))
        m_watcher.addPath(path);
}

// ── Position & geometry ────────────────────────────────────────────────────

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

// ── Dock bar background ────────────────────────────────────────────────────

QString ConfigWatcher::backgroundColor() const
{
    return m_config.value(QStringLiteral("background")).toObject()
        .value(QStringLiteral("color")).toString(QStringLiteral("#1a1a2e"));
}

double ConfigWatcher::backgroundOpacity() const
{
    return m_config.value(QStringLiteral("background")).toObject()
        .value(QStringLiteral("opacity")).toDouble(0.85);
}

int ConfigWatcher::backgroundRadius() const
{
    return m_config.value(QStringLiteral("background")).toObject()
        .value(QStringLiteral("radius")).toInt(14);
}

// ── Per-icon background ────────────────────────────────────────────────────

QString ConfigWatcher::iconBgShape() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("shape")).toString(QStringLiteral("none"));
}

QString ConfigWatcher::iconBgColor() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"));
}

double ConfigWatcher::iconBgOpacity() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("opacity")).toDouble(0.12);
}

int ConfigWatcher::iconBgPadding() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("padding")).toInt(6);
}

int ConfigWatcher::iconBgRadius() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("radius")).toInt(10);
}

QString ConfigWatcher::iconBgBorderColor() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("borderColor")).toString(QStringLiteral("#ffffff"));
}

int ConfigWatcher::iconBgBorderWidth() const
{
    return m_config.value(QStringLiteral("iconBackground")).toObject()
        .value(QStringLiteral("borderWidth")).toInt(0);
}

// ── Hover effects ──────────────────────────────────────────────────────────

int ConfigWatcher::hoverLiftPx() const
{
    return m_config.value(QStringLiteral("hover")).toObject()
        .value(QStringLiteral("liftPx")).toInt(6);
}

double ConfigWatcher::hoverScaleBoost() const
{
    return m_config.value(QStringLiteral("hover")).toObject()
        .value(QStringLiteral("scaleBoost")).toDouble(1.05);
}

double ConfigWatcher::hoverGlowOpacity() const
{
    return m_config.value(QStringLiteral("hover")).toObject()
        .value(QStringLiteral("glowOpacity")).toDouble(0.35);
}

QString ConfigWatcher::hoverGlowColor() const
{
    return m_config.value(QStringLiteral("hover")).toObject()
        .value(QStringLiteral("glowColor")).toString(QStringLiteral("auto"));
}

// ── Scroll-to-zoom ─────────────────────────────────────────────────────────

int ConfigWatcher::scrollStepPx() const
{
    return m_config.value(QStringLiteral("scroll")).toObject()
        .value(QStringLiteral("stepPx")).toInt(4);
}

int ConfigWatcher::scrollMinSize() const
{
    return m_config.value(QStringLiteral("scroll")).toObject()
        .value(QStringLiteral("minSize")).toInt(24);
}

int ConfigWatcher::scrollMaxSize() const
{
    return m_config.value(QStringLiteral("scroll")).toObject()
        .value(QStringLiteral("maxSize")).toInt(128);
}

// ── Behaviour ──────────────────────────────────────────────────────────────

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

// ── Running indicator ──────────────────────────────────────────────────────

bool ConfigWatcher::runningIndicatorVisible() const
{
    return m_config.value(QStringLiteral("runningIndicator")).toObject()
        .value(QStringLiteral("visible")).toBool(true);
}

QString ConfigWatcher::runningIndicatorColor() const
{
    return m_config.value(QStringLiteral("runningIndicator")).toObject()
        .value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"));
}

int ConfigWatcher::runningIndicatorSize() const
{
    return m_config.value(QStringLiteral("runningIndicator")).toObject()
        .value(QStringLiteral("size")).toInt(4);
}

// ── Pinned apps ────────────────────────────────────────────────────────────

QStringList ConfigWatcher::pinnedApps() const
{
    const QJsonArray arr = m_config.value(QStringLiteral("pinned")).toArray();
    QStringList result;
    result.reserve(arr.size());
    for (const QJsonValue &v : arr)
        result << v.toString();
    return result;
}

// ── Mutators ───────────────────────────────────────────────────────────────

void ConfigWatcher::setIconSize(int px)
{
    const int clamped = qBound(scrollMinSize(), px, scrollMaxSize());
    if (clamped == iconSize())
        return;
    m_config[QStringLiteral("iconSize")] = clamped;
    emit configChanged();
    save();
}

void ConfigWatcher::setMagnifyScale(double s)
{
    m_config[QStringLiteral("magnifyScale")] = s;
    emit configChanged();
    save();
}

void ConfigWatcher::setHoverLiftPx(int px)
{
    QJsonObject hover = m_config.value(QStringLiteral("hover")).toObject();
    hover[QStringLiteral("liftPx")] = px;
    m_config[QStringLiteral("hover")] = hover;
    emit configChanged();
    save();
}

void ConfigWatcher::setBackgroundOpacity(double v)
{
    QJsonObject bg = m_config.value(QStringLiteral("background")).toObject();
    bg[QStringLiteral("opacity")] = v;
    m_config[QStringLiteral("background")] = bg;
    emit configChanged();
    save();
}

void ConfigWatcher::setAutohide(bool on)
{
    m_config[QStringLiteral("autohide")] = on;
    emit configChanged();
    save();
}

void ConfigWatcher::setIconBgShape(const QString &shape)
{
    QJsonObject ib = m_config.value(QStringLiteral("iconBackground")).toObject();
    ib[QStringLiteral("shape")] = shape;
    m_config[QStringLiteral("iconBackground")] = ib;
    emit configChanged();
    save();
}

void ConfigWatcher::setIconBgOpacity(double v)
{
    QJsonObject ib = m_config.value(QStringLiteral("iconBackground")).toObject();
    ib[QStringLiteral("opacity")] = v;
    m_config[QStringLiteral("iconBackground")] = ib;
    emit configChanged();
    save();
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

void ConfigWatcher::reload()
{
    load();
    emit configChanged();
}

void ConfigWatcher::resetToDefaults()
{
    m_config = QJsonObject{
        {QStringLiteral("pinned"), QJsonArray{
            QStringLiteral("org.kde.dolphin"),
            QStringLiteral("org.kde.konsole"),
            QStringLiteral("org.mozilla.firefox"),
            QStringLiteral("org.kde.kate"),
        }},
        {QStringLiteral("position"), QStringLiteral("bottom")},
        {QStringLiteral("iconSize"), 52},
        {QStringLiteral("padding"), 8},
        {QStringLiteral("spacing"), 6},
        {QStringLiteral("screenIndex"), 0},
        {QStringLiteral("background"), QJsonObject{
            {QStringLiteral("color"),   QStringLiteral("#1a1a2e")},
            {QStringLiteral("opacity"), 0.85},
            {QStringLiteral("radius"),  14},
        }},
        {QStringLiteral("iconBackground"), QJsonObject{
            {QStringLiteral("shape"),       QStringLiteral("none")},
            {QStringLiteral("color"),       QStringLiteral("#ffffff")},
            {QStringLiteral("opacity"),     0.12},
            {QStringLiteral("padding"),     6},
            {QStringLiteral("radius"),      10},
            {QStringLiteral("borderColor"), QStringLiteral("#ffffff")},
            {QStringLiteral("borderWidth"), 0},
        }},
        {QStringLiteral("hover"), QJsonObject{
            {QStringLiteral("liftPx"),      6},
            {QStringLiteral("scaleBoost"),  1.05},
            {QStringLiteral("glowOpacity"), 0.35},
            {QStringLiteral("glowColor"),   QStringLiteral("auto")},
        }},
        {QStringLiteral("scroll"), QJsonObject{
            {QStringLiteral("stepPx"),  4},
            {QStringLiteral("minSize"), 24},
            {QStringLiteral("maxSize"), 128},
        }},
        {QStringLiteral("autohide"),      false},
        {QStringLiteral("magnify"),       true},
        {QStringLiteral("magnifyScale"),  1.5},
        {QStringLiteral("magnifyRadius"), 120},
        {QStringLiteral("runningIndicator"), QJsonObject{
            {QStringLiteral("visible"), true},
            {QStringLiteral("color"),   QStringLiteral("#ffffff")},
            {QStringLiteral("size"),    4},
        }},
    };
    emit configChanged();
    save();
}

QString ConfigWatcher::configPath() const
{
    return m_configPath;
}

// ── Visual extras ──────────────────────────────────────────────────────────

bool ConfigWatcher::blurEnabled() const
{
    return m_config.value(QStringLiteral("blurEnabled")).toBool(false);
}

bool ConfigWatcher::adaptiveColor() const
{
    return m_config.value(QStringLiteral("adaptiveColor")).toBool(false);
}

void ConfigWatcher::setBlurEnabled(bool on)
{
    m_config[QStringLiteral("blurEnabled")] = on;
    emit configChanged();
    save();
}

void ConfigWatcher::setAdaptiveColor(bool on)
{
    m_config[QStringLiteral("adaptiveColor")] = on;
    emit configChanged();
    save();
}

void ConfigWatcher::setPosition(const QString &pos)
{
    m_config[QStringLiteral("position")] = pos;
    emit configChanged();
    save();
}

// ── Preset management ──────────────────────────────────────────────────────

QString ConfigWatcher::presetsDir() const
{
    const QString configDir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    return configDir + QStringLiteral("/presets");
}

QStringList ConfigWatcher::presetNames() const
{
    const QDir dir(presetsDir());
    QStringList names;
    for (const QString &fn : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        names << fn.chopped(5);  // strip ".json"
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ConfigWatcher::savePreset(const QString &name)
{
    if (name.trimmed().isEmpty()) return false;
    QDir().mkpath(presetsDir());

    // Store only the app list and position in a preset
    QJsonObject preset;
    preset[QStringLiteral("pinned")]   = m_config.value(QStringLiteral("pinned"));
    preset[QStringLiteral("position")] = m_config.value(QStringLiteral("position"));

    QSaveFile f(presetsDir() + QChar('/') + name + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(preset).toJson());
    if (!f.commit()) return false;

    if (m_activePreset != name) {
        m_activePreset = name;
        emit presetChanged();
    }
    return true;
}

bool ConfigWatcher::loadPreset(const QString &name)
{
    const QString path = presetsDir() + QChar('/') + name + QStringLiteral(".json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) return false;

    const QJsonObject preset = doc.object();
    if (preset.contains(QStringLiteral("pinned")))
        m_config[QStringLiteral("pinned")] = preset.value(QStringLiteral("pinned"));
    if (preset.contains(QStringLiteral("position")))
        m_config[QStringLiteral("position")] = preset.value(QStringLiteral("position"));

    m_activePreset = name;
    emit presetChanged();
    emit configChanged();
    save();
    return true;
}

bool ConfigWatcher::deletePreset(const QString &name)
{
    const QString path = presetsDir() + QChar('/') + name + QStringLiteral(".json");
    const bool ok = QFile::remove(path);
    if (ok && m_activePreset == name) {
        m_activePreset = QStringLiteral("Default");
        emit presetChanged();
    }
    return ok;
}

QString ConfigWatcher::activePreset() const
{
    return m_activePreset;
}
