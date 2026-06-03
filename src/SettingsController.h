#pragma once

// SettingsController is a thin validation + persistence layer between
// the QML settings panel and ConfigWatcher.
// Every setter clamps its input before writing so the UI cannot produce
// a corrupt config file regardless of what value a slider emits.
// All changes write to disk immediately via ConfigWatcher::save() so
// that killing the process never loses a setting change.

#include <QObject>
#include <QString>

class ConfigWatcher;
class IconThemeDetector;

class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentIconTheme READ currentIconTheme NOTIFY iconThemeChanged)

public:
    explicit SettingsController(ConfigWatcher *config,
                                 IconThemeDetector *themeDetector,
                                 QObject *parent = nullptr);

    QString currentIconTheme() const;

    Q_INVOKABLE void applyIconSize(int px);
    Q_INVOKABLE void applyMagnifyScale(double s);
    Q_INVOKABLE void applyHoverLift(int px);
    Q_INVOKABLE void applyDockOpacity(double v);
    Q_INVOKABLE void applyAutohide(bool on);
    Q_INVOKABLE void applyIconBgShape(const QString &shape);
    Q_INVOKABLE void applyIconBgOpacity(double v);
    Q_INVOKABLE void resetDefaults();

signals:
    void iconThemeChanged();

private:
    ConfigWatcher *m_config;
    IconThemeDetector *m_themeDetector;
};
