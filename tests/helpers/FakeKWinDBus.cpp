// FakeKWinDBus — minimal DBus stub that impersonates org.kde.KWin.
// See FakeKWinDBus.h for design rationale and usage.

#include "FakeKWinDBus.h"

#include <QDBusConnection>

static constexpr auto kService  = "org.kde.KWin";
static constexpr auto kPath     = "/KWin";

FakeKWinDBus::FakeKWinDBus(QObject *parent)
    : QObject(parent)
{
}

FakeKWinDBus::~FakeKWinDBus()
{
    if (m_registered)
        unregisterFromBus();
}

bool FakeKWinDBus::registerOnBus()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QLatin1String(kService)))
        return false;
    if (!bus.registerObject(QLatin1String(kPath), this,
                            QDBusConnection::ExportAllSlots |
                            QDBusConnection::ExportAllSignals)) {
        bus.unregisterService(QLatin1String(kService));
        return false;
    }
    m_registered = true;
    return true;
}

void FakeKWinDBus::unregisterFromBus()
{
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QLatin1String(kPath));
    bus.unregisterService(QLatin1String(kService));
    m_registered = false;
}

void FakeKWinDBus::addWindow(quint64 id, const QString &resourceClass,
                              const QString &appId, bool demandsAttention)
{
    m_windows.append({id, resourceClass, appId, demandsAttention});
}

void FakeKWinDBus::removeWindow(quint64 id)
{
    m_windows.removeIf([id](const FakeWindow &w) { return w.id == id; });
}

void FakeKWinDBus::simulateWindowAdded(quint64 id)
{
    emit windowAdded(id);
}

void FakeKWinDBus::simulateWindowRemoved(quint64 id)
{
    emit windowRemoved(id);
}

QVariantList FakeKWinDBus::getWindowInfo()
{
    QVariantList result;
    for (const FakeWindow &w : std::as_const(m_windows)) {
        QVariantMap info;
        info[QStringLiteral("id")]               = w.id;
        info[QStringLiteral("resourceClass")]     = w.resourceClass;
        info[QStringLiteral("resourceName")]      = w.resourceClass;
        info[QStringLiteral("demandsAttention")]  = w.demandsAttention;
        result.append(info);
    }
    return result;
}
