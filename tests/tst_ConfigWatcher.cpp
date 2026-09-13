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

    // A config predating autohideDelayMs must still get a usable, generous
    // delay rather than 0 — a 0 ms delay hides the dock the instant the
    // cursor clips its edge.
    void test_autohideDelayDefaultsWhenAbsent()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"autohide":true})");
        f.close();

        ConfigWatcher cw;
        QCOMPARE(cw.autohide(), true);
        QCOMPARE(cw.autohideDelayMs(), 2500);
    }

    // An explicit autohideDelayMs in the file must win over the default
    void test_autohideDelayReadFromConfig()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"autohideDelayMs":4000})");
        f.close();

        ConfigWatcher cw;
        QCOMPARE(cw.autohideDelayMs(), 4000);
    }

    // setAutohideDelayMs() must persist so the value survives a reload
    void test_setAutohideDelayMs_persists()
    {
        ConfigWatcher cw;
        cw.setAutohideDelayMs(3750);
        cw.reload();
        QCOMPARE(cw.autohideDelayMs(), 3750);
    }

    // resetToDefaults() must leave the delay at the built-in default
    void test_resetToDefaults_restoresAutohideDelay()
    {
        ConfigWatcher cw;
        cw.setAutohideDelayMs(9000);
        cw.resetToDefaults();
        QCOMPARE(cw.autohideDelayMs(), 2500);
    }

    // ── Derived dock geometry ─────────────────────────────────────────────
    //
    // The regression these guard: the reserved strip used to be computed as
    // iconSize + padding * 2, which is SHORTER than the icon row it contains
    // (the row adds its own icon-background padding and sits `padding` off the
    // edge). Windows were then laid out over the top of the icons.

    // The painted dock must be at least as thick as the row of items in it
    void test_visualThicknessCoversIconRow()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":52,"padding":8,"iconBackground":{"padding":6}})");
        f.close();

        ConfigWatcher cw;
        // Row extent: padding + iconSize + iconBgPadding * 2 + 4 = 8 + 68 = 76,
        // against a background strip of iconSize + padding * 2 = 68.
        QCOMPARE(cw.dockVisualThickness(), 76);
        QVERIFY(cw.dockVisualThickness() >= cw.iconSize() + cw.padding() * 2);
    }

    // A large icon-background padding must widen the dock, not overflow it
    void test_visualThicknessFollowsIconBgPadding()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":40,"padding":4,"iconBackground":{"padding":20}})");
        f.close();

        ConfigWatcher cw;
        // 4 + 40 + 40 + 4 = 88, comfortably past the 48 px background strip
        QCOMPARE(cw.dockVisualThickness(), 88);
    }

    // Tiny icon backgrounds must not shrink the dock below its own background
    void test_visualThicknessNeverBelowBackgroundStrip()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":48,"padding":20,"iconBackground":{"padding":0}})");
        f.close();

        ConfigWatcher cw;
        // Strip 48 + 40 = 88 wins over the row's 20 + 52 = 72
        QCOMPARE(cw.dockVisualThickness(), 88);
    }

    // Reserved space must include the hover lift, so a raised icon stays
    // inside the dock's own space instead of over the window behind it
    void test_reservedThicknessIncludesHoverLift()
    {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(configDir);
        QFile f(configDir + QStringLiteral("/dock.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"iconSize":52,"padding":8,"iconBackground":{"padding":6},"hover":{"liftPx":10}})");
        f.close();

        ConfigWatcher cw;
        QCOMPARE(cw.dockReservedThickness(), cw.dockVisualThickness() + 10);
        QCOMPARE(cw.dockReservedThickness(), 86);
    }

    // The window is taller than the reserved strip (click-bounce headroom),
    // and that ordering is what keeps the bounce from being clipped
    void test_windowThicknessExceedsReserved()
    {
        ConfigWatcher cw;
        QVERIFY(cw.dockWindowThickness() > cw.dockReservedThickness());
        QVERIFY(cw.dockReservedThickness() >= cw.dockVisualThickness());
    }

    // ── reserveSpace ──────────────────────────────────────────────────────

    // Defaults to on: a dock that reserves nothing sits over other windows
    void test_reserveSpaceDefaultsOn()
    {
        ConfigWatcher cw;
        QCOMPARE(cw.reserveSpace(), true);
    }

    // ...and survives a reload once turned off
    void test_setReserveSpace_persists()
    {
        ConfigWatcher cw;
        cw.setReserveSpace(false);
        cw.reload();
        QCOMPARE(cw.reserveSpace(), false);
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

    // Verify that reload() emits configChanged()
    void test_reload_emitsConfigChanged()
    {
        ConfigWatcher cw;
        QSignalSpy spy(&cw, &ConfigWatcher::configChanged);
        cw.reload();
        QCOMPARE(spy.count(), 1);
    }

    // Verify that setIconSize() emits configChanged() once for a new value
    void test_setIconSize_emitsConfigChanged()
    {
        ConfigWatcher cw;
        QSignalSpy spy(&cw, &ConfigWatcher::configChanged);
        const int newSize = (cw.iconSize() == cw.scrollMinSize() + 4)
                            ? cw.scrollMinSize() + 8
                            : cw.scrollMinSize() + 4;
        cw.setIconSize(newSize);
        QCOMPARE(spy.count(), 1);
    }

    // Verify that setIconSize() does NOT emit configChanged() when the value is unchanged
    void test_setIconSize_noSignalWhenUnchanged()
    {
        ConfigWatcher cw;
        QSignalSpy spy(&cw, &ConfigWatcher::configChanged);
        cw.setIconSize(cw.iconSize());
        QCOMPARE(spy.count(), 0);
    }

    // Verify that default background values are internally consistent and valid
    void test_backgroundDefaults_areValid()
    {
        ConfigWatcher cw;
        QVERIFY(cw.backgroundOpacity() > 0.0);
        QVERIFY(cw.backgroundOpacity() <= 1.0);
        QVERIFY(!cw.backgroundColor().isEmpty());
        QVERIFY(cw.backgroundRadius() >= 0);
    }

    // Verify that scroll bounds are ordered: minSize < maxSize
    void test_scrollBounds_areOrdered()
    {
        ConfigWatcher cw;
        QVERIFY(cw.scrollMinSize() < cw.scrollMaxSize());
    }

    // Verify that padding and spacing defaults are non-negative
    void test_paddingAndSpacing_areNonNegative()
    {
        ConfigWatcher cw;
        QVERIFY(cw.padding() >= 0);
        QVERIFY(cw.spacing() >= 0);
    }

    // Verify that setAutohide() persists the value across a save+reload cycle
    void test_setAutohide_persistsAcrossReload()
    {
        ConfigWatcher cw;
        cw.setAutohide(true);
        cw.save();

        ConfigWatcher cw2;
        QCOMPARE(cw2.autohide(), true);
    }
};

QTEST_MAIN(TestConfigWatcher)
#include "tst_ConfigWatcher.moc"
