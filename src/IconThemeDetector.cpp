// IconThemeDetector reads the active KDE icon theme from ~/.config/kdeglobals
// and applies it to QIcon::setThemeName() so all fromTheme() calls pick the
// right icons.  A QFileSystemWatcher fires when KDE's System Settings writes
// a new theme choice so the dock updates without a restart.

#include "IconThemeDetector.h"

#include <QFile>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>

IconThemeDetector::IconThemeDetector(QObject *parent)
    : QObject(parent)
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_kdeglobalsPath = configDir + QStringLiteral("/kdeglobals");

    detect();

    if (QFile::exists(m_kdeglobalsPath)) {
        m_watcher.addPath(m_kdeglobalsPath);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
            detect();
            // Re-add the path — editors often replace files via rename
            if (!m_watcher.files().contains(path))
                m_watcher.addPath(path);
        });
    }
}

QString IconThemeDetector::currentTheme() const
{
    return m_currentTheme;
}

void IconThemeDetector::redetect()
{
    detect();
}

void IconThemeDetector::detect()
{
    QString theme = QStringLiteral("breeze");

    if (QFile::exists(m_kdeglobalsPath)) {
        QSettings kdeGlobals(m_kdeglobalsPath, QSettings::IniFormat);
        kdeGlobals.beginGroup(QStringLiteral("Icons"));
        theme = kdeGlobals.value(QStringLiteral("Theme"), QStringLiteral("breeze")).toString();
        kdeGlobals.endGroup();
    }

    if (theme == m_currentTheme)
        return;

    m_currentTheme = theme;
    QIcon::setThemeName(m_currentTheme);
    emit themeChanged(m_currentTheme);
}
