// SPDX-License-Identifier: MIT
//
// ImageOptionBar implementation - F-E (2026-09-09)
//
#include "ImageOptionBar.h"
#include "ui_ImageOptionBar.h"
#include "ToolContext.h"
#include "TransformTool.h"
#include "ShapeTool.h"
#include "PenTool.h"
#include "CloneTool.h"
#include "HealTool.h"
#include "PatchTool.h"
#include "RedEyeTool.h"
#include "MaskBrushTool.h"
#include "logger.h"

#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

namespace tools {

ImageOptionBar::ImageOptionBar(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::ImageOptionBar;
    ui->setupUi(this);
    // F-E 阶段: 默认显示 pageNone (0)
    //   F-L 阶段: attach 时用 ToolState::optionPage() 替换对应 page
    ui->stackedWidget->setCurrentIndex(static_cast<int>(mediators::ToolId::None));
}

ImageOptionBar::~ImageOptionBar()
{
    delete ui;
}

void ImageOptionBar::attach(ToolContext* ctx)
{
    m_ctx = ctx;
    if (m_ctx) {
        connect(m_ctx, &ToolContext::toolChanged,
                this, &ImageOptionBar::onToolChanged);
        // 同步初始状态
        onToolChanged(m_ctx->currentToolId());
    }
}

void ImageOptionBar::detach()
{
    if (m_ctx) {
        disconnect(m_ctx, nullptr, this, nullptr);
    }
    m_ctx = nullptr;
}

void ImageOptionBar::onToolChanged(mediators::ToolId id)
{
    const int idx = static_cast<int>(id);
    if (idx < 0 || idx >= ui->stackedWidget->count()) {
        // P0-6.11 (2026-09-14): Transform tool (idx=9) 动态加 page + 4 mode combo
        // Q4.2.1 (2026-09-23): MaskBrush (idx=16) 也走动态 addPage 路径
        //   跟 Transform 一样 — MaskBrushTool::optionPage() 返回 MaskOptionsPanel
        if (id == mediators::ToolId::Transform) {
            // 检查是否已经 addPage
            const int transformIdx = static_cast<int>(mediators::ToolId::Transform);
            if (transformIdx >= ui->stackedWidget->count()) {
                // 找当前 TransformTool (m_ctx->currentState())
                if (m_ctx) {
                    if (auto* tool = dynamic_cast<TransformTool*>(m_ctx->currentState())) {
                        if (QWidget* page = tool->optionPage(this)) {
                            page->setObjectName("pageTransform");
                            ui->stackedWidget->addWidget(page);
                            LOG_INFO("[OptionBar] added Transform page (4 mode combo + Shift toggle)");
                        }
                    }
                }
            }
            ui->stackedWidget->setCurrentIndex(static_cast<int>(mediators::ToolId::Transform));
        } else if (id == mediators::ToolId::MaskBrush) {
            // Q4.2.1: MaskBrush dynamic addPage, ui 已经预留 page16 占位 (跟 ui 一致)
            const int maskIdx = static_cast<int>(mediators::ToolId::MaskBrush);
            if (maskIdx >= ui->stackedWidget->count()) {
                if (m_ctx) {
                    if (auto* tool = dynamic_cast<MaskBrushTool*>(m_ctx->currentState())) {
                        if (QWidget* page = tool->optionPage(this)) {
                            page->setObjectName("pageMaskBrush");
                            ui->stackedWidget->addWidget(page);
                            LOG_INFO("[OptionBar] added MaskBrush page (MaskOptionsPanel)");
                        }
                    }
                }
            }
            ui->stackedWidget->setCurrentIndex(maskIdx);
        } else {
            LOG_WARN("[OptionBar] toolChanged id={} out of range (count={})", idx, ui->stackedWidget->count());
        }
        return;
    }

    // P0-9.1 (2026-09-15): 形状工具 (idx=10) 动态装载 optionPage
    //   P0-9.2/9.3 同模式 (Pen/Clone/Heal/Patch/RedEye)
    //   ui 已经有空 page10~15, 我们 replace 它, 避免 addPage 越界
    if (m_ctx) {
        if (auto* tool = m_ctx->currentState()) {
            if (auto* page = tool->optionPage(this)) {
                page->setObjectName(QString("page%1").arg(idx));
                // replace 现有 idx 位置
                QWidget* old = ui->stackedWidget->widget(idx);
                if (old && old != page) {
                    ui->stackedWidget->removeWidget(old);
                    old->deleteLater();
                }
                ui->stackedWidget->insertWidget(idx, page);
            }
        }
    }

    LOG_DEBUG("[OptionBar] onToolChanged: id={} idx={}", idx, idx);
    ui->stackedWidget->setCurrentIndex(idx);
}

} // namespace tools
