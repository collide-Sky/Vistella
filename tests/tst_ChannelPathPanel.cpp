// SPDX-License-Identifier: MIT
//
// tst_ChannelPathPanel - P6 (2026-10-09)
//
// Verifies the new P6 widgets in ChannelPathPanel: Save Selection / Load
// Selection buttons + Quick Mask toggle. Channel / path selectors are
// covered by existing F-M.3 tests.
//

#include <QTest>
#include <QSignalSpy>
#include <QPushButton>

#include "../src/media/docks/ChannelPathPanel.h"

using namespace docks;

class tst_ChannelPathPanel : public QObject
{
    Q_OBJECT
private slots:
    void ctor_hasSaveLoadQuickMaskButtons();
    void saveSel_click_emitsSaveSignal();
    void loadSel_click_emitsLoadSignal();
    void quickMask_toggle_emitsToggledAndSyncsBack();
};

void tst_ChannelPathPanel::ctor_hasSaveLoadQuickMaskButtons()
{
    ChannelPathPanel panel;
    QVERIFY(panel.findChild<QPushButton*>("saveSelBtn") != nullptr);
    QVERIFY(panel.findChild<QPushButton*>("loadSelBtn") != nullptr);
    // quickMaskBtn is declared as QPushButton+checkable in the .ui.
    QVERIFY(panel.findChild<QPushButton*>("quickMaskBtn") != nullptr);
}

void tst_ChannelPathPanel::saveSel_click_emitsSaveSignal()
{
    ChannelPathPanel panel;
    QSignalSpy spy(&panel, &ChannelPathPanel::saveSelectionRequested);
    auto* btn = panel.findChild<QPushButton*>("saveSelBtn");
    QVERIFY(btn != nullptr);
    btn->click();
    QCOMPARE(spy.count(), 1);
}

void tst_ChannelPathPanel::loadSel_click_emitsLoadSignal()
{
    ChannelPathPanel panel;
    QSignalSpy spy(&panel, &ChannelPathPanel::loadSelectionRequested);
    auto* btn = panel.findChild<QPushButton*>("loadSelBtn");
    QVERIFY(btn != nullptr);
    btn->click();
    QCOMPARE(spy.count(), 1);
}

void tst_ChannelPathPanel::quickMask_toggle_emitsToggledAndSyncsBack()
{
    ChannelPathPanel panel;

    QSignalSpy spy(&panel, &ChannelPathPanel::quickMaskToggled);
    auto* btn = panel.findChild<QPushButton*>("quickMaskBtn");
    QVERIFY(btn != nullptr);

    btn->setChecked(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    // External sync from AppBar / ToolMediator (no echo signal).
    panel.setQuickMask(false);
    QCOMPARE(btn->isChecked(), false);
    QCOMPARE(spy.count(), 1);  // unchanged (no echo)

    panel.setQuickMask(true);
    QCOMPARE(btn->isChecked(), true);
    QCOMPARE(spy.count(), 1);  // unchanged
}

QTEST_MAIN(tst_ChannelPathPanel)
#include "tst_ChannelPathPanel.moc"
