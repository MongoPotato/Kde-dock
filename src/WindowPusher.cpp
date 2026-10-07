// WindowPusher — see WindowPusher.h for what this is and why the moving
// itself happens inside KWin.
//
// Lifecycle of the KWin script:
//   setEnabled(true)  → write the script out, load and start it in KWin.
//   setEnabled(false) → publish enabled=false; the script puts every window
//                       back and acknowledges, and only then is it unloaded
//                       (or after a short timeout, if it never answers).
//   destruction       → the same, waiting briefly in a local event loop so
//                       quitting kdock doesn't leave windows pushed.
// If kdock dies without any of that, the script notices the missing
// keep-alive and restores everything by itself.

#include "WindowPusher.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <utility>

static const QString kScriptName = QStringLiteral("kdock-windowpusher");

// How often a held poll is answered even though nothing changed. The script
// treats 15 s of silence as "kdock is gone", so this has to stay well below.
static constexpr int kKeepAliveMs = 10000;

// How long to wait for the script to confirm it has put every window back
// before unloading it anyway.
static constexpr int kRestoreTimeoutMs = 1000;

static QString scriptFilePath()
{
    // The runtime dir is per-user and private; /tmp is a last resort.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/kdock-windowpusher.js");
}

WindowPusher::WindowPusher(QObject *parent)
    : QObject(parent)
{
    m_keepAlive.setSingleShot(true);
    m_keepAlive.setInterval(kKeepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, &WindowPusher::answerPendingPolls);

    m_unloadTimer.setSingleShot(true);
    m_unloadTimer.setInterval(kRestoreTimeoutMs);
    connect(&m_unloadTimer, &QTimer::timeout, this, [this]() {
        qDebug("kdock [pusher]: script never confirmed the restore; unloading anyway");
        unloadScript();
    });

    // The org.kde.kdock name itself is owned by SingleInstance.
    m_objectRegistered = QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/WindowPusher"), this, QDBusConnection::ExportScriptableSlots);
}

WindowPusher::~WindowPusher()
{
    if (m_scriptLoaded) {
        // Have the script put everything back before it goes away. Only worth
        // waiting for if it is actually running and listening.
        const bool listening = m_status == QStringLiteral("active");
        m_enabled = false;
        bumpSerial();
        if (listening && m_acknowledgedSerial < m_serial) {
            QEventLoop loop;
            QTimer::singleShot(kRestoreTimeoutMs, &loop, &QEventLoop::quit);
            connect(this, &WindowPusher::statusChanged, &loop, &QEventLoop::quit);
            loop.exec();
        }
        unloadScript();
    }
    answerPendingPolls();
    if (m_objectRegistered)
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/WindowPusher"));
}

// ── Inputs ───────────────────────────────────────────────────────────────────

void WindowPusher::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    qDebug("kdock [pusher]: %s", enabled ? "enabled" : "disabled");
    bumpSerial();

    if (enabled) {
        m_unloadTimer.stop();
        loadScript();
    } else if (m_scriptLoaded) {
        if (m_status == QStringLiteral("active"))
            m_unloadTimer.start();   // acknowledge() unloads sooner
        else
            unloadScript();
    } else {
        setStatus(QStringLiteral("off"));
    }
}

void WindowPusher::setPlacement(const QString &outputName, const QString &edge, int thickness)
{
    if (m_outputName == outputName && m_edge == edge && m_thickness == thickness)
        return;
    m_outputName = outputName;
    m_edge = edge;
    m_thickness = thickness;
    bumpSerial();
}

void WindowPusher::setDockRevealed(bool revealed)
{
    if (m_revealed == revealed)
        return;
    m_revealed = revealed;
    bumpSerial();
}

QString WindowPusher::status() const
{
    return m_status;
}

QString WindowPusher::stateJson() const
{
    const QJsonObject state{
        {QStringLiteral("serial"),    m_serial},
        {QStringLiteral("enabled"),   m_enabled},
        {QStringLiteral("revealed"),  m_revealed},
        {QStringLiteral("output"),    m_outputName},
        {QStringLiteral("edge"),      m_edge},
        {QStringLiteral("thickness"), m_thickness},
    };
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

// ── D-Bus, called by the KWin script ────────────────────────────────────────

QString WindowPusher::pollState(const QString &knownSerial)
{
    if (m_scriptLoaded && calledFromDBus())
        setStatus(QStringLiteral("active"));

    // The script only ever has one poll outstanding. One already held here
    // was abandoned (the script's watchdog fired), so answer it — the script
    // ignores replies to polls it has given up on — rather than let them
    // pile up.
    answerPendingPolls();

    if (knownSerial != QString::number(m_serial) || !calledFromDBus())
        return stateJson();

    setDelayedReply(true);
    m_pendingPolls.append(message());
    m_keepAlive.start();
    return QString();
}

void WindowPusher::acknowledge(const QString &serial, const QString &summary)
{
    bool ok = false;
    const int acked = serial.toInt(&ok);
    if (!ok)
        return;
    m_acknowledgedSerial = acked;
    qDebug("kdock [pusher]: script applied state %d (%s)", acked, qPrintable(summary));

    // The script has put everything back; it is safe to unload now.
    if (!m_enabled && m_scriptLoaded && acked >= m_serial) {
        m_unloadTimer.stop();
        unloadScript();
    }
}

// ── Internals ────────────────────────────────────────────────────────────────

void WindowPusher::bumpSerial()
{
    ++m_serial;
    answerPendingPolls();
}

void WindowPusher::answerPendingPolls()
{
    m_keepAlive.stop();
    if (m_pendingPolls.isEmpty())
        return;
    const QString state = stateJson();
    const QList<QDBusMessage> polls = std::exchange(m_pendingPolls, {});
    for (const QDBusMessage &poll : polls)
        QDBusConnection::sessionBus().send(poll.createReply(state));
}

void WindowPusher::loadScript()
{
    if (m_scriptLoaded)
        return;

    if (!m_scripting) {
        m_scripting = new QDBusInterface(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"),
            QDBusConnection::sessionBus(),
            this);
    }
    if (!m_scripting->isValid() || !m_objectRegistered) {
        qWarning("kdock [pusher]: KWin scripting is not available (scripting valid=%s, "
                 "D-Bus object registered=%s) — windows will not be pushed aside",
                 m_scripting->isValid() ? "true" : "false",
                 m_objectRegistered ? "true" : "false");
        setStatus(QStringLiteral("unavailable"));
        return;
    }

    QFile source(QStringLiteral(":/kwin/windowpusher.js"));
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning("kdock [pusher]: KWin script missing from the resources");
        setStatus(QStringLiteral("unavailable"));
        return;
    }
    const QString path = scriptFilePath();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("kdock [pusher]: cannot write KWin script to '%s'", qPrintable(path));
        setStatus(QStringLiteral("unavailable"));
        return;
    }
    out.write(source.readAll());
    out.close();

    // A copy left behind by a kdock that didn't shut down cleanly.
    m_scripting->call(QStringLiteral("unloadScript"), kScriptName);

    // KWin reads the file asynchronously, so it has to stay on disk until the
    // script is unloaded.
    // A refusal (name taken, file unreadable) is a normal reply carrying -1.
    const QDBusMessage loadReply = m_scripting->call(QStringLiteral("loadScript"), path, kScriptName);
    const bool loaded = loadReply.type() == QDBusMessage::ReplyMessage
                     && !loadReply.arguments().isEmpty()
                     && loadReply.arguments().constFirst().toInt() >= 0;
    if (!loaded) {
        qWarning("kdock [pusher]: KWin refused to load the script%s%s",
                 loadReply.errorMessage().isEmpty() ? "" : ": ",
                 qPrintable(loadReply.errorMessage()));
        QFile::remove(path);
        setStatus(QStringLiteral("unavailable"));
        return;
    }
    m_scriptLoaded = true;
    m_acknowledgedSerial = -1;
    m_scripting->call(QStringLiteral("start"));
    qDebug("kdock [pusher]: KWin script loaded from '%s'", qPrintable(path));
    setStatus(QStringLiteral("starting"));
}

void WindowPusher::unloadScript()
{
    m_unloadTimer.stop();
    if (!m_scriptLoaded)
        return;
    m_scriptLoaded = false;
    if (m_scripting && m_scripting->isValid())
        m_scripting->call(QStringLiteral("unloadScript"), kScriptName);
    QFile::remove(scriptFilePath());
    qDebug("kdock [pusher]: KWin script unloaded");
    setStatus(QStringLiteral("off"));
}

void WindowPusher::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
