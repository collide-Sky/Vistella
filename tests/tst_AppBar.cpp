// SPDX-License-Identifier: MIT
//
// tst_AppBar - P5.5+ (2026-10-09)
//
// Verifies the AppBar widget emits the three documented signals correctly:
//   1) quickMaskToggled(bool) when btnQuickMask is toggled
//   2) screenModeSelected(int) when any of the 3 segmented buttons is clicked
//   3) captureRequested(int) when a menu action is triggered
//
// AppBar is a QWidget (no ImageWindow dependency), so we drive it standalone.
//

#include <QTest>
#include <QSignalSpy>
#include <QToolButton>
#include <QMenu>
#include <QAction>

#include "../src/media/tools/AppBar.h"

using namespace tools;

class tst_AppBar : public QObject
{
    Q_OBJECT
private slots:
    void ctor_hasQuickMaskScreenModeCapture();
    void quickMaskToggle_emitsSignal();
    void screenMode_clickEach_emitsCorrectId();
    void capture_menuTrigger_emitsCorrectId();
};

void tst_AppBar::ctor_hasQuickMaskScreenModeCapture()
{
    AppBar bar;
    QVERIFY(bar.findChild<QToolButton*>("btnQuickMask") != nullptr);
    QVERIFY(bar.findChild<QToolButton*>("btnScreenStd") != nullptr);
    QVERIFY(bar.findChild<QToolButton*>("btnScreenFull") != nullptr);
    QVERIFY(bar.findChild<QToolButton*>("btnScreenFullMenu") != nullptr);
    QVERIFY(bar.findChild<QToolButton*>("btnCapture") != nullptr);
}

void tst_AppBar::quickMaskToggle_emitsSignal()
{
    AppBar bar;
    auto* btn = bar.findChild<QToolButton*>("btnQuickMask");
    QVERIFY(btn != nullptr);

    QSignalSpy spy(&bar, &AppBar::quickMaskToggled);
    btn->setChecked(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    btn->setChecked(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void tst_AppBar::screenMode_clickEach_emitsCorrectId()
{
    AppBar bar;
    auto* stdBtn = bar.findChild<QToolButton*>("btnScreenStd");
    auto* fullBtn = bar.findChild<QToolButton*>("btnScreenFull");
    auto* menuBtn = bar.findChild<QToolButton*>("btnScreenFullMenu");
    QVERIFY(stdBtn != nullptr);
    QVERIFY(fullBtn != nullptr);
    QVERIFY(menuBtn != nullptr);

    QSignalSpy spy(&bar, &AppBar::screenModeSelected);

    // QButtonGroup::idClicked only fires on user click (not setChecked()),
    //   so drive each button through click().
    stdBtn->click();   // toggles Std off
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), int(AppBar::ScreenStandard));

    fullBtn->click();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), int(AppBar::ScreenFullScreen));

    menuBtn->click();
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toInt(), int(AppBar::ScreenFullScreenMenu));
}

void tst_AppBar::capture_menuTrigger_emitsCorrectId()
{
    AppBar bar;
    auto* btn = bar.findChild<QToolButton*>("btnCapture");
    QVERIFY(btn != nullptr);
    auto* menu = btn->menu();
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().size(), 3);

    QSignalSpy spy(&bar, &AppBar::captureRequested);

    // Programmatically trigger the 3 menu actions.
    menu->actions().at(0)->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), int(AppBar::CaptureSelected));

    menu->actions().at(1)->trigger();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), int(AppBar::CaptureWindow));

    menu->actions().at(2)->trigger();
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toInt(), int(AppBar::CaptureFullScreen));
}

QTEST_MAIN(tst_AppBar)
#include "tst_AppBar.moc"
