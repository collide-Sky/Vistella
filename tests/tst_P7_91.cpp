// tst_P7_91.cpp
//
// Unit test for P7.9.1 (2026-10-15):
//   - BrushSharedState 9-field struct + needsGpu() rules
//   - ToolContext setters: per-field setters skip-no-change + emit on change,
//     bulk setBrushSharedState emits single brushSettingsChanged when any
//     of the 9 fields differ.
//   - BrushOptionsPanel SharedState 9-field round-trip
//     (applySharedState -> sharedStatePulledIn returns same values)
//
//   P7.9.1 commit (2026-10-15). P7.9.2 follow-up will add 13 dynamic brush
//   types + per-type needsGpu coverage; this file focuses on the data +
//   panel plumbing.

#include <QTest>
#include <QApplication>
#include <QSignalSpy>

#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/BrushOptionsPanel.h"

using tools::BrushSharedState;
using tools::ToolContext;
using tools::BrushOptionsPanel;

class tst_P7_91 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // BrushSharedState defaults + needsGpu() rules (4 case matrix)
    void defaults_nineFieldsArePsStyleDefaults();
    void needsGpu_defaultFalse();
    void needsGpu_smoothingOver50();
    void needsGpu_scatterNonZero();
    void needsGpu_blendModeFivePlus();

    // ToolContext: per-field setters + bulk update
    void setter_perField_emitsOnChange();
    void setter_perField_skipsWhenUnchanged();
    void bulkSet_emitsOnceWhenAnyFieldDiffers();
    void bulkSet_emitsNothingWhenAllIdentical();

    // BrushOptionsPanel: 9-field round-trip
    void panel_roundTripPreservesAllNineFields();
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
void tst_P7_91::initTestCase() {}
void tst_P7_91::cleanupTestCase() {}

void tst_P7_91::defaults_nineFieldsArePsStyleDefaults()
{
    BrushSharedState s;
    QCOMPARE(s.blendMode, 0);
    QCOMPARE(s.opacity,   100.0);
    QCOMPARE(s.flow,      100.0);
    QCOMPARE(s.smoothing, 0.0);
    QCOMPARE(s.angle,     0);
    QCOMPARE(s.brushSize, 13);    // PS default
    QCOMPARE(s.hardness,  50.0);
    QCOMPARE(s.spacing,   25.0);
    QCOMPARE(s.scatter,   0.0);
}

void tst_P7_91::needsGpu_defaultFalse()
{
    BrushSharedState s;
    QVERIFY(!s.needsGpu());
}

void tst_P7_91::needsGpu_smoothingOver50()
{
    BrushSharedState s;
    s.smoothing = 50.0;
    QVERIFY(!s.needsGpu());        // boundary: 50 is NOT > 50
    s.smoothing = 50.0001;
    QVERIFY(s.needsGpu());
    s.smoothing = 75.0;
    QVERIFY(s.needsGpu());
}

void tst_P7_91::needsGpu_scatterNonZero()
{
    BrushSharedState s;
    s.scatter = 0.0;
    QVERIFY(!s.needsGpu());
    s.scatter = 0.5;
    QVERIFY(s.needsGpu());
    s.scatter = 100.0;
    QVERIFY(s.needsGpu());
}

void tst_P7_91::needsGpu_blendModeFivePlus()
{
    BrushSharedState s;
    s.blendMode = 4;
    QVERIFY(!s.needsGpu());        // boundary: 4 is NOT >= 5
    s.blendMode = 5;
    QVERIFY(s.needsGpu());         // dissolve / multiplicative / darken / screen
    s.blendMode = 6;
    QVERIFY(s.needsGpu());
}

// ---------------------------------------------------------------------------
// ToolContext setters
// ---------------------------------------------------------------------------
void tst_P7_91::setter_perField_emitsOnChange()
{
    ToolContext ctx;
    QSignalSpy spy(&ctx, &ToolContext::brushSettingsChanged);

    ctx.setBlendMode(2);
    QCOMPARE(spy.count(), 1);
    ctx.setOpacity(50.0);
    QCOMPARE(spy.count(), 2);
    ctx.setFlow(75.0);
    QCOMPARE(spy.count(), 3);
    ctx.setSmoothing(33.0);
    QCOMPARE(spy.count(), 4);
    ctx.setAngle(45);
    QCOMPARE(spy.count(), 5);
    ctx.setBrushSize(96);
    QCOMPARE(spy.count(), 6);
    // P7.9.1: 3 new fields
    ctx.setHardness(80.0);
    QCOMPARE(spy.count(), 7);
    ctx.setSpacing(40.0);
    QCOMPARE(spy.count(), 8);
    ctx.setScatter(15.0);
    QCOMPARE(spy.count(), 9);
}

void tst_P7_91::setter_perField_skipsWhenUnchanged()
{
    ToolContext ctx;
    ctx.setBlendMode(1);
    ctx.setOpacity(50.0);
    ctx.setHardness(70.0);
    ctx.setSpacing(40.0);
    ctx.setScatter(10.0);
    QSignalSpy spy(&ctx, &ToolContext::brushSettingsChanged);
    const int baseline = spy.count();

    ctx.setBlendMode(1);     // unchanged
    ctx.setOpacity(50.0);    // unchanged
    ctx.setHardness(70.0);   // unchanged
    ctx.setSpacing(40.0);    // unchanged
    ctx.setScatter(10.0);    // unchanged
    QCOMPARE(spy.count(), baseline);  // no extra signals
}

void tst_P7_91::bulkSet_emitsOnceWhenAnyFieldDiffers()
{
    ToolContext ctx;
    QSignalSpy spy(&ctx, &ToolContext::brushSettingsChanged);

    BrushSharedState s;
    s.blendMode  = 3;
    s.hardness   = 70.0;
    s.spacing    = 50.0;
    s.scatter    = 25.0;
    ctx.setBrushSharedState(s);
    QCOMPARE(spy.count(), 1);   // exactly one signal even though 4 fields differ
}

void tst_P7_91::bulkSet_emitsNothingWhenAllIdentical()
{
    ToolContext ctx;
    BrushSharedState initial = ctx.brushSharedState();
    QSignalSpy spy(&ctx, &ToolContext::brushSettingsChanged);

    ctx.setBrushSharedState(initial);
    QCOMPARE(spy.count(), 0);

    // also no signal when re-applying a tiny-but-fuzzy-equal change
    BrushSharedState same = initial;
    same.opacity = initial.opacity;     // qFuzzyCompare self == self
    ctx.setBrushSharedState(same);
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// BrushOptionsPanel: 9-field round-trip
// ---------------------------------------------------------------------------
void tst_P7_91::panel_roundTripPreservesAllNineFields()
{
    BrushOptionsPanel panel;
    BrushOptionsPanel::SharedState in;
    in.blendMode  = 4;
    in.size       = 128;
    in.opacityPct = 87.0;
    in.flowPct    = 65.0;
    in.smoothing  = 42.0;
    in.angle      = 135;
    // P7.9.1: 3 new advanced fields
    in.hardness   = 60.0;
    in.spacing    = 33.0;
    in.scatter    = 18.0;

    panel.applySharedState(in);
    BrushOptionsPanel::SharedState out = panel.sharedStatePulledIn();
    QCOMPARE(out.blendMode,  in.blendMode);
    QCOMPARE(out.size,       in.size);
    QCOMPARE(out.opacityPct, in.opacityPct);
    QCOMPARE(out.flowPct,    in.flowPct);
    QCOMPARE(out.smoothing,  in.smoothing);
    QCOMPARE(out.angle,      in.angle);
    QCOMPARE(out.hardness,   in.hardness);
    QCOMPARE(out.spacing,    in.spacing);
    QCOMPARE(out.scatter,    in.scatter);
}

QTEST_MAIN(tst_P7_91)
#include "tst_P7_91.moc"
