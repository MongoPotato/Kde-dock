// Tests for WindowPusher, the dock's half of "push windows aside" (issue #12).
//
// The KWin half is a script that long-polls pollState() over D-Bus. Without
// a KWin to load it into, these tests check the protocol from the script's
// side: what state it receives, that a poll is held until something
// changes, and that loading degrades cleanly when KWin isn't there.
//
// The long-poll tests need a session bus and are skipped without one; run
// under dbus-run-session to get one (ctest does when it is installed).

#include <QtTest>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QThread>

#include <atomic>

#include "../src/WindowPusher.h"

static QJsonObject parse(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

// ── A stand-in for KWin ──────────────────────────────────────────────────────
//
// Serves just enough of org.kde.KWin (/Scripting and /Effects) for
// WindowPusher to load its script and effect. Lives on its own thread: the
// pusher's calls into KWin block the test's main thread.

class FakeScripting : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")
public slots:
    Q_SCRIPTABLE int loadScript(const QString &, const QString &) { return 0; }
    Q_SCRIPTABLE void start() {}
    Q_SCRIPTABLE bool unloadScript(const QString &) { return true; }
};

class FakeEffects : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
public:
    std::atomic<bool> installed { true };
    std::atomic<bool> preloaded { false };
    std::atomic<int> loads { 0 };
    std::atomic<int> unloads { 0 };
    std::atomic<int> headsUps { 0 };
public slots:
    Q_SCRIPTABLE bool loadEffect(const QString &) { ++loads; return installed; }
    Q_SCRIPTABLE void unloadEffect(const QString &) { ++unloads; }
    Q_SCRIPTABLE bool isEffectLoaded(const QString &) const { return preloaded; }
    Q_SCRIPTABLE void reconfigureEffect(const QString &) { ++headsUps; }
};

// Polls the pusher the way the KWin script does — from KWin's own bus
// connection, which is what makes D-Bus ordering apply between the effect's
// heads-up and the state. Records how many heads-ups the effect had
// received by the time each reply arrived.
class FakeScriptPoller : public QObject {
    Q_OBJECT
public:
    FakeScriptPoller(QDBusConnection connection, FakeEffects *effects)
        : m_connection(connection), m_effects(effects) {}

    std::atomic<int> replies { 0 };
    std::atomic<int> headsUpsAtLastReply { -1 };

    QString lastState() const { QMutexLocker lock(&m_mutex); return m_lastState; }

    void poll(const QString &knownSerial)
    {
        QMetaObject::invokeMethod(this, [this, knownSerial]() {
            QDBusMessage call = QDBusMessage::createMethodCall(
                QDBusConnection::sessionBus().baseService(),
                QStringLiteral("/WindowPusher"),
                QStringLiteral("org.kde.kdock.WindowPusher"),
                QStringLiteral("pollState"));
            call << knownSerial;
            auto *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call), this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this](QDBusPendingCallWatcher *w) {
                headsUpsAtLastReply = m_effects->headsUps.load();
                {
                    QMutexLocker lock(&m_mutex);
                    m_lastState = QDBusPendingReply<QString>(*w).value();
                }
                ++replies;
                w->deleteLater();
            });
        });
    }

private:
    QDBusConnection m_connection;
    FakeEffects *m_effects;
    mutable QMutex m_mutex;
    QString m_lastState;
};

class FakeKWin {
public:
    FakeKWin()
        : m_connection(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                     QStringLiteral("fake-kwin")))
        , poller(m_connection, &effects)
    {
        scripting.moveToThread(&m_thread);
        effects.moveToThread(&m_thread);
        poller.moveToThread(&m_thread);
        m_thread.start();
        m_connection.registerObject(QStringLiteral("/Scripting"), &scripting,
                                    QDBusConnection::ExportScriptableSlots);
        m_connection.registerObject(QStringLiteral("/Effects"), &effects,
                                    QDBusConnection::ExportScriptableSlots);
        registered = m_connection.registerService(QStringLiteral("org.kde.KWin"));
    }

    ~FakeKWin()
    {
        m_connection.unregisterService(QStringLiteral("org.kde.KWin"));
        m_connection.unregisterObject(QStringLiteral("/Scripting"));
        m_connection.unregisterObject(QStringLiteral("/Effects"));
        m_thread.quit();
        m_thread.wait();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-kwin"));
    }

    bool registered = false;

private:
    QDBusConnection m_connection;
    QThread m_thread;

public:
    FakeScripting scripting;
    FakeEffects effects;
    FakeScriptPoller poller;
};

class TestWindowPusher : public QObject {
    Q_OBJECT

private:
    // A second connection, so the call reaches the pusher through the bus
    // the way the KWin script's does, rather than being short-circuited as a
    // call to ourselves.
    static QDBusConnection scriptConnection()
    {
        return QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                             QStringLiteral("fake-kwin-script"));
    }

    static QDBusPendingCall poll(const QString &knownSerial)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QDBusConnection::sessionBus().baseService(),
            QStringLiteral("/WindowPusher"),
            QStringLiteral("org.kde.kdock.WindowPusher"),
            QStringLiteral("pollState"));
        call << knownSerial;
        return scriptConnection().asyncCall(call);
    }

private slots:
    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-kwin-script"));
    }

    void test_initialState()
    {
        WindowPusher pusher;
        QCOMPARE(pusher.status(), QStringLiteral("off"));
        const QJsonObject state = parse(pusher.stateJson());
        QCOMPARE(state.value(QStringLiteral("enabled")).toBool(), false);
        QCOMPARE(state.value(QStringLiteral("revealed")).toBool(), false);
        QCOMPARE(state.value(QStringLiteral("edge")).toString(), QStringLiteral("bottom"));
    }

    void test_stateCarriesPlacement()
    {
        WindowPusher pusher;
        pusher.setPlacement(QStringLiteral("DP-1"), QStringLiteral("left"), 84);
        pusher.setDockRevealed(true);
        const QJsonObject state = parse(pusher.stateJson());
        QCOMPARE(state.value(QStringLiteral("output")).toString(), QStringLiteral("DP-1"));
        QCOMPARE(state.value(QStringLiteral("edge")).toString(), QStringLiteral("left"));
        QCOMPARE(state.value(QStringLiteral("thickness")).toInt(), 84);
        QCOMPARE(state.value(QStringLiteral("revealed")).toBool(), true);
        QCOMPARE(state.value(QStringLiteral("serial")).toInt(), pusher.serial());
    }

    // Every change has to reach the script, and only changes may: the
    // script re-polls with the serial it has, and an unchanged serial means
    // "wait".
    void test_serialMovesOnlyOnChange()
    {
        WindowPusher pusher;
        const int start = pusher.serial();
        pusher.setDockRevealed(true);
        QCOMPARE(pusher.serial(), start + 1);
        pusher.setDockRevealed(true);
        QCOMPARE(pusher.serial(), start + 1);
        pusher.setPlacement(QStringLiteral("eDP-1"), QStringLiteral("bottom"), 90);
        QCOMPARE(pusher.serial(), start + 2);
        pusher.setPlacement(QStringLiteral("eDP-1"), QStringLiteral("bottom"), 90);
        QCOMPARE(pusher.serial(), start + 2);
        pusher.setDockRevealed(false);
        QCOMPARE(pusher.serial(), start + 3);
    }

    // Without KWin on the bus there is nothing to load the script into. The
    // dock must say so (the settings panel shows it) and carry on.
    void test_enableWithoutKWinIsUnavailable()
    {
        if (QDBusConnection::sessionBus().interface()
            && QDBusConnection::sessionBus().interface()->isServiceRegistered(
                   QStringLiteral("org.kde.KWin"))) {
            QSKIP("A real KWin is on this bus");
        }
        WindowPusher pusher;
        pusher.setEnabled(true);
        QCOMPARE(pusher.status(), QStringLiteral("unavailable"));
        QVERIFY(parse(pusher.stateJson()).value(QStringLiteral("enabled")).toBool());
        pusher.setEnabled(false);
        QCOMPARE(pusher.status(), QStringLiteral("off"));
    }

    // A script that is behind gets the current state straight away.
    void test_staleSerialIsAnsweredImmediately()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        WindowPusher pusher;
        pusher.setDockRevealed(true);

        // Not waitForFinished(): that would block the very thread that has
        // to answer the call.
        auto *watcher = new QDBusPendingCallWatcher(poll(QString()), this);
        QSignalSpy finished(watcher, &QDBusPendingCallWatcher::finished);
        QVERIFY(finished.wait(2000));
        QDBusPendingReply<QString> reply = *watcher;
        QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
        const QJsonObject state = parse(reply.value());
        QCOMPARE(state.value(QStringLiteral("serial")).toInt(), pusher.serial());
        QCOMPARE(state.value(QStringLiteral("revealed")).toBool(), true);
        watcher->deleteLater();
    }

    // A script that is up to date is held until something changes, then
    // gets the new state.
    void test_currentSerialIsHeldUntilChange()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        WindowPusher pusher;
        pusher.setPlacement(QStringLiteral("DP-2"), QStringLiteral("bottom"), 80);

        QDBusPendingCall call = poll(QString::number(pusher.serial()));
        auto *watcher = new QDBusPendingCallWatcher(call, this);
        QSignalSpy finished(watcher, &QDBusPendingCallWatcher::finished);

        QVERIFY(!finished.wait(300));   // held

        pusher.setDockRevealed(true);
        QVERIFY(finished.wait(2000));
        QDBusPendingReply<QString> reply = *watcher;
        QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
        const QJsonObject state = parse(reply.value());
        QCOMPARE(state.value(QStringLiteral("revealed")).toBool(), true);
        QCOMPARE(state.value(QStringLiteral("output")).toString(), QStringLiteral("DP-2"));
        watcher->deleteLater();
    }

    // The script only ever waits on one poll; if it gave up on one (its
    // watchdog fired) and asked again, the abandoned one must be answered
    // rather than left to pile up.
    void test_newPollReleasesAbandonedOne()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        WindowPusher pusher;
        const QString current = QString::number(pusher.serial());

        auto *first = new QDBusPendingCallWatcher(poll(current), this);
        QSignalSpy firstDone(first, &QDBusPendingCallWatcher::finished);
        QVERIFY(!firstDone.wait(200));

        auto *second = new QDBusPendingCallWatcher(poll(current), this);
        QSignalSpy secondDone(second, &QDBusPendingCallWatcher::finished);
        QVERIFY(firstDone.wait(2000));
        QVERIFY(QDBusPendingReply<QString>(*first).isValid());
        QVERIFY(!secondDone.wait(200));   // the new one is held as usual

        pusher.setDockRevealed(true);
        QVERIFY(secondDone.wait(2000));
        first->deleteLater();
        second->deleteLater();
    }

    // ── The slide effect (phase 2) ────────────────────────────────────────

    // The effect comes and goes with the script, and the dock knows whether
    // windows will slide.
    void test_effectLoadedAndUnloadedWithScript()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        FakeKWin kwin;
        QVERIFY(kwin.registered);
        WindowPusher pusher;
        pusher.setEnabled(true);
        QCOMPARE(pusher.status(), QStringLiteral("starting"));
        QVERIFY(pusher.isAnimated());
        QCOMPARE(kwin.effects.loads.load(), 1);

        pusher.setEnabled(false);
        QCOMPARE(pusher.status(), QStringLiteral("off"));
        QVERIFY(!pusher.isAnimated());
        QCOMPARE(kwin.effects.unloads.load(), 1);
    }

    // Enabled by the user in System Settings: use it, but don't unload it.
    void test_effectAlreadyLoadedIsLeftLoaded()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        FakeKWin kwin;
        QVERIFY(kwin.registered);
        kwin.effects.preloaded = true;
        WindowPusher pusher;
        pusher.setEnabled(true);
        QVERIFY(pusher.isAnimated());
        QCOMPARE(kwin.effects.loads.load(), 0);
        pusher.setEnabled(false);
        QCOMPARE(kwin.effects.unloads.load(), 0);
    }

    // Not installed: windows jump, as in phase 1, and nothing else changes.
    void test_missingEffectMeansNoAnimation()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        FakeKWin kwin;
        QVERIFY(kwin.registered);
        kwin.effects.installed = false;
        WindowPusher pusher;
        pusher.setEnabled(true);
        QCOMPARE(pusher.status(), QStringLiteral("starting"));
        QVERIFY(!pusher.isAnimated());

        pusher.setPlacement(QStringLiteral("eDP-1"), QStringLiteral("bottom"), 80);
        pusher.setDockRevealed(true);
        QTest::qWait(100);
        QCOMPARE(kwin.effects.headsUps.load(), 0);
        pusher.setEnabled(false);
        QCOMPARE(kwin.effects.unloads.load(), 0);
    }

    // The whole point of the heads-up: the effect must have it before the
    // script hears about the new state, or the windows jump.
    void test_headsUpArrivesBeforeHeldState()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        FakeKWin kwin;
        QVERIFY(kwin.registered);
        WindowPusher pusher;
        pusher.setEnabled(true);
        QVERIFY(pusher.isAnimated());

        kwin.poller.poll(QString::number(pusher.serial()));
        QTRY_COMPARE(pusher.status(), QStringLiteral("active"));
        QTest::qWait(100);
        QCOMPARE(kwin.poller.replies.load(), 0);   // held
        QCOMPARE(kwin.effects.headsUps.load(), 0);

        pusher.setDockRevealed(true);
        QTRY_COMPARE(kwin.poller.replies.load(), 1);
        QCOMPARE(kwin.poller.headsUpsAtLastReply.load(), 1);
        QVERIFY(parse(kwin.poller.lastState()).value(QStringLiteral("revealed")).toBool());
    }

    // Same when the script was between polls and picks the state up late.
    void test_headsUpArrivesBeforeImmediateState()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        FakeKWin kwin;
        QVERIFY(kwin.registered);
        WindowPusher pusher;
        pusher.setEnabled(true);
        pusher.setDockRevealed(true);              // nobody polling yet
        QCOMPARE(kwin.effects.headsUps.load(), 0);

        kwin.poller.poll(QStringLiteral("0"));
        QTRY_COMPARE(kwin.poller.replies.load(), 1);
        QCOMPARE(kwin.poller.headsUpsAtLastReply.load(), 1);
    }

    // Shutting the dock down must not leave a poll hanging until the bus
    // times it out.
    void test_destructionAnswersHeldPoll()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("No session bus");
        auto *pusher = new WindowPusher;
        auto *watcher = new QDBusPendingCallWatcher(poll(QString::number(pusher->serial())), this);
        QSignalSpy finished(watcher, &QDBusPendingCallWatcher::finished);
        QVERIFY(!finished.wait(200));

        delete pusher;
        QVERIFY(finished.wait(2000));
        QVERIFY(QDBusPendingReply<QString>(*watcher).isValid());
        watcher->deleteLater();
    }
};

QTEST_MAIN(TestWindowPusher)
#include "tst_WindowPusher.moc"
