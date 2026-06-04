// Tests for DockModel: merge logic, role data, pin/unpin, running app injection.

#include <QtTest>
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>

#include "../src/ConfigWatcher.h"
#include "../src/DockModel.h"
#include "helpers/TempConfigDir.h"

// Write a minimal dock.json with the given pinned apps list
static void writeConfig(const QStringList &pinned)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QJsonArray arr;
    for (const QString &a : pinned) arr.append(a);
    QJsonObject obj;
    obj[QStringLiteral("pinned")] = arr;
    QFile f(dir + QStringLiteral("/dock.json"));
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(obj).toJson());
}

class TestDockModel : public QObject {
    Q_OBJECT

    std::unique_ptr<TempConfigDir> m_tmp;

private slots:

    void init()    { m_tmp = std::make_unique<TempConfigDir>(); }
    void cleanup() { m_tmp.reset(); }

    // Verify that the model starts with exactly the pinned apps from config, in order
    void test_initialRowsMatchPinnedApps()
    {
        writeConfig({QStringLiteral("app.a"), QStringLiteral("app.b")});
        ConfigWatcher cw;
        DockModel model(&cw);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), DockModel::AppIdRole).toString(),
                 QStringLiteral("app.a"));
        QCOMPARE(model.data(model.index(1), DockModel::AppIdRole).toString(),
                 QStringLiteral("app.b"));
    }

    // Verify that a running-but-not-pinned app is appended after the pinned apps
    void test_runningAppAppendsAfterPinned()
    {
        writeConfig({QStringLiteral("app.pinned")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.pinned"),
                                    QStringLiteral("app.running.only")});

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1), DockModel::AppIdRole).toString(),
                 QStringLiteral("app.running.only"));
    }

    // Verify that a running pinned app is NOT duplicated in the list
    void test_runningPinnedAppNotDuplicated()
    {
        writeConfig({QStringLiteral("app.both")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.both")});
        QCOMPARE(model.rowCount(), 1);
    }

    // Verify that IsRunningRole is true only for apps with open windows
    void test_isRunningRole()
    {
        writeConfig({QStringLiteral("app.a"), QStringLiteral("app.b")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.a")});

        QCOMPARE(model.data(model.index(0), DockModel::IsRunningRole).toBool(), true);
        QCOMPARE(model.data(model.index(1), DockModel::IsRunningRole).toBool(), false);
    }

    // Verify that WindowCountRole returns the correct window count per app
    void test_windowCountRole()
    {
        writeConfig({QStringLiteral("app.a")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.a")});
        model.onWindowCountChanged(QStringLiteral("app.a"), 3);

        QCOMPARE(model.data(model.index(0), DockModel::WindowCountRole).toInt(), 3);
    }

    // Verify that IsUrgentRole is set when the app has an urgent window
    void test_isUrgentRole()
    {
        writeConfig({QStringLiteral("app.a")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.a")});
        QCOMPARE(model.data(model.index(0), DockModel::IsUrgentRole).toBool(), false);

        model.onWindowUrgent(QStringLiteral("app.a"));
        QCOMPARE(model.data(model.index(0), DockModel::IsUrgentRole).toBool(), true);
    }

    // Verify that pinApp() adds a row to the model
    void test_pinApp_addsRow()
    {
        writeConfig({});
        ConfigWatcher cw;
        DockModel model(&cw);

        QCOMPARE(model.rowCount(), 0);
        model.pinApp(QStringLiteral("app.new"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), DockModel::AppIdRole).toString(),
                 QStringLiteral("app.new"));
    }

    // Verify that unpinApp() removes a non-running app from the model entirely
    void test_unpinApp_removesNonRunningApp()
    {
        writeConfig({QStringLiteral("app.x")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.unpinApp(QStringLiteral("app.x"));
        QCOMPARE(model.rowCount(), 0);
    }

    // Verify that unpinApp() keeps a running app visible but sets IsPinned to false
    void test_unpinApp_keepRunningApp()
    {
        writeConfig({QStringLiteral("app.x")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.x")});
        model.unpinApp(QStringLiteral("app.x"));

        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), DockModel::IsPinnedRole).toBool(), false);
        QCOMPARE(model.data(model.index(0), DockModel::IsRunningRole).toBool(), true);
    }

    // Verify that refreshAllIcons() emits dataChanged for the IconNameRole on all rows
    void test_refreshAllIcons_emitsDataChanged()
    {
        writeConfig({QStringLiteral("app.a"), QStringLiteral("app.b")});
        ConfigWatcher cw;
        DockModel model(&cw);

        QSignalSpy spy(&model, &DockModel::dataChanged);
        model.refreshAllIcons();

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        // topLeft row should be 0, bottomRight row should be 1
        QCOMPARE(args[0].value<QModelIndex>().row(), 0);
        QCOMPARE(args[1].value<QModelIndex>().row(), 1);
        const QVector<int> roles = args[2].value<QVector<int>>();
        QVERIFY(roles.contains(DockModel::IconNameRole));
    }

    // Verify that the model satisfies the Qt abstract item model invariants
    void test_modelConsistency()
    {
        writeConfig({QStringLiteral("app.a"), QStringLiteral("app.b")});
        ConfigWatcher cw;
        DockModel model(&cw);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        Q_UNUSED(tester)
    }

    // Verify that isAppRunning() returns correct values for running and non-running apps
    void test_isAppRunning_helper()
    {
        writeConfig({QStringLiteral("app.a"), QStringLiteral("app.b")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.a")});

        QCOMPARE(model.isAppRunning(QStringLiteral("app.a")), true);
        QCOMPARE(model.isAppRunning(QStringLiteral("app.b")), false);
        QCOMPARE(model.isAppRunning(QStringLiteral("app.nonexistent")), false);
    }

    // Verify that isAppPinned() distinguishes pinned apps from running-only apps
    void test_isAppPinned_helper()
    {
        writeConfig({QStringLiteral("app.pinned")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.pinned"),
                                    QStringLiteral("app.running.only")});

        QCOMPARE(model.isAppPinned(QStringLiteral("app.pinned")),      true);
        QCOMPARE(model.isAppPinned(QStringLiteral("app.running.only")), false);
    }

    // Verify that windowCountForApp() returns the correct count and 0 for unknown apps
    void test_windowCountForApp_helper()
    {
        writeConfig({QStringLiteral("app.a")});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.a")});
        model.onWindowCountChanged(QStringLiteral("app.a"), 5);

        QCOMPARE(model.windowCountForApp(QStringLiteral("app.a")),           5);
        QCOMPARE(model.windowCountForApp(QStringLiteral("app.nonexistent")), 0);
    }

    // Verify that displayNameForApp() falls back to the appId when no .desktop file exists
    void test_displayNameForApp_fallback()
    {
        writeConfig({});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("com.example.nododesktop")});

        QCOMPARE(model.displayNameForApp(QStringLiteral("com.example.nododesktop")),
                 QStringLiteral("com.example.nododesktop"));
    }

    // Verify that pinApp() is a no-op when the app is already pinned
    void test_pinApp_idempotent()
    {
        writeConfig({QStringLiteral("app.already")});
        ConfigWatcher cw;
        DockModel model(&cw);

        const int countBefore = model.rowCount();
        model.pinApp(QStringLiteral("app.already"));
        QCOMPARE(model.rowCount(), countBefore);
    }

    // Verify that when a running-only app is pinned it moves to the pinned section
    // (i.e. it's still in the model and its IsPinned role flips to true)
    void test_pinApp_promotesRunningApp()
    {
        writeConfig({});
        ConfigWatcher cw;
        DockModel model(&cw);

        model.onRunningAppsChanged({QStringLiteral("app.promote")});
        QCOMPARE(model.isAppPinned(QStringLiteral("app.promote")), false);

        model.pinApp(QStringLiteral("app.promote"));
        QCOMPARE(model.isAppPinned(QStringLiteral("app.promote")), true);
        QCOMPARE(model.rowCount(), 1);
    }
};

QTEST_MAIN(TestDockModel)
#include "tst_DockModel.moc"
