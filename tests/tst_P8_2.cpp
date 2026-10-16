// tst_P8_2.cpp
//
// Unit test for P8.2 (2026-10-16): GPU/CPU backend abstraction
//   - CpuBackend: per-tile pass-through (input clones to output)
//   - GpuBackend stub: per-tile pass-through, isAvailable()
//     delegates to BackendCapabilities
//   - BackendCapabilities: startup probe, setForced for tests,
//     setGpuFailed for permanent fallback
//   - BackendRegistry::pick(BrushKind, BrushSharedState):
//       Basic 8   defer to sharedState.needsGpu()
//       Wet 3     prefer GPU (falls back to CPU if not available)
//       Special 2 always CPU
//       setForceCpu overrides everything
//       setKindOverride per-kind (P8.3 hooks here)
//
//   P8.2 commit (2026-10-16).

#include <QTest>
#include <QApplication>

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>   // cv::Vec3b

using cv::Vec3b;

#include "../src/media/gpu/RenderBackend.h"
#include "../src/media/gpu/CpuBackend.h"
#include "../src/media/gpu/GpuBackend.h"
#include "../src/media/gpu/BackendCapabilities.h"
#include "../src/media/gpu/BackendRegistry.h"

#include "../src/media/brushes/BrushKind.h"
#include "../src/media/tools/ToolContext.h"

using gpu::RenderBackend;
using gpu::CpuBackend;
using gpu::GpuBackend;
using gpu::BackendCapabilities;
using gpu::BackendRegistry;
using brushes::BrushKind;
using brushes::BrushCategory;
using brushes::allKinds;
using brushes::kindNeedsGpu;
using tools::BrushSharedState;

class tst_P8_2 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();   // reset BackendCapabilities between tests

    // CpuBackend
    void cpuBackend_nameIsCpu();
    void cpuBackend_alwaysAvailable();
    void cpuBackend_processTile_clonesInput();
    void cpuBackend_processTile_preservesTypeAndSize();

    // GpuBackend stub
    void gpuBackend_stubName();
    void gpuBackend_isAvailableFollowsCapabilities();

    // BackendCapabilities
    void backendCapabilities_defaultIsNotAvailable();
    void backendCapabilities_setForcedToggles();
    void backendCapabilities_setGpuFailedLocksOff();
    void backendCapabilities_resetRestoresDefault();
    void backendCapabilities_summaryIncludesState();

    // BackendRegistry::pick
    void registry_basicKindDefaultStatePicksCpu();
    void registry_basicKindWithStateNeedsGpuPicksGpuWhenAvailable();
    void registry_wetKindAlwaysPicksGpuWhenAvailable();
    void registry_wetKindFallsBackToCpuWhenGpuUnavailable();
    void registry_specialKindAlwaysPicksCpu();
    void registry_forceCpuOverridesEverything();
    void registry_kindOverrideRoutesCorrectly();
    void registry_describeDecision_containsCategoryAndKind();
    void registry_cpuAndGpuAccessorsReturnNonNull();

    // end-to-end: pick + processTile roundtrip via the registry
    void registry_wetKind_pipelineProducesIdenticalOutput();
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
void tst_P8_2::initTestCase() {}
void tst_P8_2::cleanupTestCase() {}

void tst_P8_2::cleanup()
{
    // Make sure no test leaks forced state into the next one.
    BackendCapabilities::instance().reset();
}

// ---------------------------------------------------------------------------
// CpuBackend
// ---------------------------------------------------------------------------
void tst_P8_2::cpuBackend_nameIsCpu()
{
    CpuBackend b;
    QCOMPARE(b.name(), QStringLiteral("cpu"));
}

void tst_P8_2::cpuBackend_alwaysAvailable()
{
    CpuBackend b;
    QVERIFY(b.isAvailable());
}

void tst_P8_2::cpuBackend_processTile_clonesInput()
{
    CpuBackend b;
    cv::Mat in(64, 64, CV_8UC3, cv::Scalar(11, 22, 33));
    gpu::RenderBackendContext ctx;
    cv::Mat out = b.processTile({0, 0, 0, 1}, in, ctx);
    QVERIFY(!out.empty());
    QCOMPARE((int)out.at<Vec3b>(0, 0)[0], 11);
    QCOMPARE((int)out.at<Vec3b>(0, 0)[1], 22);
    QCOMPARE((int)out.at<Vec3b>(0, 0)[2], 33);
}

void tst_P8_2::cpuBackend_processTile_preservesTypeAndSize()
{
    CpuBackend b;
    cv::Mat in(128, 64, CV_8UC4, cv::Scalar(0, 0, 0, 255));
    gpu::RenderBackendContext ctx;
    cv::Mat out = b.processTile({0, 0, 0, 1}, in, ctx);
    QCOMPARE(out.size(), in.size());
    QCOMPARE(out.type(), in.type());
}

// ---------------------------------------------------------------------------
// GpuBackend stub
// ---------------------------------------------------------------------------
void tst_P8_2::gpuBackend_stubName()
{
    GpuBackend b;   // default ctor: no provider
    QCOMPARE(b.name(), QStringLiteral("qt-opengl-stub"));
}

void tst_P8_2::gpuBackend_isAvailableFollowsCapabilities()
{
    BackendCapabilities::instance().setForced(true, "test");
    GpuBackend b(&BackendCapabilities::isGpuAvailable);
    QVERIFY(b.isAvailable());

    BackendCapabilities::instance().setForced(false, "test");
    QVERIFY(!b.isAvailable());
}

// ---------------------------------------------------------------------------
// BackendCapabilities
// ---------------------------------------------------------------------------
void tst_P8_2::backendCapabilities_defaultIsNotAvailable()
{
    BackendCapabilities::instance().reset();
    QVERIFY(!BackendCapabilities::isGpuAvailable());
}

void tst_P8_2::backendCapabilities_setForcedToggles()
{
    auto& c = BackendCapabilities::instance();
    c.setForced(true);
    QVERIFY(BackendCapabilities::isGpuAvailable());
    QVERIFY(c.reason().contains("forced"));
    c.setForced(false);
    QVERIFY(!BackendCapabilities::isGpuAvailable());
}

void tst_P8_2::backendCapabilities_setGpuFailedLocksOff()
{
    auto& c = BackendCapabilities::instance();
    c.setForced(true);
    QVERIFY(BackendCapabilities::isGpuAvailable());
    c.setGpuFailed("shader compile crash");
    QVERIFY(!BackendCapabilities::isGpuAvailable());
    QVERIFY(c.reason().contains("shader compile crash"));
}

void tst_P8_2::backendCapabilities_resetRestoresDefault()
{
    auto& c = BackendCapabilities::instance();
    c.setForced(true);
    c.reset();
    QVERIFY(!BackendCapabilities::isGpuAvailable());
    QVERIFY(c.reason().contains("no provider"));
}

void tst_P8_2::backendCapabilities_summaryIncludesState()
{
    auto& c = BackendCapabilities::instance();
    c.setForced(true);
    const auto s = c.summary();
    QVERIFY(s.size() >= 2);
    QVERIFY(s.at(0).contains("yes"));
    c.setForced(false);
    const auto s2 = c.summary();
    QVERIFY(s2.at(0).contains("no"));
}

// ---------------------------------------------------------------------------
// BackendRegistry::pick
// ---------------------------------------------------------------------------
void tst_P8_2::registry_basicKindDefaultStatePicksCpu()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    BrushSharedState ss;   // default = no per-state GPU need
    RenderBackend* b = r.pick(BrushKind::HardRound, ss);
    QVERIFY(b == r.cpuBackend());
    QVERIFY(b->name() == QStringLiteral("cpu"));
}

void tst_P8_2::registry_basicKindWithStateNeedsGpuPicksGpuWhenAvailable()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    BrushSharedState ss;
    ss.smoothing = 99.0;   // per-state GPU needed
    RenderBackend* b = r.pick(BrushKind::Pencil, ss);
    QVERIFY(b == r.gpuBackend());
    QVERIFY(b->name() == QStringLiteral("qt-opengl-stub"));
}

void tst_P8_2::registry_wetKindAlwaysPicksGpuWhenAvailable()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    BrushSharedState ss;
    for (const auto k : {BrushKind::Watercolor, BrushKind::Oil, BrushKind::Acrylic}) {
        RenderBackend* b = r.pick(k, ss);
        QVERIFY2(b == r.gpuBackend(), qPrintable(QString::fromLatin1("kind %1").arg(static_cast<int>(k))));
    }
}

void tst_P8_2::registry_wetKindFallsBackToCpuWhenGpuUnavailable()
{
    BackendCapabilities::instance().setForced(false);
    BackendRegistry r;
    BrushSharedState ss;
    // Even though Wet kind wants GPU, GpuBackend reports !isAvailable() so
    // the registry must fall back to CPU rather than return null.
    for (const auto k : {BrushKind::Watercolor, BrushKind::Oil, BrushKind::Acrylic}) {
        RenderBackend* b = r.pick(k, ss);
        QVERIFY2(b == r.cpuBackend(), qPrintable(QString::fromLatin1("kind %1").arg(static_cast<int>(k))));
    }
}

void tst_P8_2::registry_specialKindAlwaysPicksCpu()
{
    BackendCapabilities::instance().setForced(true);   // even with GPU
    BackendRegistry r;
    BrushSharedState ss;
    QVERIFY(r.pick(BrushKind::Smudge, ss) == r.cpuBackend());
    QVERIFY(r.pick(BrushKind::Eraser, ss) == r.cpuBackend());
}

void tst_P8_2::registry_forceCpuOverridesEverything()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    r.setForceCpu(true);
    BrushSharedState ss;
    // Even Wet kinds -> CPU because of force override
    QVERIFY(r.pick(BrushKind::Watercolor, ss) == r.cpuBackend());
    QVERIFY(r.pick(BrushKind::Oil,        ss) == r.cpuBackend());
}

void tst_P8_2::registry_kindOverrideRoutesCorrectly()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    // Override Watercolor to force CPU (e.g. user has the toggle off)
    r.setKindOverride(BrushKind::Watercolor, false);
    QVERIFY(r.pick(BrushKind::Watercolor, {}) == r.cpuBackend());
    // Override Pencil to prefer GPU even though Basic defaults to CPU
    r.setKindOverride(BrushKind::Pencil, true);
    QVERIFY(r.pick(BrushKind::Pencil, {}) == r.gpuBackend());
}

void tst_P8_2::registry_describeDecision_containsCategoryAndKind()
{
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    const auto desc = r.describeDecision(BrushKind::Watercolor, {});
    QVERIFY(desc.contains("Wet"));
    QVERIFY(desc.contains("Watercolor"));
    QVERIFY(desc.contains("->"));
    QVERIFY(desc.contains("qt-opengl-stub"));
}

void tst_P8_2::registry_cpuAndGpuAccessorsReturnNonNull()
{
    BackendRegistry r;
    QVERIFY(r.cpuBackend() != nullptr);
    QVERIFY(r.gpuBackend() != nullptr);
    QVERIFY(r.cpuBackend() != r.gpuBackend());
}

// ---------------------------------------------------------------------------
// end-to-end
// ---------------------------------------------------------------------------
void tst_P8_2::registry_wetKind_pipelineProducesIdenticalOutput()
{
    // With a stub GPU, the per-tile output is the same as the input.
    // The registry routes Wet to GPU; processTile on the GPU stub
    // clones input, so we get the original tile back.
    BackendCapabilities::instance().setForced(true);
    BackendRegistry r;
    cv::Mat tile(64, 64, CV_8UC3, cv::Scalar(7, 14, 21));
    BrushSharedState ss;
    RenderBackend* b = r.pick(BrushKind::Watercolor, ss);
    gpu::RenderBackendContext ctx;
    cv::Mat out = b->processTile({0, 0, 0, 1}, tile, ctx);
    QVERIFY(!out.empty());
    QCOMPARE(out.size(), tile.size());
    QCOMPARE((int)out.at<Vec3b>(0, 0)[0], 7);
    QCOMPARE((int)out.at<Vec3b>(0, 0)[1], 14);
    QCOMPARE((int)out.at<Vec3b>(0, 0)[2], 21);
}

QTEST_MAIN(tst_P8_2)
#include "tst_P8_2.moc"
