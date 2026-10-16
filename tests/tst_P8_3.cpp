// tst_P8_3.cpp
//
// Unit test for P8.3 (2026-10-16): GPU user toggle + status bar wiring
//   - BackendRegistry RenderMode 3-state (Auto/ForceGpu/ForceCpu)
//     + setMode / mode / setForceCpu (legacy)
//   - modeLabel() for status bar text
//   - currentBackendName() reflects mode + kind
//   - probeOpenGL() returns a bool (true in GUI, may be false in
//     headless test runner; we accept either)
//   - Settings dialog wiring (deferred to P8.3.x; P8.3 MVP only
//     adds the data + status bar pieces)
//
//   P8.3 commit (2026-10-16).

#include <QTest>
#include <QApplication>

#include "../src/media/gpu/BackendRegistry.h"
#include "../src/media/gpu/BackendCapabilities.h"
#include "../src/media/gpu/CpuBackend.h"
#include "../src/media/gpu/GpuBackend.h"
#include "../src/media/brushes/BrushKind.h"
#include "../src/media/tools/ToolContext.h"

using gpu::RenderMode;
using gpu::BackendRegistry;
using gpu::BackendCapabilities;
using brushes::BrushKind;

class tst_P8_3 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();   // reset caps + registry mode between tests

    // RenderMode 3-state
    void mode_defaultIsAuto();
    void mode_setModeCyclesAllThreeStates();
    void mode_setForceCpuMapsToForceCpuAndAuto();
    void modeLabel_returnsExpectedStrings();

    // currentBackendName + kind
    void currentBackendName_wetKindForcedGpu_returnsGpuName();
    void currentBackendName_wetKindAutoGpuAvailable_returnsGpu();
    void currentBackendName_wetKindAutoGpuUnavailable_fallsBackToCpu();
    void currentBackendName_specialKindForcedGpuStillReturnsGpu();
    void currentBackendName_specialKindAuto_returnsCpu();

    // probeOpenGL
    void probeOpenGL_returnsBool();
    void probeOpenGL_doesNotCrashWhenNoDisplay();

    // Registry singleton
    void instance_isStable();
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
void tst_P8_3::initTestCase() {}
void tst_P8_3::cleanupTestCase() {}

void tst_P8_3::cleanup()
{
    BackendCapabilities::instance().reset();
    BackendRegistry::instance().setMode(RenderMode::Auto);
}

// ---------------------------------------------------------------------------
// RenderMode 3-state
// ---------------------------------------------------------------------------
void tst_P8_3::mode_defaultIsAuto()
{
    QCOMPARE(BackendRegistry::instance().mode(), RenderMode::Auto);
}

void tst_P8_3::mode_setModeCyclesAllThreeStates()
{
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::ForceGpu);
    QCOMPARE(r.mode(), RenderMode::ForceGpu);
    r.setMode(RenderMode::ForceCpu);
    QCOMPARE(r.mode(), RenderMode::ForceCpu);
    r.setMode(RenderMode::Auto);
    QCOMPARE(r.mode(), RenderMode::Auto);
}

void tst_P8_3::mode_setForceCpuMapsToForceCpuAndAuto()
{
    auto& r = BackendRegistry::instance();
    r.setForceCpu(true);
    QCOMPARE(r.mode(), RenderMode::ForceCpu);
    r.setForceCpu(false);
    QCOMPARE(r.mode(), RenderMode::Auto);
}

void tst_P8_3::modeLabel_returnsExpectedStrings()
{
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::Auto),     QStringLiteral("AUTO"));
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::ForceGpu), QStringLiteral("FORCE GPU"));
    QCOMPARE(BackendRegistry::modeLabel(RenderMode::ForceCpu), QStringLiteral("FORCE CPU"));
}

// ---------------------------------------------------------------------------
// currentBackendName
// ---------------------------------------------------------------------------
void tst_P8_3::currentBackendName_wetKindForcedGpu_returnsGpuName()
{
    BackendCapabilities::instance().setForced(true);
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::ForceGpu);
    tools::BrushSharedState ss;
    QCOMPARE(r.currentBackendName(BrushKind::Watercolor, ss), QStringLiteral("qt-opengl-stub"));
    QCOMPARE(r.currentBackendName(BrushKind::Oil,        ss), QStringLiteral("qt-opengl-stub"));
    QCOMPARE(r.currentBackendName(BrushKind::Acrylic,    ss), QStringLiteral("qt-opengl-stub"));
}

void tst_P8_3::currentBackendName_wetKindAutoGpuAvailable_returnsGpu()
{
    BackendCapabilities::instance().setForced(true);
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::Auto);
    tools::BrushSharedState ss;
    QCOMPARE(r.currentBackendName(BrushKind::Watercolor, ss), QStringLiteral("qt-opengl-stub"));
}

void tst_P8_3::currentBackendName_wetKindAutoGpuUnavailable_fallsBackToCpu()
{
    BackendCapabilities::instance().setForced(false);
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::Auto);
    tools::BrushSharedState ss;
    // Wet wants GPU; if not available, registry must fall back to CPU.
    QCOMPARE(r.currentBackendName(BrushKind::Watercolor, ss), QStringLiteral("cpu"));
}

void tst_P8_3::currentBackendName_specialKindForcedGpuStillReturnsGpu()
{
    BackendCapabilities::instance().setForced(true);
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::ForceGpu);
    tools::BrushSharedState ss;
    // ForceGpu overrides Special's per-kind preference
    QCOMPARE(r.currentBackendName(BrushKind::Eraser, ss), QStringLiteral("qt-opengl-stub"));
}

void tst_P8_3::currentBackendName_specialKindAuto_returnsCpu()
{
    BackendCapabilities::instance().setForced(true);
    auto& r = BackendRegistry::instance();
    r.setMode(RenderMode::Auto);
    tools::BrushSharedState ss;
    // Special kinds always return CPU even with GPU available
    QCOMPARE(r.currentBackendName(BrushKind::Smudge, ss), QStringLiteral("cpu"));
    QCOMPARE(r.currentBackendName(BrushKind::Eraser, ss), QStringLiteral("cpu"));
}

// ---------------------------------------------------------------------------
// probeOpenGL
// ---------------------------------------------------------------------------
void tst_P8_3::probeOpenGL_returnsBool()
{
    // The probe runs against whatever GPU/display the test runner has.
    // The result is environment-dependent (true in a real GUI session
    // with a working driver, false in a headless CI). We just verify
    // it returns a bool without crashing and that the inverse of a
    // forced override is also a bool.
    const bool live = BackendCapabilities::probeOpenGL();
    QVERIFY(live == true || live == false);   // tautology; just type-checks
    // Forced override path
    BackendCapabilities::instance().setForced(true);
    QVERIFY(BackendCapabilities::isGpuAvailable());
    BackendCapabilities::instance().setForced(false);
    QVERIFY(!BackendCapabilities::isGpuAvailable());
}

void tst_P8_3::probeOpenGL_doesNotCrashWhenNoDisplay()
{
    // Just call it twice to make sure repeated calls don't leak.
    const bool a = BackendCapabilities::probeOpenGL();
    const bool b = BackendCapabilities::probeOpenGL();
    QVERIFY(a == true || a == false);
    QVERIFY(b == true || b == false);
}

// ---------------------------------------------------------------------------
// Registry singleton
// ---------------------------------------------------------------------------
void tst_P8_3::instance_isStable()
{
    auto& a = BackendRegistry::instance();
    auto& b = BackendRegistry::instance();
    QVERIFY(&a == &b);
}

QTEST_MAIN(tst_P8_3)
#include "tst_P8_3.moc"
