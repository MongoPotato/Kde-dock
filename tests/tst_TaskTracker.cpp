// Smoke test for TaskTracker: it now binds the plasma-window-management
// Wayland protocol directly (no DBus mocking possible — there is no DBus
// involved at all). Without a real compositor advertising that global,
// TaskTracker should simply detect no Wayland window manager and report no
// running apps, without crashing.

#include <QtTest>

#include "../src/TaskTracker.h"

class TestTaskTracker : public QObject {
    Q_OBJECT

private slots:
    void test_constructDestructWithoutCompositor()
    {
        TaskTracker tracker;
        QVERIFY(tracker.runningApps().isEmpty());
        QVERIFY(tracker.activeAppId().isEmpty());
    }

    void test_noWindowsMeansNoneRunning()
    {
        TaskTracker tracker;
        QVERIFY(!tracker.hasWindowForApp(QStringLiteral("org.kde.dolphin")));
    }

    void test_activateAndCloseAreNoOpsWithoutWindows()
    {
        TaskTracker tracker;
        // Must not crash when there is nothing to act on.
        tracker.activateWindow(QStringLiteral("org.kde.dolphin"));
        tracker.closeWindows(QStringLiteral("org.kde.dolphin"));
    }
};

QTEST_MAIN(TestTaskTracker)
#include "tst_TaskTracker.moc"
