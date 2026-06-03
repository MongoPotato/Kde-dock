#pragma once

// IconThemeDetector reads the active KDE icon theme from ~/.config/kdeglobals
// and applies it to QIcon::setThemeName() so all fromTheme() calls pick the
// right icons.  A QFileSystemWatcher fires when KDE's System Settings writes
// a new theme choice so the dock updates without a restart.

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

class IconThemeDetector : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY themeChanged)

public:
    explicit IconThemeDetector(QObject *parent = nullptr);

    QString currentTheme() const;

    // Re-reads kdeglobals and re-applies the theme immediately.
    // Called from the settings panel "Reload" button.
    Q_INVOKABLE void redetect();

signals:
    void themeChanged(const QString &themeName);

private:
    void detect();

    QString m_currentTheme;
    QFileSystemWatcher m_watcher;
    QString m_kdeglobalsPath;
};
