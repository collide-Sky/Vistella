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
    // Cast to AdjustDialogBase (no HslAdjustDialog-specific access)
    AdjustDialogBase* dlg = DialogFactory::create("HSL", QVariantMap());
    QVERIFY(dlg != nullptr);
    QSignalSpy previewSpy(dlg, &AdjustDialogBase::preview);
    QList<QSlider*> sliders = dlg->findChildren<QSlider*>();
    QVERIFY(!sliders.isEmpty());
    QSlider* hueSlider = sliders.first();
    // 改 hue slider: 0 -> 30
    hueSlider->setValue(30);
    // preview 应 emit
    QVERIFY(previewSpy.count() >= 1);
    // currentArgs 含 hue
    const QVariantMap args = dlg->currentArgs();
    QCOMPARE(args.value("hue").toInt(), 30);
    delete dlg;
}

void tst_F_K_AdjustDialog::hsl_applyEmitsSignal()
{
    AdjustDialogBase* dlg = DialogFactory::create("HSL", QVariantMap({{"hue", 0}}));
    QVERIFY(dlg != nullptr);
    QSignalSpy appliedSpy(dlg, &AdjustDialogBase::applied);
    QList<QSlider*> sliders = dlg->findChildren<QSlider*>();
    QVERIFY(!sliders.isEmpty());
    QSlider* hue = sliders.first();
    hue->setValue(45);
    // 点 Apply
    QDialogButtonBox* box = dlg->findChild<QDialogButtonBox*>();
    QVERIFY(box != nullptr);
    box->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(appliedSpy.count(), 1);
    // args 含 hue=45
    const QVariantMap args = appliedSpy.takeFirst().at(0).toMap();
    QCOMPARE(args.value("hue").toInt(), 45);
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
