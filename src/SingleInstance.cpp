#include "SingleInstance.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDeadlineTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace {

// QStringLiteral needs an actual literal at the call site, so these are
// functions rather than char* constants.
QString serviceName() { return QStringLiteral("org.kde.kdock"); }
QString objectPath()  { return QStringLiteral("/Application"); }
QString interfaceName() { return QStringLiteral("org.kde.kdock.Application"); }

bool takeName()
{
    return QDBusConnection::sessionBus().registerService(serviceName());
}

// Stop the installed unit first. Asking the old process to quit without this
// leaves systemd owning a service it thinks should be running, and any restart
// policy would race us straight back to two docks.
bool stopSystemdUnit()
{
    if (QStandardPaths::findExecutable(QStringLiteral("systemctl")).isEmpty())
        return false;
    // Returns non-zero when the unit isn't installed or isn't running, which
    // is not an error here — there was simply nothing to stop.
    return QProcess::execute(QStringLiteral("systemctl"),
                             {QStringLiteral("--user"),
                              QStringLiteral("stop"),
                              QStringLiteral("kdock.service")}) == 0;
}

void askRunningInstanceToQuit()
{
    QDBusInterface iface(serviceName(), objectPath(),
                         interfaceName(), QDBusConnection::sessionBus());
    if (!iface.isValid()) return;
    // Fire and forget: the peer is about to exit, so waiting for a reply it
    // may never send would just stall us.
    iface.asyncCall(QStringLiteral("quit"));
}

} // namespace

SingleInstance *SingleInstance::acquire(bool replace, QString *error)
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        // No session bus (a bare test environment, say). Nothing can be
        // holding the name, so there is nothing to replace and nothing to
        // publish a quit() method on.
        return new SingleInstance(false);
    }

    bool stoppedUnit = false;

    if (!takeName()) {
        if (!replace) {
            if (error)
                *error = QStringLiteral(
                    "another kdock is already running — pass --replace to take over");
            return nullptr;
        }

        stoppedUnit = stopSystemdUnit();
        askRunningInstanceToQuit();

        // Wait for the old instance to drop the name. It has to finish its own
        // shutdown — unloading its KWin script among other things — so this is
        // not instant.
        bool got = false;
        QDeadlineTimer deadline(5000);
        while (!deadline.hasExpired()) {
            QThread::msleep(100);
            if (takeName()) { got = true; break; }
        }

        if (!got) {
            if (error)
                *error = QStringLiteral(
                    "another kdock is running and did not exit within 5s — "
                    "stop it with: systemctl --user stop kdock");
            return nullptr;
        }
    }

    auto *self = new SingleInstance(stoppedUnit);
    QDBusConnection::sessionBus().registerObject(
        objectPath(), self, QDBusConnection::ExportScriptableSlots);
    return self;
}

void SingleInstance::quit()
{
    qInfo("kdock: another instance is taking over — exiting");
    QCoreApplication::quit();
}
