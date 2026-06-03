// Tests for IconThemeDetector: parsing kdeglobals, applying theme, hot-reload.

#include <QtTest>
#include <QDir>
#include <QIcon>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>

#include "../src/IconThemeDetector.h"
#include "helpers/TempConfigDir.h"

// Write a kdeglobals file with the given theme name (empty string = no [Icons] group)
static void writeKdeglobals(const QString &configDir, const QString &theme)
{
    QDir().mkpath(configDir);
    QFile f(configDir + QStringLiteral("/kdeglobals"));
    f.open(QIODevice::WriteOnly);
    if (!theme.isEmpty())
        f.write(("[Icons]\nTheme=" + theme + "\n").toUtf8());
    else
        f.write("[General]\nfoo=bar\n");
}

class TestIconThemeDetector : public QObject {
    Q_OBJECT

    std::unique_ptr<TempConfigDir> m_tmp;
    QString m_configDir;

private slots:

    void init()
    {
        m_tmp = std::make_unique<TempConfigDir>();
        m_configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }

    void cleanup() { m_tmp.reset(); }

    // Verify that the correct theme name is read from a valid kdeglobals file
    void test_detectsThemeFromKdeglobals()
    {
        writeKdeglobals(m_configDir, QStringLiteral("Papirus"));
        IconThemeDetector detector;
        QCOMPARE(detector.currentTheme(), QStringLiteral("Papirus"));
    }

    // Verify that "breeze" is returned when kdeglobals has no [Icons] group
    void test_defaultsBreezeWhenNoIconsGroup()
    {
        writeKdeglobals(m_configDir, QString());
        IconThemeDetector detector;
        QCOMPARE(detector.currentTheme(), QStringLiteral("breeze"));
    }

    // Verify that "breeze" is returned when kdeglobals does not exist
    void test_defaultsBreezeWhenFileMissing()
    {
        // Ensure file does NOT exist
        QFile::remove(m_configDir + QStringLiteral("/kdeglobals"));
        IconThemeDetector detector;
        QCOMPARE(detector.currentTheme(), QStringLiteral("breeze"));
    }

    // Verify that QIcon::themeName() is updated after detection
    void test_appliesThemeToQIcon()
    {
        writeKdeglobals(m_configDir, QStringLiteral("Adwaita"));
        IconThemeDetector detector;
        QCOMPARE(QIcon::themeName(), QStringLiteral("Adwaita"));
    }

    // Verify that themeChanged() signal is emitted when kdeglobals is updated on disk
    void test_emitsThemeChangedOnFileUpdate()
    {
        writeKdeglobals(m_configDir, QStringLiteral("Breeze"));
        IconThemeDetector detector;

        QSignalSpy spy(&detector, &IconThemeDetector::themeChanged);

        // Change the theme in the file
        writeKdeglobals(m_configDir, QStringLiteral("Papirus-Dark"));

        // Allow the file watcher event to be delivered
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("Papirus-Dark"));
        QCOMPARE(detector.currentTheme(), QStringLiteral("Papirus-Dark"));
    }

    // Verify that themeChanged() is NOT emitted when the file changes but the theme value is unchanged
    void test_noSignalWhenThemeUnchanged()
    {
        writeKdeglobals(m_configDir, QStringLiteral("Breeze"));
        IconThemeDetector detector;

        QSignalSpy spy(&detector, &IconThemeDetector::themeChanged);

        // Rewrite the file with the same theme — only a comment changes
        QFile f(m_configDir + QStringLiteral("/kdeglobals"));
        f.open(QIODevice::WriteOnly);
        f.write("[Icons]\nTheme=Breeze\n# a comment\n");
        f.close();

        QTest::qWait(300);
        QCOMPARE(spy.count(), 0);
    }

    // Verify that redetect() re-reads the file and updates the theme immediately
    void test_redetectIsImmediate()
    {
        writeKdeglobals(m_configDir, QStringLiteral("Breeze"));
        IconThemeDetector detector;

        // Update file without waiting for watcher
        writeKdeglobals(m_configDir, QStringLiteral("Tela"));
        detector.redetect();

        QCOMPARE(detector.currentTheme(), QStringLiteral("Tela"));
    }
};

QTEST_MAIN(TestIconThemeDetector)
#include "tst_IconThemeDetector.moc"
