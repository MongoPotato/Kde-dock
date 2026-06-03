// Tests for ConfigWatcher: reading, writing, hot-reload, validation, defaults.

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>

#include "../src/ConfigWatcher.h"
#include "helpers/TempConfigDir.h"

class TestConfigWatcher : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<TempConfigDir> m_tmp;

private slots:

    void init()
    {
        m_tmp = std::make_unique<TempConfigDir>();
        // Remove any dock.json left by a previous test run so each test
        // starts from a clean slate.
        const QString configDir = QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
        QFile::remove(configDir + QStringLiteral("/dock.json"));
    }
    void cleanup() { m_tmp.reset(); }

    // Verify that a missing dock.json is created from built-in defaults on construction
    void test_createsDefaultConfigWhenMissing()
    {
        ConfigWatcher cw;
        QVERIFY(QFile::exists(cw.configPath()));
        QCOMPARE(cw.position(), QStringLiteral("bottom"));
        QCOMPARE(cw.iconSize(), 52);
    }

    // Verify that a valid dock.json is parsed and all Q_PROPERTYs have correct values
    void test_parsesValidConfig()
    {
        // Write a custom config before constructing ConfigWatcher
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"position":"top","iconSize":64,"padding":12})");
        f.close();

        ConfigWatcher cw;
        QCOMPARE(cw.position(), QStringLiteral("top"));
        QCOMPARE(cw.iconSize(), 64);
        QCOMPARE(cw.padding(), 12);
    }

    // Verify that an unknown JSON key is silently ignored (forward compatibility)
    void test_ignoresUnknownKeys()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":48,"futureFeature":{"foo":"bar"}})");
        f.close();

        ConfigWatcher cw;
        QCOMPARE(cw.iconSize(), 48);
        // No crash; unknown key is ignored
    }

    // Verify that a corrupt JSON file falls back to defaults without crashing
    void test_fallsBackOnCorruptJson()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not valid json !!!");
        f.close();

        ConfigWatcher cw;
        // Should not crash; iconSize falls back to built-in default
        QVERIFY(cw.iconSize() > 0);
    }

    // Verify that setIconSize() clamps below scrollMinSize to scrollMinSize
    void test_setIconSize_clampsToMin()
    {
        ConfigWatcher cw;
        cw.setIconSize(1);
        QCOMPARE(cw.iconSize(), cw.scrollMinSize());
    }

    // Verify that setIconSize() clamps above scrollMaxSize to scrollMaxSize
    void test_setIconSize_clampsToMax()
    {
        ConfigWatcher cw;
        cw.setIconSize(9999);
        QCOMPARE(cw.iconSize(), cw.scrollMaxSize());
    }

    // Verify that save() writes valid JSON that can be re-parsed
    void test_saveProducesValidJson()
    {
        ConfigWatcher cw;
        cw.setIconSize(60);
        cw.save();

        QFile f(cw.configPath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QCOMPARE(doc.object().value(QStringLiteral("iconSize")).toInt(), 60);
    }

    // Verify that save() is atomic: no .tmp file is left behind after a successful save
    void test_saveIsAtomic()
    {
        ConfigWatcher cw;
        cw.setIconSize(55);
        cw.save();

        const QString tmpPath = cw.configPath() + QStringLiteral(".tmp");
        QVERIFY(!QFile::exists(tmpPath));
    }

    // Verify that modifying dock.json on disk triggers configChanged() signal
    void test_hotReloadEmitsSignal()
    {
        ConfigWatcher cw;
        QSignalSpy spy(&cw, &ConfigWatcher::configChanged);

        // Overwrite the file on disk
        QFile f(cw.configPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":77})");
        f.close();

        // Allow event loop to deliver the file-change event
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(cw.iconSize(), 77);
    }

    // Verify that configChanged() is NOT emitted for a change to an unrelated file
    void test_hotReloadIgnoresOtherFiles()
    {
        ConfigWatcher cw;
        QSignalSpy spy(&cw, &ConfigWatcher::configChanged);

        const QString otherPath = cw.configPath() + QStringLiteral(".unrelated");
        QFile f(otherPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("garbage");
        f.close();

        QTest::qWait(200);
        QCOMPARE(spy.count(), 0);
    }

    // Verify that setPinnedApps() + save() persists the new pin list
    void test_pinApp_addsAndPersists()
    {
        ConfigWatcher cw;
        QStringList pinned = cw.pinnedApps();
        pinned.append(QStringLiteral("com.example.test"));
        cw.setPinnedApps(pinned);
        cw.save();

        ConfigWatcher cw2;
        QVERIFY(cw2.pinnedApps().contains(QStringLiteral("com.example.test")));
    }

    // Verify that removing an appId from pinnedApps and saving persists the removal
    void test_unpinApp_removesEntry()
    {
        ConfigWatcher cw;
        QStringList pinned = cw.pinnedApps();
        const int countBefore = pinned.size();
        if (pinned.isEmpty())
            QSKIP("Default config has no pinned apps to test removal");

        const QString toRemove = pinned.takeFirst();
        cw.setPinnedApps(pinned);
        cw.save();

        ConfigWatcher cw2;
        QCOMPARE(cw2.pinnedApps().size(), countBefore - 1);
        QVERIFY(!cw2.pinnedApps().contains(toRemove));
    }

    // Verify that removing an appId that is not in the list leaves the list unchanged
    void test_unpinApp_noopWhenNotPinned()
    {
        ConfigWatcher cw;
        const QStringList before = cw.pinnedApps();
        QStringList copy = before;
        copy.removeAll(QStringLiteral("com.example.not.pinned"));
        cw.setPinnedApps(copy);
        cw.save();

        ConfigWatcher cw2;
        QCOMPARE(cw2.pinnedApps(), before);
    }
};

QTEST_MAIN(TestConfigWatcher)
#include "tst_ConfigWatcher.moc"
