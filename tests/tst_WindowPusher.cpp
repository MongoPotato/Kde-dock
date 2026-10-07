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

#include "../src/WindowPusher.h"

static QJsonObject parse(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

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
