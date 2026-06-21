// Smoke test for TaskTracker: it drives KWin's scripting engine over DBus
// (org.kde.kwin.Scripting). Without a real KWin session bus to talk to, the
// DBus calls simply fail/return invalid, and TaskTracker should degrade to
// reporting no running apps and treating actions as no-ops, without crashing.

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
