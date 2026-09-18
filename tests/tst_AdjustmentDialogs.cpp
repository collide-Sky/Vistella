// SPDX-License-Identifier: MIT
//
// tst_AdjustmentDialogs - P0-3.2 v2 (2026-09-18)
//
// Coverage for the 4 standalone PS-style adjust dialogs that replaced the
// previous QLabel "TODO" stubs:
//   - CurvesAdjustDialog      (CurveEditor + 4 channels + Reset)
//   - LevelsAdjustDialog      (5 sliders + 4 channels + inLow<inHigh guard)
//   - BlackWhiteAdjustDialog  (6 colour sliders + Tint + swatches)
//   - ChannelMixerAdjustDialog(3x3 matrix + Monochrome + Reset)
// plus the DialogFactory dispatch and the AdjustDialogBase apply flow.
//
#include <QTest>
#include <QSignalSpy>

#include <QApplication>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include "dialogs/AdjustDialogBase.h"
#include "dialogs/CurvesAdjustDialog.h"
#include "dialogs/LevelsAdjustDialog.h"
#include "dialogs/BlackWhiteAdjustDialog.h"
#include "dialogs/ChannelMixerAdjustDialog.h"
#include "dialogs/DialogFactory.h"
#include "imagewindow/AdjustmentPanel.h"   // CurveEditor (used by CurvesAdjustDialog)

using namespace dialogs;

namespace {

void pump(int ms = 30) {
    // Drain pending events so valueChanged cascades and signal-slot connections
    // get a chance to run before assertions.
    QApplication::processEvents();
    QTest::qWait(ms);
    QApplication::processEvents();
}

} // namespace

class tst_AdjustmentDialogs : public QObject
{
    Q_OBJECT

private slots:
    // ---- Curves ----
    void curves_dialog_creation();
    void curves_restores_channel_and_points();
    void curves_curve_change_updates_args();

    // ---- Levels ----
    void levels_initial_values();
    void levels_slider_emits_update();
    void levels_inLow_inHigh_guard();

    // ---- Black & White ----
    void bw_dialog_has_six_sliders_and_tint();
    void bw_slider_change_emits_update();

    // ---- Channel Mixer ----
    void channelMixer_diagonal_default();
    void channelMixer_slider_writes_args();
    void channelMixer_monochrome_emits_args();

    // ---- DialogFactory ----
    void factory_returns_correct_types();
    void factory_knownIds_is_five();

    // ---- AdjustDialogBase.apply flow ----
    void apply_emits_applied_signal();
};

// =====================================================================
// CurvesAdjustDialog
// =====================================================================

void tst_AdjustmentDialogs::curves_dialog_creation()
{
    CurvesAdjustDialog dlg(QVariantMap{});
    QCOMPARE(dlg.windowTitle().isEmpty(), false);

    // The CurveEditor widget is exposed as a child.
    QList<CurveEditor*> editors = dlg.findChildren<CurveEditor*>();
    QCOMPARE(editors.size(), 1);
    QVERIFY(editors.first() != nullptr);

    // Channel box present
    QList<QComboBox*> boxes = dlg.findChildren<QComboBox*>();
    QVERIFY(!boxes.isEmpty());
    QCOMPARE(boxes.first()->count(), 4);   // RGB / Red / Green / Blue
    QCOMPARE(boxes.first()->currentText(), QString("RGB"));
}

void tst_AdjustmentDialogs::curves_restores_channel_and_points()
{
    QVariantMap args;
    args.insert(QString("channel"), QString("Red"));
    QVariantList points;
    QVariantMap p0; p0.insert("x", 0.0); p0.insert("y", 0.0);
    QVariantMap p1; p1.insert("x", 128.0); p1.insert("y", 200.0);
    QVariantMap p2; p2.insert("x", 255.0); p2.insert("y", 255.0);
    points << p0 << p1 << p2;
    args.insert(QString("points"), points);

    CurvesAdjustDialog dlg(args);
    CurveEditor* editor = dlg.findChild<CurveEditor*>();
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->controlPoints().size(), 3);
    QCOMPARE(dlg.findChild<QComboBox*>()->currentText(), QString("Red"));
}

void tst_AdjustmentDialogs::curves_curve_change_updates_args()
{
    CurvesAdjustDialog dlg(QVariantMap{});
    CurveEditor* editor = dlg.findChild<CurveEditor*>();
    QVERIFY(editor != nullptr);

    QSignalSpy previewSpy(&dlg, &AdjustDialogBase::preview);
    QSignalSpy appliedSpy(&dlg, &AdjustDialogBase::applied);

    // Drive the CurveEditor::pointsChanged signal so the dialog updates args.
    // We invoke the private slot via the moc-generated method registry, which
    // is the most reliable way to test the slot pipeline without depending on
    // cursor-coordinate math inside CurveEditor.
    QPolygonF pts;
    pts << QPointF(0, 0) << QPointF(64, 64) << QPointF(255, 255);
    QVERIFY(QMetaObject::invokeMethod(
        &dlg, "onCurveChanged", Qt::DirectConnection,
        Q_ARG(QPolygonF, pts)));
    pump();

    // The internal m_args must now contain a "points" entry with 3 items.
    const QVariantMap args = dlg.currentArgs();
    QVERIFY(args.contains(QString("points")));
    const QVariantList plist = args.value(QString("points")).toList();
    QCOMPARE(plist.size(), 3);

    // Click Apply to confirm the applied() signal carries the same payload.
    QDialogButtonBox* box = dlg.findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    box->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(appliedSpy.count(), 1);
    const QVariantMap appliedArgs = appliedSpy.takeFirst().at(0).toMap();
    QCOMPARE(appliedArgs.value(QString("points")).toList().size(), 3);
}

// =====================================================================
// LevelsAdjustDialog
// =====================================================================

void tst_AdjustmentDialogs::levels_initial_values()
{
    QVariantMap args;
    args.insert(QString("inLow"), 50);
    args.insert(QString("inHigh"), 200);
    args.insert(QString("gamma"), 1.5);
    args.insert(QString("outLow"), 10);
    args.insert(QString("outHigh"), 240);

    LevelsAdjustDialog dlg(args);
    QList<QSlider*> sliders = dlg.findChildren<QSlider*>();
    // 5 sliders expected
    QCOMPARE(sliders.size(), 5);
    // Settle initial restoration order:
    // Find the slider at inLow position (the order we constructed them
    // is inLow, inHigh, gamma, outLow, outHigh).
    auto* inLow  = sliders.at(0);
    auto* inHigh = sliders.at(1);
    auto* gamma  = sliders.at(2);
    auto* outLow = sliders.at(3);
    auto* outHigh= sliders.at(4);
    QCOMPARE(inLow->value(),  50);
    QCOMPARE(inHigh->value(), 200);
    QCOMPARE(gamma->value(),  150);   // 1.5 * 100
    QCOMPARE(outLow->value(), 10);
    QCOMPARE(outHigh->value(),240);
    Q_UNUSED(outLow); Q_UNUSED(outHigh);
}

void tst_AdjustmentDialogs::levels_slider_emits_update()
{
    LevelsAdjustDialog dlg(QVariantMap{});
    QSignalSpy previewSpy(&dlg, &AdjustDialogBase::preview);

    QSlider* inLow = dlg.findChildren<QSlider*>().at(0);
    inLow->setValue(100);
    pump();
    QVERIFY(previewSpy.count() >= 1);
    const QVariantMap args = dlg.currentArgs();
    QCOMPARE(args.value(QString("inLow")).toInt(), 100);
}

void tst_AdjustmentDialogs::levels_inLow_inHigh_guard()
{
    // Cross-over guard: setting inLow to a value >= inHigh should snap it back.
    LevelsAdjustDialog dlg(QVariantMap{});
    QSlider* inLow  = dlg.findChildren<QSlider*>().at(0);
    QSlider* inHigh = dlg.findChildren<QSlider*>().at(1);
    inHigh->setValue(100);
    pump();
    inLow->setValue(150);  // attempting to cross over
    pump();
    QVERIFY(inLow->value() < inHigh->value());
}

// =====================================================================
// BlackWhiteAdjustDialog
// =====================================================================

void tst_AdjustmentDialogs::bw_dialog_has_six_sliders_and_tint()
{
    BlackWhiteAdjustDialog dlg(QVariantMap{});
    QList<QSlider*> sliders = dlg.findChildren<QSlider*>();
    QVERIFY(sliders.size() >= 8);    // 6 colour + 2 tint
    QCOMPARE(sliders.size(), 8);

    // First 6 sliders default to 100.
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(sliders.at(i)->value(), 100);
    }
    // Tint Hue 0..360 defaults to 0, Tint Sat 0..100 defaults to 0.
    QCOMPARE(sliders.at(6)->value(), 0);
    QCOMPARE(sliders.at(7)->value(), 0);
    QCOMPARE(sliders.at(6)->maximum(), 360);
    QCOMPARE(sliders.at(7)->maximum(), 100);
}

void tst_AdjustmentDialogs::bw_slider_change_emits_update()
{
    BlackWhiteAdjustDialog dlg(QVariantMap{});
    QSignalSpy previewSpy(&dlg, &AdjustDialogBase::preview);

    QSlider* s = dlg.findChildren<QSlider*>().at(0);   // reds
    s->setValue(150);
    pump();
    QVERIFY(previewSpy.count() >= 1);
    const QVariantMap args = dlg.currentArgs();
    QCOMPARE(args.value(QString("color0")).toInt(), 150);
}

// =====================================================================
// ChannelMixerAdjustDialog
// =====================================================================

void tst_AdjustmentDialogs::channelMixer_diagonal_default()
{
    ChannelMixerAdjustDialog dlg(QVariantMap{});
    QList<QSlider*> sliders = dlg.findChildren<QSlider*>();
    QCOMPARE(sliders.size(), 9);

    // Diagonal (m00, m11, m22) default to 200.
    QCOMPARE(sliders.at(0)->value(),  200);   // m00 = rR
    QCOMPARE(sliders.at(4)->value(),  200);   // m11 = gG
    QCOMPARE(sliders.at(8)->value(),  200);   // m22 = bB

    // Off-diagonal defaults to 0.
    QCOMPARE(sliders.at(1)->value(),  0);     // m01 = rG
    QCOMPARE(sliders.at(2)->value(),  0);
    QCOMPARE(sliders.at(3)->value(),  0);
    QCOMPARE(sliders.at(5)->value(),  0);
    QCOMPARE(sliders.at(6)->value(),  0);
    QCOMPARE(sliders.at(7)->value(),  0);

    // Monochrome checkbox starts unchecked.
    QCheckBox* box = dlg.findChild<QCheckBox*>();
    QVERIFY(box != nullptr);
    QCOMPARE(box->isChecked(), false);
}

void tst_AdjustmentDialogs::channelMixer_slider_writes_args()
{
    ChannelMixerAdjustDialog dlg(QVariantMap{});
    QSignalSpy previewSpy(&dlg, &AdjustDialogBase::preview);

    QSlider* s = dlg.findChildren<QSlider*>().at(0);   // m00 = rR
    s->setValue(-100);
    pump();
    QVERIFY(previewSpy.count() >= 1);
    const QVariantMap args = dlg.currentArgs();
    QCOMPARE(args.value(QString("m00")).toInt(), -100);
}

void tst_AdjustmentDialogs::channelMixer_monochrome_emits_args()
{
    ChannelMixerAdjustDialog dlg(QVariantMap{});
    QCheckBox* box = dlg.findChild<QCheckBox*>();
    QVERIFY(box != nullptr);
    QSignalSpy previewSpy(&dlg, &AdjustDialogBase::preview);

    box->setChecked(true);
    pump();
    QVERIFY(previewSpy.count() >= 1);
    QCOMPARE(dlg.currentArgs().value(QString("monochrome")).toBool(), true);
}

// =====================================================================
// DialogFactory
// =====================================================================

void tst_AdjustmentDialogs::factory_returns_correct_types()
{
    {
        AdjustDialogBase* d = DialogFactory::create(QString("Curves"), QVariantMap());
        QVERIFY(qobject_cast<CurvesAdjustDialog*>(d) != nullptr);
        delete d;
    }
    {
        AdjustDialogBase* d = DialogFactory::create(QString("Levels"), QVariantMap());
        QVERIFY(qobject_cast<LevelsAdjustDialog*>(d) != nullptr);
        delete d;
    }
    {
        AdjustDialogBase* d = DialogFactory::create(QString("B&W"), QVariantMap());
        QVERIFY(qobject_cast<BlackWhiteAdjustDialog*>(d) != nullptr);
        delete d;
    }
    {
        AdjustDialogBase* d = DialogFactory::create(QString("ChannelMixer"), QVariantMap());
        QVERIFY(qobject_cast<ChannelMixerAdjustDialog*>(d) != nullptr);
        delete d;
    }
    {
        AdjustDialogBase* d = DialogFactory::create(QString("HSL"), QVariantMap());
        QVERIFY(d != nullptr);   // sanity for non-regression
        delete d;
    }
}

void tst_AdjustmentDialogs::factory_knownIds_is_five()
{
    const QStringList ids = DialogFactory::knownIds();
    QCOMPARE(ids.size(), 5);
    QVERIFY(ids.contains(QString("HSL")));
    QVERIFY(ids.contains(QString("Curves")));
    QVERIFY(ids.contains(QString("Levels")));
    QVERIFY(ids.contains(QString("B&W")));
    QVERIFY(ids.contains(QString("ChannelMixer")));
}

// =====================================================================
// AdjustDialogBase.apply (OK / Apply buttons)
// =====================================================================

void tst_AdjustmentDialogs::apply_emits_applied_signal()
{
    LevelsAdjustDialog dlg(QVariantMap{});
    QSignalSpy appliedSpy(&dlg, &AdjustDialogBase::applied);

    // Mutate a slider so the Apply button becomes enabled.
    QSlider* inLow = dlg.findChildren<QSlider*>().at(0);
    inLow->setValue(40);
    pump();

    QDialogButtonBox* box = dlg.findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    QPushButton* applyBtn = box->button(QDialogButtonBox::Apply);
    QVERIFY(applyBtn != nullptr);
    QVERIFY(applyBtn->isEnabled());
    applyBtn->click();
    QCOMPARE(appliedSpy.count(), 1);

    const QVariantMap args = appliedSpy.takeFirst().at(0).toMap();
    QCOMPARE(args.value(QString("inLow")).toInt(), 40);
}

QTEST_MAIN(tst_AdjustmentDialogs)
#include "tst_AdjustmentDialogs.moc"
