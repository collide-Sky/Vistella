// SPDX-License-Identifier: MIT
//
// LeftToolBar implementation - F-D (2026-09-09) + F-L (2026-09-10)
//
#include "LeftToolBar.h"
#include "ui_LeftToolBar.h"
#include "ToolContext.h"
#include "ToolState.h"
#include "MoveTool.h"
#include "RectSelect.h"
#include "Lasso.h"
#include "MagicWand.h"
#include "Crop.h"
#include "Text.h"
#include "Brush.h"
#include "EyedropperTool.h"
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
}

LeftToolBar::~LeftToolBar()
{
    delete ui;
}

ToolState* LeftToolBar::createMoveTool()       { return new MoveTool(); }
ToolState* LeftToolBar::createRectSelectTool() { return new RectSelect(); }
ToolState* LeftToolBar::createLassoTool()      { return new Lasso(); }
ToolState* LeftToolBar::createMagicWandTool()  { return new MagicWand(); }
ToolState* LeftToolBar::createCropTool()       { return new Crop(); }
ToolState* LeftToolBar::createTextTool()       { return new Text(); }
ToolState* LeftToolBar::createBrushTool()      { return new Brush(); }
ToolState* LeftToolBar::createEyedropperTool() { return new EyedropperTool(); }

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
