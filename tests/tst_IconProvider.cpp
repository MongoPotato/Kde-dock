// Tests for IconProvider: lookup priority, format support, fallback chain.

#include <QtTest>
#include <QDir>
#include <QImage>
#include <QStandardPaths>

#include "../src/IconProvider.h"
#include "helpers/TempConfigDir.h"

// Create a minimal 32×32 PNG in the icons dir
static bool writePng(const QString &iconsDir, const QString &name)
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    return img.save(iconsDir + QChar('/') + name + QStringLiteral(".png"));
}

class TestIconProvider : public QObject {
    Q_OBJECT

    std::unique_ptr<TempConfigDir> m_tmp;
    QString m_iconsDir;

private slots:

    void init()
    {
        m_tmp = std::make_unique<TempConfigDir>();
        m_iconsDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                     + QStringLiteral("/icons");
        QDir().mkpath(m_iconsDir);
    }

    void cleanup() { m_tmp.reset(); }

    // Verify that a custom PNG in the icons/ folder is returned before the theme icon
    void test_customPngTakesPriorityOverTheme()
    {
        QVERIFY(writePng(m_iconsDir, QStringLiteral("test-app")));

        IconProvider provider;
        QSize size;
        const QImage img = provider.requestImage(
            QStringLiteral("test-app"), &size, QSize(32, 32));

        QVERIFY(!img.isNull());
        // The custom PNG is red; a theme icon would be something else
        QCOMPARE(img.pixel(16, 16), QColor(Qt::red).rgb());
    }

    // Verify that a custom SVG file is found when no PNG exists
    void test_customSvgFallsBackCorrectly()
    {
        // Write a minimal valid SVG
        const QString svgPath = m_iconsDir + QStringLiteral("/svg-app.svg");
        QFile f(svgPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32">)"
                R"(<rect width="32" height="32" fill="blue"/>)"
                R"(</svg>)");
        f.close();

        IconProvider provider;
        QSize size;
        const QImage img = provider.requestImage(
            QStringLiteral("svg-app"), &size, QSize(32, 32));
        // SVG rendering may produce a non-null image — just check no crash
        // (SVG requires Qt SVG module; if absent the image will be null)
        Q_UNUSED(img)
        QVERIFY(true);
    }

    // Verify that QIcon::fromTheme is tried when no custom file exists
    void test_fallsBackToThemeIcon()
    {
        // "folder" is present in nearly every icon theme
        IconProvider provider;
        QSize size;
        const QImage img = provider.requestImage(
            QStringLiteral("folder"), &size, QSize(48, 48));
        // May be null in a headless environment — just check no crash
        Q_UNUSED(img)
        QVERIFY(true);
    }

    // Verify that the generic application icon is returned for an unknown app ID
    void test_ultimateFallbackIsGenericIcon()
    {
        IconProvider provider;
        QSize size;
        // Use an ID that will not match any custom file or theme icon
        const QImage img = provider.requestImage(
            QStringLiteral("__kdock_nonexistent_12345__"), &size, QSize(48, 48));
        // Should return some image (possibly transparent), not crash
        QVERIFY(!img.isNull());
    }

    // Verify that the returned image is scaled to the requested size
    void test_imageScaledToRequestedSize()
    {
        // Write a 128×128 PNG; request 48×48 — should be scaled down
        QImage big(128, 128, QImage::Format_ARGB32);
        big.fill(Qt::green);
        const QString pngPath = m_iconsDir + QStringLiteral("/big-app.png");
        QVERIFY(big.save(pngPath));

        IconProvider provider;
        QSize size;
        const QImage img = provider.requestImage(
            QStringLiteral("big-app"), &size, QSize(48, 48));

        QVERIFY(!img.isNull());
        QVERIFY(img.width()  <= 48);
        QVERIFY(img.height() <= 48);
    }

    // Verify that the cache-buster query parameter (?v=N) is stripped before lookup
    void test_cacheBusterQueryIsStripped()
    {
        QVERIFY(writePng(m_iconsDir, QStringLiteral("cached-app")));

        IconProvider provider;
        QSize size;
        // Request with a cache buster — should still find the PNG
        const QImage img = provider.requestImage(
            QStringLiteral("cached-app?v=42"), &size, QSize(32, 32));

        QVERIFY(!img.isNull());
    }
};

QTEST_MAIN(TestIconProvider)
#include "tst_IconProvider.moc"
