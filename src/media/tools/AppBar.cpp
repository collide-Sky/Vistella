// SPDX-License-Identifier: MIT
//
// AppBar implementation - P5.5+ (2026-10-09)
//
#include "AppBar.h"
#include "ui_AppBar.h"

#include "logger.h"

#include <QAction>
#include <QButtonGroup>
#include <QMenu>

namespace tools {

AppBar::AppBar(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::AppBar;
    ui->setupUi(this);

    // Quick Mask toggle.
    connect(ui->btnQuickMask, &QToolButton::toggled,
            this, &AppBar::onQuickMaskClicked);

    // Screen mode segmented selector via QButtonGroup.
    auto* screenGroup = new QButtonGroup(this);
    screenGroup->setExclusive(true);
    screenGroup->addButton(ui->btnScreenStd,     ScreenStandard);
    screenGroup->addButton(ui->btnScreenFull,    ScreenFullScreen);
    screenGroup->addButton(ui->btnScreenFullMenu, ScreenFullScreenMenu);
    ui->btnScreenStd->setChecked(true);
    connect(screenGroup, &QButtonGroup::idClicked,
            this, &AppBar::onScreenModeBtnClicked);

    // Capture dropdown.
    setupCaptureMenu();
}

AppBar::~AppBar()
{
    delete ui;
}

void AppBar::onQuickMaskClicked(bool checked)
{
    m_quickMask = checked;
    LOG_INFO("[AppBar] quickMask toggled: {}", checked);
    emit quickMaskToggled(checked);
}

void AppBar::onScreenModeBtnClicked(int id)
{
    LOG_INFO("[AppBar] screen mode: {}", id);
    emit screenModeSelected(id);
}

void AppBar::onCaptureMenuTriggered(QAction* a)
{
    if (!a) return;
    const int kind = a->data().toInt();
    LOG_INFO("[AppBar] capture requested: {}", kind);
    emit captureRequested(kind);
}

void AppBar::setupCaptureMenu()
{
    auto* menu = new QMenu(this);
    auto* aSel = menu->addAction("选定区域");
    aSel->setData(CaptureSelected);
    auto* aWin = menu->addAction("本窗口");
    aWin->setData(CaptureWindow);
    auto* aFull = menu->addAction("全屏");
    aFull->setData(CaptureFullScreen);
    ui->btnCapture->setMenu(menu);
    connect(menu, &QMenu::triggered,
            this, &AppBar::onCaptureMenuTriggered);
}

} // namespace tools
