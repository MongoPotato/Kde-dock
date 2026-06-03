// IconProvider implements a two-level lookup so users can override any
// icon by dropping a file with the app's ID as the filename into their
// icons/ folder.  No restart required — QML image cache is bypassed
// by appending a ?v=<mtime> query to the source URL when a change is
// detected by ConfigWatcher.

#include "IconProvider.h"

#include <QFile>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QStandardPaths>

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    m_iconsDir = configDir + QStringLiteral("/icons");
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Strip cache-buster query (?v=...) if present
    const QString appId = id.section(QChar('?'), 0, 0);

    const QSize target = requestedSize.isValid() ? requestedSize : QSize(64, 64);

    // 1. Custom PNG override
    const QString pngPath = m_iconsDir + QChar('/') + appId + QStringLiteral(".png");
    if (QFile::exists(pngPath)) {
        QImage img(pngPath);
        if (!img.isNull()) {
            img = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (size) *size = img.size();
            return img;
        }
    }

    // 2. Custom SVG override
    const QString svgPath = m_iconsDir + QChar('/') + appId + QStringLiteral(".svg");
    if (QFile::exists(svgPath)) {
        QImage img(svgPath);
        if (!img.isNull()) {
            img = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (size) *size = img.size();
            return img;
        }
    }

    // 3. System icon theme
    QIcon icon = QIcon::fromTheme(appId);
    if (!icon.isNull()) {
        const QPixmap px = icon.pixmap(target);
        if (!px.isNull()) {
            const QImage img = px.toImage();
            if (size) *size = img.size();
            return img;
        }
    }

    // 4. Generic application icon fallback
    QIcon fallback = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    if (!fallback.isNull()) {
        const QPixmap px = fallback.pixmap(target);
        if (!px.isNull()) {
            const QImage img = px.toImage();
            if (size) *size = img.size();
            return img;
        }
    }

    // Ultimate fallback: transparent image
    if (size) *size = target;
    return QImage(target, QImage::Format_ARGB32);
}
