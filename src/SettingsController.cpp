// SettingsController is a thin validation + persistence layer between
// the QML settings panel and ConfigWatcher.
// Every setter clamps its input before writing so the UI cannot produce
// a corrupt config file regardless of what value a slider emits.
// All changes write to disk immediately via ConfigWatcher::save() so
// that killing the process never loses a setting change.

#include "SettingsController.h"
#include "ConfigWatcher.h"
#include "IconThemeDetector.h"

#include <QJsonObject>

SettingsController::SettingsController(ConfigWatcher *config,
                                       IconThemeDetector *themeDetector,
                                       QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_themeDetector(themeDetector)
{
    connect(themeDetector, &IconThemeDetector::themeChanged,
            this, &SettingsController::iconThemeChanged);
}

QString SettingsController::currentIconTheme() const
{
    return m_themeDetector->currentTheme();
}

void SettingsController::applyIconSize(int px)
{
    m_config->setIconSize(qBound(16, px, 256));
}

void SettingsController::applyMagnifyScale(double s)
{
    m_config->setMagnifyScale(qBound(1.0, s, 3.0));
}

void SettingsController::applyHoverLift(int px)
{
    m_config->setHoverLiftPx(qBound(0, px, 40));
}

void SettingsController::applyDockOpacity(double v)
{
    m_config->setBackgroundOpacity(qBound(0.1, v, 1.0));
}

void SettingsController::applyAutohide(bool on)
{
    m_config->setAutohide(on);
}

void SettingsController::applyIconBgShape(const QString &shape)
{
    static const QStringList valid{
        QStringLiteral("none"),
        QStringLiteral("circle"),
        QStringLiteral("pill"),
        QStringLiteral("squircle"),
    };
    if (valid.contains(shape))
        m_config->setIconBgShape(shape);
}

void SettingsController::applyIconBgOpacity(double v)
{
    m_config->setIconBgOpacity(qBound(0.0, v, 1.0));
}

void SettingsController::resetDefaults()
{
    m_config->resetToDefaults();
}
