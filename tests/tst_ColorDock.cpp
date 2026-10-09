// SPDX-License-Identifier: MIT
//
// tst_ColorDock - P6 (2026-10-09)
//
// Verifies the new P6 widgets in ColorDock (Swap / Default buttons +
// foreground/background signal emission). Existing F-H / F-M.1 / F-M.2
// tests in tst_F_M_Dock_GradientPatternChannel cover the palette +
// gradient + pattern paths; this file targets the P6 Surface only.
//

#include <QTest>
#include <QSignalSpy>
#include <QPushButton>

#include "../src/media/docks/ColorDock.h"

using namespace docks;

class tst_ColorDock : public QObject
{
    Q_OBJECT
private slots:
    void ctor_hasSwapAndDefaultButtons();
    void swap_click_emitsBothColorSignals();
    void default_click_emitsBothColorSignals();
};

void tst_ColorDock::ctor_hasSwapAndDefaultButtons()
{
    ColorDock dock;
    QVERIFY(dock.findChild<QPushButton*>("swapBtn") != nullptr);
    QVERIFY(dock.findChild<QPushButton*>("defaultBtn") != nullptr);
    QVERIFY(dock.findChild<QPushButton*>("pickBtn") != nullptr);
}

void tst_ColorDock::swap_click_emitsBothColorSignals()
{
    ColorDock dock;
    QSignalSpy fgSpy(&dock, &ColorDock::foregroundColorChanged);
    QSignalSpy bgSpy(&dock, &ColorDock::backgroundColorChanged);

    auto* swapBtn = dock.findChild<QPushButton*>("swapBtn");
    QVERIFY(swapBtn != nullptr);

    const QColor origFg = dock.foregroundColor();
    const QColor origBg = dock.backgroundColor();
    QVERIFY(origFg != origBg);

    swapBtn->click();
    QCOMPARE(fgSpy.count(), 1);
    QCOMPARE(bgSpy.count(), 1);
    QCOMPARE(dock.foregroundColor(), origBg);
    QCOMPARE(dock.backgroundColor(), origFg);
}

void tst_ColorDock::default_click_emitsBothColorSignals()
{
    ColorDock dock;

    auto* defBtn = dock.findChild<QPushButton*>("defaultBtn");
    QVERIFY(defBtn != nullptr);

    // First put it into a non-default state.
    auto* swapBtn = dock.findChild<QPushButton*>("swapBtn");
    QVERIFY(swapBtn != nullptr);
    swapBtn->click();
    QVERIFY(dock.foregroundColor() != Qt::black);
    QVERIFY(dock.backgroundColor() != Qt::white);

    QSignalSpy fgSpy(&dock, &ColorDock::foregroundColorChanged);
    QSignalSpy bgSpy(&dock, &ColorDock::backgroundColorChanged);
    defBtn->click();
    QCOMPARE(fgSpy.count(), 1);
    QCOMPARE(bgSpy.count(), 1);
    QCOMPARE(dock.foregroundColor(), Qt::black);
    QCOMPARE(dock.backgroundColor(), Qt::white);
}

QTEST_MAIN(tst_ColorDock)
#include "tst_ColorDock.moc"
