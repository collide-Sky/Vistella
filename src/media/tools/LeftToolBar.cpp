// SPDX-License-Identifier: MIT
//
// LeftToolBar implementation - F-D (2026-09-09) + F-L (2026-09-10)
//
#include "LeftToolBar.h"
#include "ui_LeftToolBar.h"
#include "ToolContext.h"
#include "ToolState.h"
#include "ToolStateFactory.h"
#include "logger.h"

#include <QToolButton>
#include <QGridLayout>
#include <QIcon>

namespace tools {

LeftToolBar::LeftToolBar(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::LeftToolBar;
    ui->setupUi(this);
    // F-L (2026-09-10): 8 工具 button 映射 (PS 风格 4 行 2 列)
    m_buttons[mediators::ToolId::Move]       = ui->btnMove;
    m_buttons[mediators::ToolId::RectSelect] = ui->btnRectSelect;
    m_buttons[mediators::ToolId::Lasso]      = ui->btnLasso;
    m_buttons[mediators::ToolId::MagicWand]  = ui->btnMagicWand;
    m_buttons[mediators::ToolId::Crop]       = ui->btnCrop;
    m_buttons[mediators::ToolId::Text]       = ui->btnText;
    m_buttons[mediators::ToolId::Brush]      = ui->btnBrush;
    m_buttons[mediators::ToolId::Eyedropper] = ui->btnEyedropper;
    // P0-9.4 (2026-09-15): 形状/矢量 + 修复工具 6 按钮
    m_buttons[mediators::ToolId::Shape]      = ui->btnShape;
    m_buttons[mediators::ToolId::Pen]        = ui->btnPen;
    m_buttons[mediators::ToolId::Clone]      = ui->btnClone;
    m_buttons[mediators::ToolId::Heal]       = ui->btnHeal;
    m_buttons[mediators::ToolId::Patch]      = ui->btnPatch;
    m_buttons[mediators::ToolId::RedEye]     = ui->btnRedEye;
    // P1.3.4 (2026-09-17): MaskBrush 按钮暂未加到 ui,
    //   但 mediator.switchTool(ToolId::MaskBrush) 仍走 case 分支触发.
}

LeftToolBar::~LeftToolBar()
{
    delete ui;
}

ToolState* LeftToolBar::createMoveTool()       { return createToolState(mediators::ToolId::Move); }
ToolState* LeftToolBar::createRectSelectTool() { return createToolState(mediators::ToolId::RectSelect); }
ToolState* LeftToolBar::createLassoTool()      { return createToolState(mediators::ToolId::Lasso); }
ToolState* LeftToolBar::createMagicWandTool()  { return createToolState(mediators::ToolId::MagicWand); }
ToolState* LeftToolBar::createCropTool()       { return createToolState(mediators::ToolId::Crop); }
ToolState* LeftToolBar::createTextTool()       { return createToolState(mediators::ToolId::Text); }
ToolState* LeftToolBar::createBrushTool()      { return createToolState(mediators::ToolId::Brush); }
ToolState* LeftToolBar::createEyedropperTool() { return createToolState(mediators::ToolId::Eyedropper); }
ToolState* LeftToolBar::createShapeTool()      { return createToolState(mediators::ToolId::Shape); }
ToolState* LeftToolBar::createPenTool()        { return createToolState(mediators::ToolId::Pen); }
ToolState* LeftToolBar::createCloneTool()      { return createToolState(mediators::ToolId::Clone); }
ToolState* LeftToolBar::createHealTool()       { return createToolState(mediators::ToolId::Heal); }
ToolState* LeftToolBar::createPatchTool()      { return createToolState(mediators::ToolId::Patch); }
ToolState* LeftToolBar::createRedEyeTool()     { return createToolState(mediators::ToolId::RedEye); }
ToolState* LeftToolBar::createMaskBrushTool()  { return createToolState(mediators::ToolId::MaskBrush); }

void LeftToolBar::attach(mediators::ToolMediator* toolMed, ToolContext* ctx)
{
    m_toolMed = toolMed;
    m_ctx = ctx;

    // 1. 监听 Mediator 切工具 → 同步 button 高亮 + ctx setState
    if (m_toolMed) {
        connect(m_toolMed, &mediators::ToolMediator::toolSwitched,
                this, &LeftToolBar::onToolMediatorSwitched);
    }

    // 2. button click → Mediator.switchTool (Mediator 转发给 ctx setState)
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        const mediators::ToolId id = it.key();
        QToolButton* btn = it.value();
        connect(btn, &QToolButton::clicked, this, [this, id, btn](bool checked) {
            // checked 来自 click 触发 (按钮 toggle 状态)
            if (checked && m_toolMed) {
                m_toolMed->switchTool(id);
            } else if (!checked) {
                // 再点一次 → 切回 None 状态
                if (m_toolMed) m_toolMed->switchTool(mediators::ToolId::None);
            }
            (void)btn;
        });
    }
}

void LeftToolBar::detach()
{
    if (m_toolMed) {
        disconnect(m_toolMed, nullptr, this, nullptr);
    }
    m_toolMed = nullptr;
    m_ctx = nullptr;
}

void LeftToolBar::onToolMediatorSwitched(mediators::ToolId id)
{
    // 同步 button 高亮
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        it.value()->setChecked(it.key() == id);
    }
    // 调 ctx setState (LeftToolBar 是 Mediator 的下游, 这里完成 State 切换)
    if (m_ctx) {
        LOG_INFO("[LeftBar] onToolMediatorSwitched: id={}", static_cast<int>(id));
        switch (id) {
        case mediators::ToolId::Move:
            m_ctx->setState(std::unique_ptr<ToolState>(createMoveTool()));
            break;
        case mediators::ToolId::RectSelect:
            m_ctx->setState(std::unique_ptr<ToolState>(createRectSelectTool()));
            break;
        case mediators::ToolId::Lasso:
            m_ctx->setState(std::unique_ptr<ToolState>(createLassoTool()));
            break;
        case mediators::ToolId::MagicWand:
            m_ctx->setState(std::unique_ptr<ToolState>(createMagicWandTool()));
            break;
        case mediators::ToolId::Crop:
            m_ctx->setState(std::unique_ptr<ToolState>(createCropTool()));
            break;
        case mediators::ToolId::Text:
            m_ctx->setState(std::unique_ptr<ToolState>(createTextTool()));
            break;
        case mediators::ToolId::Brush:
            m_ctx->setState(std::unique_ptr<ToolState>(createBrushTool()));
            break;
        case mediators::ToolId::Eyedropper:
            m_ctx->setState(std::unique_ptr<ToolState>(createEyedropperTool()));
            break;
        // P0-9.4 (2026-09-15): 形状/矢量 + 修复工具 6 个 case
        case mediators::ToolId::Shape:
            m_ctx->setState(std::unique_ptr<ToolState>(createShapeTool()));
            break;
        case mediators::ToolId::Pen:
            m_ctx->setState(std::unique_ptr<ToolState>(createPenTool()));
            break;
        case mediators::ToolId::Clone:
            m_ctx->setState(std::unique_ptr<ToolState>(createCloneTool()));
            break;
        case mediators::ToolId::Heal:
            m_ctx->setState(std::unique_ptr<ToolState>(createHealTool()));
            break;
        case mediators::ToolId::Patch:
            m_ctx->setState(std::unique_ptr<ToolState>(createPatchTool()));
            break;
        case mediators::ToolId::RedEye:
            m_ctx->setState(std::unique_ptr<ToolState>(createRedEyeTool()));
            break;
        // P1.3.4 (2026-09-17): MaskBrush (像素蒙版画笔)
        case mediators::ToolId::MaskBrush:
            m_ctx->setState(std::unique_ptr<ToolState>(createMaskBrushTool()));
            break;
        default:
            m_ctx->setState(nullptr);  // None / unknown -> 释放当前 tool
            break;
        }
    }
}

void LeftToolBar::setToolEnabled(mediators::ToolId id, bool enabled)
{
    auto it = m_buttons.find(id);
    if (it != m_buttons.end()) {
        it.value()->setEnabled(enabled);
    }
}

} // namespace tools
