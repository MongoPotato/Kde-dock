#pragma once

// WindowPusher: makes room for the auto-hidden dock when it reveals, by
// moving the windows it would cover out of the way and putting them back
// when it hides again (issue #12).
//
// A Wayland client cannot move other clients' windows, so the moving is
// done by a KWin script (data/kwin/windowpusher.js) that this class loads
// into KWin over org.kde.kwin.Scripting, the same way TaskTracker loads its
// tracking script. Nothing has to be installed separately.
//
// KWin scripts can call D-Bus but cannot be called, so the script asks
// rather than being told: it long-polls pollState() on the object this class
// publishes at org.kde.kdock /WindowPusher. pollState() answers at once if
// the script's view is out of date, and otherwise holds the reply until the
// state changes or the keep-alive interval passes. The script reports back
// through acknowledge() once it has applied a state.
//
// The state is: enabled, revealed, and where the dock sits (output name,
// edge, thickness). It is deliberately all the script needs: it finds the
// output itself, so dock and compositor never have to agree on coordinates.
//
// Animation (phase 2) is a separate KWin effect, kdock_pushslide, installed
// with the dock (data/kwin/kdock_pushslide). The script still moves each
// window once; the effect makes that move slide. It is loaded and unloaded
// together with the script, and is optional: without it windows jump.
// Effects can't talk D-Bus either, so before every state change this class
// pokes the effect (reconfigureEffect), which tells it the script is about
// to move windows. See the effect's main.js.

#include <QDBusContext>
#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QDBusInterface;

class WindowPusher : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdock.WindowPusher")

    // "off"         — the feature is disabled (or auto-hide is)
    // "unavailable" — KWin's scripting interface isn't on the session bus
    // "starting"    — the script is loaded but hasn't checked in yet
    // "active"      — the script is running and talking to us
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // True while the kdock_pushslide effect is loaded, i.e. pushed windows
    // slide rather than jump. The dock then pushes as soon as it starts to
    // slide in, so both move together.
    Q_PROPERTY(bool animated READ isAnimated NOTIFY statusChanged)

public:
    explicit WindowPusher(QObject *parent = nullptr);
    ~WindowPusher() override;

    // Loads the KWin script when turned on; on the way off, has the script
    // put every window back before unloading it.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Where the dock sits. thickness is the band it covers when revealed.
    void setPlacement(const QString &outputName, const QString &edge, int thickness);

    // Called from QML: true as the dock starts sliding in when animated, else
    // once it is all the way in; false as soon as it starts sliding out.
    Q_INVOKABLE void setDockRevealed(bool revealed);
    bool isDockRevealed() const { return m_revealed; }

    QString status() const;
    bool isAnimated() const { return m_animated; }

    // The state as the script receives it — exposed for tests.
    QString stateJson() const;
    int serial() const { return m_serial; }

public slots:
    // Called by the KWin script. Returns stateJson() straight away if
    // knownSerial isn't the current serial, otherwise once it changes or the
    // keep-alive interval passes.
    Q_SCRIPTABLE QString pollState(const QString &knownSerial);

    // Called by the KWin script once it has applied the state with this
    // serial. summary is a short note for the debug log.
    Q_SCRIPTABLE void acknowledge(const QString &serial, const QString &summary);

signals:
    void statusChanged();

private:
    void bumpSerial();
    void answerPendingPolls();
    void loadScript();
    void unloadScript();
    void loadEffect();
    void unloadEffect();
    void notifyEffect();
    void setStatus(const QString &status);

    QDBusInterface *m_scripting = nullptr;
    QDBusInterface *m_effects = nullptr;
    bool m_animated = false;
    bool m_ownsEffect = false;   // we loaded it, so we unload it
    bool m_objectRegistered = false;
    bool m_scriptLoaded = false;

    bool m_enabled = false;
    bool m_revealed = false;
    QString m_outputName;
    QString m_edge { QStringLiteral("bottom") };
    int m_thickness = 0;

    int m_serial = 0;
    int m_acknowledgedSerial = -1;
    QList<QDBusMessage> m_pendingPolls;
    QTimer m_keepAlive;      // answers held polls so the script knows we're alive
    QTimer m_unloadTimer;    // gives up waiting for the "restored" ack
    QString m_status { QStringLiteral("off") };
};
