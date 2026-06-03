// Tests for TaskTracker: window detection, appId mapping, urgent hint.
// Uses FakeKWinDBus to avoid requiring a real KWin session.

#include <QtTest>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QStandardPaths>

#include "../src/TaskTracker.h"
#include "helpers/FakeKWinDBus.h"
#include "helpers/TempConfigDir.h"

class TestTaskTracker : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeKWinDBus> m_fakeKWin;
    std::unique_ptr<TempConfigDir> m_tmp;

private slots:

    void initTestCase()
    {
        m_tmp = std::make_unique<TempConfigDir>();
        m_fakeKWin = std::make_unique<FakeKWinDBus>();
        if (!m_fakeKWin->registerOnBus()) {
            QSKIP("Cannot register FakeKWinDBus on the session bus — "
                  "no DBus session available in this environment");
        }
    }

    void cleanupTestCase()
    {
        m_fakeKWin.reset();
        m_tmp.reset();
    }

    void cleanup()
    {
        // Clear all fake windows between tests
        if (m_fakeKWin) {
            m_fakeKWin->removeWindow(1);
            m_fakeKWin->removeWindow(2);
            m_fakeKWin->removeWindow(42);
            m_fakeKWin->removeWindow(99);
        }
    }

    // Verify that a window whose resourceClass resolves to a known .desktop file
    // appears in runningApps() with the correct normalised appId
    void test_mapsResourceClassToAppId()
    {
        // "dolphin" maps to "org.kde.dolphin" or "dolphin" depending on installed .desktop files
        m_fakeKWin->addWindow(1, QStringLiteral("dolphin"), QStringLiteral("dolphin"));

        TaskTracker tracker;
        // Give the first poll cycle time to complete
        QTRY_VERIFY(!tracker.runningApps().isEmpty());
        const QStringList running = tracker.runningApps();
        // At minimum, some entry for dolphin should be present
        const bool found = std::any_of(running.cbegin(), running.cend(),
            [](const QString &id){ return id.contains(QStringLiteral("dolphin"), Qt::CaseInsensitive); });
        QVERIFY(found);
    }

    // Verify that runningAppsChanged is emitted when a window is added via signal
    void test_emitsRunningAppsChangedOnWindowAdded()
    {
        TaskTracker tracker;
        QSignalSpy spy(&tracker, &TaskTracker::runningAppsChanged);

        m_fakeKWin->addWindow(42, QStringLiteral("firefox"), QStringLiteral("firefox"));
        m_fakeKWin->simulateWindowAdded(42);

        QTRY_COMPARE_GT(spy.count(), 0);
    }

    // Verify that runningAppsChanged is emitted when a window is removed via signal
    void test_emitsRunningAppsChangedOnWindowRemoved()
    {
        m_fakeKWin->addWindow(99, QStringLiteral("firefox"), QStringLiteral("firefox"));
        TaskTracker tracker;
        QTRY_VERIFY(!tracker.runningApps().isEmpty());

        QSignalSpy spy(&tracker, &TaskTracker::runningAppsChanged);
        m_fakeKWin->removeWindow(99);
        m_fakeKWin->simulateWindowRemoved(99);

        QTRY_COMPARE_GT(spy.count(), 0);
    }

    // Verify that windowUrgent is emitted when a window has the demandsAttention flag
    void test_emitsWindowUrgentSignal()
    {
        m_fakeKWin->addWindow(2, QStringLiteral("dolphin"), QStringLiteral("dolphin"),
                              /*demandsAttention=*/true);
        TaskTracker tracker;
        QSignalSpy spy(&tracker, &TaskTracker::windowUrgent);
        QTRY_COMPARE_GT(spy.count(), 0);
    }

    // Verify that TaskTracker does not crash when the KWin DBus service is unavailable
    void test_gracefulDegradationWithoutKWin()
    {
        // Temporarily hide the fake KWin service
        m_fakeKWin->unregisterFromBus();

        {
            TaskTracker tracker;
            // Allow at least one poll cycle
            QTest::qWait(600);
            QVERIFY(tracker.runningApps().isEmpty());
        }

        // Re-register for subsequent tests
        m_fakeKWin->registerOnBus();
    }
};

QTEST_MAIN(TestTaskTracker)
#include "tst_TaskTracker.moc"
