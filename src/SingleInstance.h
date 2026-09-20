#pragma once

// SingleInstance makes sure only one kdock owns the desktop at a time.
//
// The dock installs itself as a systemd user service, so a second copy
// launched from a terminal — which is exactly what `kdock --debug` is for —
// used to end up side by side with the installed one: two docks, two KWin
// tracking scripts fighting over the same script name, and two DBus bridges
// racing for org.kde.kdock.
//
// Ownership of the org.kde.kdock bus name IS the lock. Acquiring it with
// replace=true stops the systemd unit (so it can't simply come back), asks the
// running instance to quit over DBus, and waits for the name to be released.

#include <QObject>
#include <QString>

class SingleInstance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdock.Application")

public:
    // Becomes the running kdock, or explains why it couldn't.
    //
    // replace=false — fails if another instance already owns the name.
    // replace=true  — displaces it, as described above.
    //
    // On success the returned object is alive for the process lifetime and
    // answers quit() on the bus; ownership passes to the caller.
    static SingleInstance *acquire(bool replace, QString *error);

    // True when acquire() stopped the systemd unit to take over, so the caller
    // can tell the user how to put the installed dock back.
    bool stoppedSystemdUnit() const { return m_stoppedUnit; }

public slots:
    // Called over DBus by an incoming instance that is replacing us.
    Q_SCRIPTABLE void quit();

private:
    explicit SingleInstance(bool stoppedUnit)
        : m_stoppedUnit(stoppedUnit) {}

    bool m_stoppedUnit = false;
};
