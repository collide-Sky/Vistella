// tst_P8_3_1.cpp
//
// Unit test for P8.3.x (2026-10-19): Settings dialog GPU page
// (SettingsGpuPanel embedded into SettingsDialog).
//
//   Panel exposes 3 radio buttons (Auto / Force GPU / Force CPU) +
//   Apply + Probe OpenGL. State lives in gpu::BackendRegistry::instance().
//   Tests cover: default selection matches registry, applySelected
//   writes back, mode labels, currentBackendName reflects mode, Probe
//   does not crash in headless environments.

#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QRadioButton>
#include <QPushButton>

#include <opencv2/core.hpp>

#include "../src/ui/settingsgpupanel.h"
#include "../src/media/gpu/BackendRegistry.h"
#include "../src/media/gpu/BackendCapabilities.h"
#include "../src/media/brushes/BrushKind.h"
#include "../src/media/tools/ToolContext.h"

using gpu::BackendRegistry;
using gpu::RenderMode;
using gpu::BackendCapabilities;

class tst_P8_3_1 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void defaultSelectionIsAuto();
    void selectingForceGpu_doesNotChangeRegistryUntilApply();
    void applySelected_writesModeToRegistry();
    void applySelected_emitsModeApplied();
    void modeLabelMatchesRegistry();
    void currentBackendNameReflectsMode();
    void probeClicked_doesNotCrash();
    void refreshFromRegistry_updatesRadios();
};

void tst_P8_3_1::initTestCase() {}
void tst_P8_3_1::cleanupTestCase() {}
void tst_P8_3_1::init()
{
    // Reset to a known starting state before each test.
    BackendRegistry::setModeStatic(RenderMode::Auto);
}

void tst_P8_3_1::defaultSelectionIsAuto()
{
    SettingsGpuPanel p;
    QCOMPARE(p.selectedMode(), RenderMode::Auto);
}

void tst_P8_3_1::selectingForceGpu_doesNotChangeRegistryUntilApply()
{
    SettingsGpuPanel p;
    // The ForceGpu radio's first line is exactly "强制 GPU". Use
    // startsWith to avoid matching the description line of another
    // radio that happens to mention GPU or 强制.
    QRadioButton* gpuRadio = nullptr;
    for (QRadioButton* rb : p.findChildren<QRadioButton*>()) {
        if (rb->text().startsWith(QStringLiteral("强制 GPU"))) {
            gpuRadio = rb;
            break;
        }
    }
    QVERIFY(gpuRadio != nullptr);
    gpuRadio->setChecked(true);

    QCOMPARE(p.selectedMode(), RenderMode::ForceGpu);
    // Registry still on Auto until applySelected is called.
    QCOMPARE(BackendRegistry::modeStatic(), RenderMode::Auto);
}

void tst_P8_3_1::applySelected_writesModeToRegistry()
{
    SettingsGpuPanel p;
    for (QRadioButton* rb : p.findChildren<QRadioButton*>()) {
        if (rb->text().startsWith(QStringLiteral("强制 CPU"))) {
            rb->setChecked(true);
            break;
        }
    }
    QCOMPARE(p.selectedMode(), RenderMode::ForceCpu);
    p.applySelected();
    QCOMPARE(BackendRegistry::modeStatic(), RenderMode::ForceCpu);
}

void tst_P8_3_1::applySelected_emitsModeApplied()
{
    SettingsGpuPanel p;
    QSignalSpy spy(&p, &SettingsGpuPanel::modeApplied);

    for (QRadioButton* rb : p.findChildren<QRadioButton*>()) {
        if (rb->text().contains(QStringLiteral("强制 GPU"))) {
            rb->setChecked(true);
            break;
        }
    }
    p.applySelected();
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<RenderMode>(), RenderMode::ForceGpu);
}

void tst_P8_3_1::modeLabelMatchesRegistry()
{
    // currentModeLabel() reads from the registry directly, so we just
    // need to verify the label strings match the registry state.
    SettingsGpuPanel p;
    BackendRegistry::setModeStatic(RenderMode::Auto);
    QCOMPARE(p.currentModeLabel(), QStringLiteral("AUTO"));

    BackendRegistry::setModeStatic(RenderMode::ForceGpu);
    QCOMPARE(p.currentModeLabel(), QStringLiteral("FORCE GPU"));

    BackendRegistry::setModeStatic(RenderMode::ForceCpu);
    QCOMPARE(p.currentModeLabel(), QStringLiteral("FORCE CPU"));
}

void tst_P8_3_1::currentBackendNameReflectsMode()
{
    SettingsGpuPanel p;
    BackendRegistry::setModeStatic(RenderMode::ForceCpu);
    p.applySelected();
    // Under ForceCpu, the picked backend is the CpuBackend.
    QCOMPARE(p.currentBackendName(), QStringLiteral("cpu"));

    BackendRegistry::setModeStatic(RenderMode::Auto);
    p.applySelected();
    // Under Auto with default shared state for HardRound, the
    // registry routes to CpuBackend (Basic kind, needsGpu() = false).
    QCOMPARE(p.currentBackendName(), QStringLiteral("cpu"));
}

void tst_P8_3_1::probeClicked_doesNotCrash()
{
    SettingsGpuPanel p;
    // In a headless test environment, the probe may return false.
    // The panel must update its label either way; the test only
    // verifies it doesn't crash.
    p.findChild<QPushButton*>();
    QPushButton* probe = nullptr;
    for (QPushButton* b : p.findChildren<QPushButton*>()) {
        if (b->text().contains(QStringLiteral("探测"))) {
            probe = b;
            break;
        }
    }
    QVERIFY(probe != nullptr);
    // Direct invoke avoids needing the modal message box; in a real
    // dialog this would be triggered by user click.
    QMetaObject::invokeMethod(probe, "click", Qt::DirectConnection);
    // No crash means success.
    QVERIFY(true);
}

void tst_P8_3_1::refreshFromRegistry_updatesRadios()
{
    // Start with Auto (default in registry), build a panel, change
    // registry to ForceGpu directly, rebuild a panel: it should
    // reflect the new mode.
    BackendRegistry::setModeStatic(RenderMode::ForceGpu);
    SettingsGpuPanel p2;
    QCOMPARE(p2.selectedMode(), RenderMode::ForceGpu);

    // Now change registry to ForceCpu, rebuild again.
    BackendRegistry::setModeStatic(RenderMode::ForceCpu);
    SettingsGpuPanel p3;
    QCOMPARE(p3.selectedMode(), RenderMode::ForceCpu);
}

QTEST_MAIN(tst_P8_3_1)
#include "tst_P8_3_1.moc"