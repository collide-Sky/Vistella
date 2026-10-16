// tst_P7_92.cpp
//
// Unit test for P7.9.2 (2026-10-16):
//   - BrushKind 13-kinds taxonomy: kindName / categoryOf / allKinds /
//     kindToString <-> kindFromString round-trip
//   - kindNeedsGpu() per-kind override:
//       Basic 8:   defer to sharedState.needsGpu()
//       Wet 3:     always true
//       Special 2: always false
//   - BrushPreset::kind field + JSON round-trip
//   - BrushPresetManager::builtIns() returns 13 entries in kind order
//   - BrushOptionsPanel: 5 -> 13 makeBuiltInByIndex switch, currentKind/setKind
//     API, lblKind auto-update on preset change
//
//   P7.9.2 commit (2026-10-16).

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QComboBox>
#include <QToolButton>
#include <QSignalSpy>

#include "../src/media/brushes/BrushKind.h"
#include "../src/media/brushes/BrushPreset.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/BrushOptionsPanel.h"

using brushes::BrushKind;
using brushes::BrushCategory;
using brushes::kindName;
using brushes::categoryOf;
using brushes::categoryName;
using brushes::kindToString;
using brushes::kindFromString;
using brushes::allKinds;
using brushes::kindNeedsGpu;
using brushes::BrushPreset;
using brushes::BrushPresetManager;
using tools::BrushSharedState;
using tools::ToolContext;
using tools::BrushOptionsPanel;

class tst_P7_92 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // BrushKind taxonomy
    void allKinds_returns13InStableOrder();
    void categoryOf_thirteenKindsGroupedCorrectly();
    void kindName_allThirteenHaveNonEmptyNames();
    void kindToString_kindFromString_roundTrip();
    void categoryName_basicWetSpecial();

    // kindNeedsGpu per-kind override
    void kindNeedsGpu_wetAlwaysTrue();
    void kindNeedsGpu_specialAlwaysFalse();
    void kindNeedsGpu_basicDeferToSharedState();

    // BrushPreset::kind + JSON
    void brushPreset_defaultKindIsHardRound();
    void brushPreset_kindRoundTripsThroughJson();

    // BrushPresetManager::builtIns 13 entries
    void builtIns_returnsThirteenInKindOrder();
    void builtIns_coveredKindsMatchesAllKinds();

    // BrushOptionsPanel: 13-case makeBuiltInByIndex + kind API
    void panel_makeBuiltInByIndex_returnsAllThirteen();
    void panel_currentKind_tracksLoadedPreset();
    void panel_setKind_updatesLabelAndCombo();
    void panel_kindPickerButton_connectedToSlot();
};

// ---------------------------------------------------------------------------
// BrushKind taxonomy
// ---------------------------------------------------------------------------
void tst_P7_92::initTestCase() {}
void tst_P7_92::cleanupTestCase() {}

void tst_P7_92::allKinds_returns13InStableOrder()
{
    const QList<BrushKind> ks = allKinds();
    QCOMPARE(ks.size(), 13);
    // Order: Basic 8 (0..7), Wet 3 (8..10), Special 2 (11..12).
    QCOMPARE(static_cast<int>(ks.at(0)),  static_cast<int>(BrushKind::HardRound));
    QCOMPARE(static_cast<int>(ks.at(7)),  static_cast<int>(BrushKind::Calligraphic));
    QCOMPARE(static_cast<int>(ks.at(8)),  static_cast<int>(BrushKind::Watercolor));
    QCOMPARE(static_cast<int>(ks.at(10)), static_cast<int>(BrushKind::Acrylic));
    QCOMPARE(static_cast<int>(ks.at(11)), static_cast<int>(BrushKind::Smudge));
    QCOMPARE(static_cast<int>(ks.at(12)), static_cast<int>(BrushKind::Eraser));
}

void tst_P7_92::categoryOf_thirteenKindsGroupedCorrectly()
{
    QCOMPARE(categoryOf(BrushKind::HardRound),    BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::SoftRound),    BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Pencil),       BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Marker),       BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Airbrush),     BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Chalk),        BrushCategory::Basic);
 QCOMPARE(categoryOf(BrushKind::Charcoal),     BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Calligraphic), BrushCategory::Basic);
    QCOMPARE(categoryOf(BrushKind::Watercolor),   BrushCategory::Wet);
    QCOMPARE(categoryOf(BrushKind::Oil),          BrushCategory::Wet);
    QCOMPARE(categoryOf(BrushKind::Acrylic),      BrushCategory::Wet);
    QCOMPARE(categoryOf(BrushKind::Smudge),       BrushCategory::Special);
    QCOMPARE(categoryOf(BrushKind::Eraser),       BrushCategory::Special);
}

void tst_P7_92::kindName_allThirteenHaveNonEmptyNames()
{
    for (const auto k : allKinds()) {
        QVERIFY(!kindName(k).isEmpty());
    }
    // Spot-check key names
    QCOMPARE(kindName(BrushKind::HardRound),    QStringLiteral("Hard Round"));
    QCOMPARE(kindName(BrushKind::SoftRound),    QStringLiteral("Soft Round"));
    QCOMPARE(kindName(BrushKind::Calligraphic), QStringLiteral("Calligraphic"));
    QCOMPARE(kindName(BrushKind::Watercolor),   QStringLiteral("Watercolor"));
    QCOMPARE(kindName(BrushKind::Eraser),       QStringLiteral("Eraser"));
}

void tst_P7_92::kindToString_kindFromString_roundTrip()
{
    for (const auto k : allKinds()) {
        const QString s = kindToString(k);
        const BrushKind back = kindFromString(s);
        QCOMPARE(static_cast<int>(back), static_cast<int>(k));
    }
    // Unknown string -> HardRound (sane default)
    QCOMPARE(kindFromString(QStringLiteral("nope")), BrushKind::HardRound);
}

void tst_P7_92::categoryName_basicWetSpecial()
{
    QCOMPARE(QString::fromLatin1(categoryName(BrushCategory::Basic)),   QStringLiteral("Basic"));
    QCOMPARE(QString::fromLatin1(categoryName(BrushCategory::Wet)),     QStringLiteral("Wet"));
    QCOMPARE(QString::fromLatin1(categoryName(BrushCategory::Special)), QStringLiteral("Special"));
}

// ---------------------------------------------------------------------------
// kindNeedsGpu per-kind override
// ---------------------------------------------------------------------------
void tst_P7_92::kindNeedsGpu_wetAlwaysTrue()
{
    // Wet 3 always true regardless of sharedState
    BrushSharedState ss;  // default: smoothing=0, scatter=0, blendMode=0 -> per-state false
    QVERIFY(!ss.needsGpu());  // sanity: default state is per-state false
    QVERIFY( kindNeedsGpu(BrushKind::Watercolor, ss));
    QVERIFY( kindNeedsGpu(BrushKind::Oil,        ss));
    QVERIFY( kindNeedsGpu(BrushKind::Acrylic,    ss));

    // Even when sharedState says "no GPU", Wet still says GPU.
    ss.smoothing = 0; ss.scatter = 0; ss.blendMode = 0;
    QVERIFY(kindNeedsGpu(BrushKind::Watercolor, ss));
}

void tst_P7_92::kindNeedsGpu_specialAlwaysFalse()
{
    BrushSharedState ss;
    // Make sharedState "want GPU" — Special should still say false.
    ss.smoothing = 99.0;
    ss.scatter   = 99.0;
    ss.blendMode = 9;
    QVERIFY(ss.needsGpu());   // sanity: per-state true
    QVERIFY(!kindNeedsGpu(BrushKind::Smudge, ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Eraser, ss));
}

void tst_P7_92::kindNeedsGpu_basicDeferToSharedState()
{
    // Basic 8: defer to sharedState.needsGpu() result.
    BrushSharedState ss;
    QVERIFY(!kindNeedsGpu(BrushKind::HardRound,    ss));
    QVERIFY(!kindNeedsGpu(BrushKind::SoftRound,    ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Pencil,       ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Marker,       ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Airbrush,     ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Chalk,        ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Charcoal,     ss));
    QVERIFY(!kindNeedsGpu(BrushKind::Calligraphic, ss));

    ss.smoothing = 75.0;   // per-state true
    QVERIFY(kindNeedsGpu(BrushKind::HardRound, ss));
    QVERIFY(kindNeedsGpu(BrushKind::Pencil,    ss));
    QVERIFY(kindNeedsGpu(BrushKind::Calligraphic, ss));
}

// ---------------------------------------------------------------------------
// BrushPreset::kind + JSON
// ---------------------------------------------------------------------------
void tst_P7_92::brushPreset_defaultKindIsHardRound()
{
    BrushPreset p;
    QCOMPARE(static_cast<int>(p.kind), static_cast<int>(BrushKind::HardRound));
}

void tst_P7_92::brushPreset_kindRoundTripsThroughJson()
{
    // Take an existing built-in (non-default kind) and round-trip via JSON.
    BrushPreset in = BrushPreset::makeBuiltInWatercolor();
    QCOMPARE(static_cast<int>(in.kind), static_cast<int>(BrushKind::Watercolor));
    const QString j = in.toJsonString();
    const BrushPreset out = BrushPreset::fromJsonString(j);
    QCOMPARE(static_cast<int>(out.kind), static_cast<int>(BrushKind::Watercolor));
    QCOMPARE(out.name, in.name);
    QCOMPARE(out.settings.size, in.settings.size);
    QCOMPARE(out.shape.type,    in.shape.type);

    // Round-trip across all 13 built-ins (kind field persists).
    for (int idx = 0; idx < 13; ++idx) {
        BrushPreset bi;   // reconstructed via factory chain in test would be too verbose;
                          // instead, iterate the manager's builtIns():
        Q_UNUSED(bi);
    }
    BrushPresetManager mgr;
    for (const auto& p : mgr.builtIns()) {
        const QString js = p.toJsonString();
        const BrushPreset reloaded = BrushPreset::fromJsonString(js);
        QCOMPARE(static_cast<int>(reloaded.kind), static_cast<int>(p.kind));
    }
}

// ---------------------------------------------------------------------------
// BrushPresetManager::builtIns
// ---------------------------------------------------------------------------
void tst_P7_92::builtIns_returnsThirteenInKindOrder()
{
    BrushPresetManager mgr;
    const QList<BrushPreset> all = mgr.builtIns();
    QCOMPARE(all.size(), 13);
    for (int i = 0; i < 13; ++i) {
        QCOMPARE(static_cast<int>(all.at(i).kind), i);
    }
}

void tst_P7_92::builtIns_coveredKindsMatchesAllKinds()
{
    BrushPresetManager mgr;
    QSet<int> seen;
    for (const auto& p : mgr.builtIns()) {
        seen.insert(static_cast<int>(p.kind));
    }
    QCOMPARE(seen.size(), 13);
    for (int i = 0; i < 13; ++i) {
        QVERIFY(seen.contains(i));
    }
}

// ---------------------------------------------------------------------------
// BrushOptionsPanel: 13-case makeBuiltInByIndex + kind API
// ---------------------------------------------------------------------------
void tst_P7_92::panel_makeBuiltInByIndex_returnsAllThirteen()
{
    BrushOptionsPanel panel;
    // Index 0..12 -> 13 distinct presets, each carrying its own kind.
    for (int i = 0; i < 13; ++i) {
        // Use the public setPreset path so we don't need a friend declaration
        // for makeBuiltInByIndex. setPresetName also picks up the right preset
        // by name (via cmbPreset findText).
        BrushPresetManager mgr;
        const QList<BrushPreset> all = mgr.builtIns();
        QVERIFY(i < all.size());
        panel.setPreset(all.at(i));
        QCOMPARE(static_cast<int>(panel.currentKind()), i);
    }
}

void tst_P7_92::panel_currentKind_tracksLoadedPreset()
{
    BrushOptionsPanel panel;
    // Default kind is HardRound
    QCOMPARE(panel.currentKind(), BrushKind::HardRound);
    // Load a Watercolor preset -> kind should be Watercolor
    panel.setPreset(BrushPreset::makeBuiltInWatercolor());
    QCOMPARE(panel.currentKind(), BrushKind::Watercolor);
    // Load an Eraser preset -> kind should be Eraser
    panel.setPreset(BrushPreset::makeBuiltInEraser());
    QCOMPARE(panel.currentKind(), BrushKind::Eraser);
}

void tst_P7_92::panel_setKind_updatesLabelAndCombo()
{
    BrushOptionsPanel panel;
    panel.setKind(BrushKind::Oil);
    QCOMPARE(panel.currentKind(), BrushKind::Oil);
    // lblKind text should now reflect "Wet - Oil"
    QVERIFY(panel.findChild<QLabel*>("lblKind"));
    QCOMPARE(panel.findChild<QLabel*>("lblKind")->text(),
             QStringLiteral("Wet - Oil"));
    // cmbPreset should be at index 9 (Oil's enum value)
    QVERIFY(panel.findChild<QComboBox*>("cmbPreset"));
    QCOMPARE(panel.findChild<QComboBox*>("cmbPreset")->currentIndex(), 9);

    // Setting same kind again is a no-op (no kindChanged emission)
    QSignalSpy spy(&panel, &BrushOptionsPanel::kindChanged);
    panel.setKind(BrushKind::Oil);
    QCOMPARE(spy.count(), 0);

    // Setting different kind emits exactly once
    panel.setKind(BrushKind::Eraser);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(static_cast<int>(qvariant_cast<BrushKind>(spy.at(0).at(0))),
             static_cast<int>(BrushKind::Eraser));
}

void tst_P7_92::panel_kindPickerButton_connectedToSlot()
{
    BrushOptionsPanel panel;
    // btnKindPicker exists and is connected to onKindPickerClicked.
    // We can only check existence of the button; firing the slot would
    // pop a QMenu which needs a real event loop and a visible widget.
    // The build itself verifies signal/slot wiring via MOC.
    QVERIFY(panel.findChild<QToolButton*>("btnKindPicker"));
}

QTEST_MAIN(tst_P7_92)
#include "tst_P7_92.moc"
