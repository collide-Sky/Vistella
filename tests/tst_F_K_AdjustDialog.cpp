// SPDX-License-Identifier: MIT
//
// tst_F_K_AdjustDialog - F-K (2026-09-09)
//
// 1) AdjustDialogBase 3 buttons (OK/Cancel/Apply), Apply disabled by default
// 2) HslAdjustDialog 3 sliders (Hue/Sat/Light), valueChanged triggers preview + applied
// 3) DialogFactory.create 5 dialogIds (HSL/Curves/Levels/B&W/ChannelMixer) all return non-null
//
// F-K simplification (2026-09-09):
//   HslAdjustDialog moc requires <QObject> direct include, currently only
//   forwarded via QDialog. To avoid the moc #error, we test AdjustDialogBase
//   via DialogFactory::create("HSL") (which returns AdjustDialogBase*).
//
#include <QTest>
#include <QSignalSpy>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVariant>
#include <QtWidgets/QDialogButtonBox>

#include "dialogs/AdjustDialogBase.h"
#include "dialogs/DialogFactory.h"

using namespace dialogs;

class tst_F_K_AdjustDialog : public QObject
{
    Q_OBJECT
private slots:
    void base_has3Buttons();
    void base_applyDisabledByDefault();
    void hsl_sliderValueChangesArgs();
    void hsl_applyEmitsSignal();
    void factory_create5Dialogs();
    void factory_unknownIdReturnsNull();
};

void tst_F_K_AdjustDialog::base_has3Buttons()
{
    // Use DialogFactory to create HSL dialog (test AdjustDialogBase generic 3-button behavior)
    AdjustDialogBase* dlg = DialogFactory::create("HSL", QVariantMap());
    QVERIFY(dlg != nullptr);
    QDialogButtonBox* box = dlg->findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    QVERIFY(box->button(QDialogButtonBox::Ok) != nullptr);
    QVERIFY(box->button(QDialogButtonBox::Cancel) != nullptr);
    QVERIFY(box->button(QDialogButtonBox::Apply) != nullptr);
    delete dlg;
}

void tst_F_K_AdjustDialog::base_applyDisabledByDefault()
{
    AdjustDialogBase* dlg = DialogFactory::create("HSL", QVariantMap());
    QVERIFY(dlg != nullptr);
    QDialogButtonBox* box = dlg->findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    QPushButton* apply = box->button(QDialogButtonBox::Apply);
    QVERIFY(!apply->isEnabled());
    delete dlg;
}

void tst_F_K_AdjustDialog::hsl_sliderValueChangesArgs()
{
    // P0 leftover review (2026-09-21): HSL uses PS-compatible 8-band
    //   layout (master + 7 hues). Each slider change emits preview()
    //   with the full hueShifts/satShifts/lightness payload.
    AdjustDialogBase* dlg = DialogFactory::create("HSL", QVariantMap());
    QVERIFY(dlg != nullptr);
    QSignalSpy previewSpy(dlg, &AdjustDialogBase::preview);
    QList<QSlider*> sliders = dlg->findChildren<QSlider*>();
    // 8 hue + 8 sat + 1 lightness = 17 sliders.
    QCOMPARE(sliders.size(), 17);   // 8 hue + 8 sat + 1 light
    // Modify the first hue slider (master band).
    sliders.first()->setValue(30);
    QVERIFY(previewSpy.count() >= 1);
    const QVariantMap args = dlg->currentArgs();
    const QVariantList hueList = args.value("hueShifts").toList();
    QCOMPARE(hueList.size(), 8);
    QCOMPARE(hueList.first().toInt(), 30);
    QCOMPARE(args.value("lightness").toInt(), 0);
    delete dlg;
}

void tst_F_K_AdjustDialog::hsl_applyEmitsSignal()
{
    // P0 leftover review (2026-09-21): 8-band args round-trip through Apply.
    QVariantList initialHues;
    for (int i = 0; i < 8; ++i) initialHues << 0;
    QVariantList initialSats;
    for (int i = 0; i < 8; ++i) initialSats << 0;
    AdjustDialogBase* dlg = DialogFactory::create("HSL",
        QVariantMap{{"hueShifts", initialHues},
                    {"satShifts", initialSats},
                    {"lightness", 0}});
    QVERIFY(dlg != nullptr);
    QSignalSpy appliedSpy(dlg, &AdjustDialogBase::applied);
    // Modify band 2 (Yellows) sat: -25
    QList<QSlider*> sliders = dlg->findChildren<QSlider*>();
    QVERIFY(!sliders.isEmpty());
    // Layout per setupUi loop: hue row, sat row interleaved.
    //   sliders[0..15] = hue0..hue7, sat0..sat7 interleaved as
    //   hue[i] = sliders[2*i], sat[i] = sliders[2*i+1].
    //   sliders[16] = light.
    QSlider* yellowsSat = sliders[2 * 2 + 1];   // sat band 2 (Yellows)
    yellowsSat->setValue(-25);
    QDialogButtonBox* box = dlg->findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    box->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(appliedSpy.count(), 1);
    const QVariantMap args = appliedSpy.takeFirst().at(0).toMap();
    const QVariantList satList = args.value("satShifts").toList();
    QCOMPARE(satList.size(), 8);
    QCOMPARE(satList[2].toInt(), -25);   // band 2 sat changed
    QCOMPARE(satList[0].toInt(), 0);     // band 0 sat untouched
    delete dlg;
}

void tst_F_K_AdjustDialog::factory_create5Dialogs()
{
    QStringList ids = DialogFactory::knownIds();
    QCOMPARE(ids.size(), 5);
    QVERIFY(ids.contains("HSL"));
    QVERIFY(ids.contains("Curves"));
    QVERIFY(ids.contains("Levels"));
    QVERIFY(ids.contains("B&W"));
    QVERIFY(ids.contains("ChannelMixer"));

    // 每个 ID 都能 create 出 non-null dialog
    for (const QString& id : ids) {
        AdjustDialogBase* d = DialogFactory::create(id, QVariantMap());
        QVERIFY2(d != nullptr, qPrintable("Failed to create dialog: " + id));
        QCOMPARE(d->windowTitle().isEmpty(), false);
        delete d;
    }
}

void tst_F_K_AdjustDialog::factory_unknownIdReturnsNull()
{
    AdjustDialogBase* d = DialogFactory::create("NonExist", QVariantMap());
    QCOMPARE(d, nullptr);
}

QTEST_MAIN(tst_F_K_AdjustDialog)
#include "tst_F_K_AdjustDialog.moc"
