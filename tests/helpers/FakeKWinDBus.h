#pragma once

// FakeKWinDBus — minimal DBus stub that impersonates org.kde.KWin on the
// session bus during tests so TaskTracker can be exercised without a live
// Plasma session.
//
// Only the DBus interface methods that TaskTracker actually calls are
// implemented here.  Adding methods beyond that scope is a source of
// confusion and maintenance burden.
//
// Usage:
//   FakeKWinDBus fakeKWin;
//   QVERIFY(fakeKWin.registerOnBus());
//   fakeKWin.addWindow(42, "firefox", "org.mozilla.firefox");
//   // now create a TaskTracker and verify it sees the window

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class FakeKWinDBus : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")

public:
    explicit FakeKWinDBus(QObject *parent = nullptr);
    ~FakeKWinDBus();

    bool registerOnBus();
    void unregisterFromBus();

    // Add a fake window to the list returned by getWindowInfo()
    void addWindow(quint64 id, const QString &resourceClass, const QString &appId,
                   bool demandsAttention = false);

    // Remove a fake window by id
    void removeWindow(quint64 id);

    // Emit windowAdded / windowRemoved to simulate KWin events
    void simulateWindowAdded(quint64 id);
    void simulateWindowRemoved(quint64 id);

public slots:
    // Called by TaskTracker via DBus
    QVariantList getWindowInfo();

signals:
    // Mimic KWin signals that TaskTracker subscribes to
    void windowAdded(quint64 id);
    void windowRemoved(quint64 id);

private:
    struct FakeWindow {
        quint64 id;
        QString resourceClass;
        QString appId;
        bool demandsAttention;
    };
    QList<FakeWindow> m_windows;
    bool m_registered = false;
};
