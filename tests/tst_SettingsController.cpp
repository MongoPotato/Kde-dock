// Tests for SettingsController: input clamping, validation, signals.
// SettingsController sits between the QML settings panel and ConfigWatcher.
// These tests verify that every setter correctly clamps/validates its input
// before forwarding it to ConfigWatcher, and that signals are emitted as expected.

#include <QtTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QFile>

#include "../src/ConfigWatcher.h"
#include "../src/IconThemeDetector.h"
#include "../src/SettingsController.h"
#include "helpers/TempConfigDir.h"

class TestSettingsController : public QObject {
    Q_OBJECT

    std::unique_ptr<TempConfigDir>      m_tmp;
    std::unique_ptr<ConfigWatcher>      m_config;
    std::unique_ptr<IconThemeDetector>  m_detector;
    std::unique_ptr<SettingsController> m_sc;

private slots:

    void init()
    {
        m_tmp    = std::make_unique<TempConfigDir>();
        // Remove stale config so each test gets built-in defaults
        QFile::remove(QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation) + QStringLiteral("/dock.json"));
        m_config   = std::make_unique<ConfigWatcher>();
        m_detector = std::make_unique<IconThemeDetector>();
        m_sc       = std::make_unique<SettingsController>(m_config.get(), m_detector.get());
    }

    void cleanup()
    {
        m_sc.reset();
        m_detector.reset();
        m_config.reset();
        m_tmp.reset();
    }

    // ── applyIconSize ─────────────────────────────────────────────────────

    // A value below the scroll minimum must be clamped up to scrollMinSize
    void test_applyIconSize_clampsBelow()
    {
        m_sc->applyIconSize(0);
        QCOMPARE(m_config->iconSize(), m_config->scrollMinSize());
    }

    // A value above the scroll maximum must be clamped down to scrollMaxSize
    void test_applyIconSize_clampsAbove()
    {
        m_sc->applyIconSize(9999);
        QCOMPARE(m_config->iconSize(), m_config->scrollMaxSize());
    }

    // A value within the valid range is stored unchanged
    void test_applyIconSize_validValue()
    {
        const int mid = (m_config->scrollMinSize() + m_config->scrollMaxSize()) / 2;
        m_sc->applyIconSize(mid);
        QCOMPARE(m_config->iconSize(), mid);
    }

    // ── applyMagnifyScale ─────────────────────────────────────────────────

    // A scale below 1.0 must be clamped to 1.0
    void test_applyMagnifyScale_clampsBelow()
    {
        m_sc->applyMagnifyScale(0.1);
        QCOMPARE(m_config->magnifyScale(), 1.0);
    }

    // A scale above 3.0 must be clamped to 3.0
    void test_applyMagnifyScale_clampsAbove()
    {
        m_sc->applyMagnifyScale(10.0);
        QCOMPARE(m_config->magnifyScale(), 3.0);
    }

    // A scale within [1.0, 3.0] is stored unchanged
    void test_applyMagnifyScale_validValue()
    {
        m_sc->applyMagnifyScale(2.0);
        QCOMPARE(m_config->magnifyScale(), 2.0);
    }

    // ── applyHoverLift ────────────────────────────────────────────────────

    // A negative lift value must be clamped to 0
    void test_applyHoverLift_clampsBelow()
    {
        m_sc->applyHoverLift(-10);
        QCOMPARE(m_config->hoverLiftPx(), 0);
    }

    // A lift above 40 px must be clamped to 40
    void test_applyHoverLift_clampsAbove()
    {
        m_sc->applyHoverLift(200);
        QCOMPARE(m_config->hoverLiftPx(), 40);
    }

    // A lift within [0, 40] is stored unchanged
    void test_applyHoverLift_validValue()
    {
        m_sc->applyHoverLift(15);
        QCOMPARE(m_config->hoverLiftPx(), 15);
    }

    // ── applyDockOpacity ──────────────────────────────────────────────────

    // An opacity of 0.0 must be clamped to the minimum of 0.1
    void test_applyDockOpacity_clampsBelow()
    {
        m_sc->applyDockOpacity(0.0);
        QCOMPARE(m_config->backgroundOpacity(), 0.1);
    }

    // An opacity above 1.0 must be clamped to 1.0
    void test_applyDockOpacity_clampsAbove()
    {
        m_sc->applyDockOpacity(2.5);
        QCOMPARE(m_config->backgroundOpacity(), 1.0);
    }

    // ── applyIconBgOpacity ────────────────────────────────────────────────

    // Opacity below 0 must be clamped to 0
    void test_applyIconBgOpacity_clampsBelow()
    {
        m_sc->applyIconBgOpacity(-0.5);
        QCOMPARE(m_config->iconBgOpacity(), 0.0);
    }

    // Opacity above 1.0 must be clamped to 1.0
    void test_applyIconBgOpacity_clampsAbove()
    {
        m_sc->applyIconBgOpacity(1.5);
        QCOMPARE(m_config->iconBgOpacity(), 1.0);
    }

    // ── applyIconBgShape ──────────────────────────────────────────────────

    // An unknown shape string must be rejected — config is left at the default
    void test_applyIconBgShape_rejectsInvalid()
    {
        const QString before = m_config->iconBgShape();
        m_sc->applyIconBgShape(QStringLiteral("hexagon"));
        QCOMPARE(m_config->iconBgShape(), before);
    }

    // A valid shape string is accepted and stored
    void test_applyIconBgShape_acceptsCircle()
    {
        m_sc->applyIconBgShape(QStringLiteral("circle"));
        QCOMPARE(m_config->iconBgShape(), QStringLiteral("circle"));
    }

    // All four valid shape values must be accepted
    void test_applyIconBgShape_allValidShapes()
    {
        const QStringList valid{
            QStringLiteral("none"),
            QStringLiteral("circle"),
            QStringLiteral("pill"),
            QStringLiteral("squircle"),
        };
        for (const QString &s : valid) {
            m_sc->applyIconBgShape(s);
            QCOMPARE(m_config->iconBgShape(), s);
        }
    }

    // ── applyAutohide ─────────────────────────────────────────────────────

    // Setting autohide to true must be persisted to config
    void test_applyAutohide_persistsTrue()
    {
        m_sc->applyAutohide(true);
        QCOMPARE(m_config->autohide(), true);
    }

    // Setting autohide to false must be persisted to config
    void test_applyAutohide_persistsFalse()
    {
        m_sc->applyAutohide(true);
        m_sc->applyAutohide(false);
        QCOMPARE(m_config->autohide(), false);
    }

    // ── requestOpenSettings ───────────────────────────────────────────────

    // Calling requestOpenSettings() must emit the openSettingsRequested signal
    void test_requestOpenSettings_emitsSignal()
    {
        QSignalSpy spy(m_sc.get(), &SettingsController::openSettingsRequested);
        m_sc->requestOpenSettings();
        QCOMPARE(spy.count(), 1);
    }

    // ── resetDefaults ─────────────────────────────────────────────────────

    // resetDefaults must cause configChanged to be emitted
    void test_resetDefaults_triggersConfigChanged()
    {
        QSignalSpy spy(m_config.get(), &ConfigWatcher::configChanged);
        m_sc->resetDefaults();
        QVERIFY(spy.count() >= 1);
    }

    // After resetDefaults, icon size must be back to the built-in default (52)
    void test_resetDefaults_restoresIconSize()
    {
        m_sc->applyIconSize(m_config->scrollMaxSize());
        m_sc->resetDefaults();
        QCOMPARE(m_config->iconSize(), 52);
    }
};

QTEST_MAIN(TestSettingsController)
#include "tst_SettingsController.moc"
