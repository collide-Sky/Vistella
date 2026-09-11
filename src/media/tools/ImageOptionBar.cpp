// SPDX-License-Identifier: MIT
//
// ImageOptionBar implementation - F-E (2026-09-09)
//
#include "ImageOptionBar.h"
#include "ui_ImageOptionBar.h"
#include "ToolContext.h"
#include "logger.h"

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
        LOG_WARN("[OptionBar] toolChanged id={} out of range (count={})", idx, ui->stackedWidget->count());
        return;
    }
    LOG_DEBUG("[OptionBar] onToolChanged: id={} idx={}", idx, idx);
    ui->stackedWidget->setCurrentIndex(idx);
}

} // namespace tools
