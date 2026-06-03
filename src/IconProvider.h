#pragma once

// IconProvider implements a two-level lookup so users can override any
// icon by dropping a file with the app's ID as the filename into their
// icons/ folder.  No restart required — QML image cache is bypassed
// by appending a ?v=<mtime> query to the source URL when a change is
// detected by ConfigWatcher.

#include <QQuickImageProvider>
#include <QString>

class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString m_iconsDir;
};
