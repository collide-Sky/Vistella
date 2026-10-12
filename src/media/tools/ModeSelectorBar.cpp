// SPDX-License-Identifier: MIT
//
// ModeSelectorBar implementation - Q4.2.3 (2026-09-24)
//
#include "ModeSelectorBar.h"

#include "ToolContext.h"
#include "MagicWand.h"
#include "Lasso.h"
#include "Crop.h"
#include "Brush.h"
#include "EyedropperTool.h"
#include "../mediators/ToolMediator.h"
#include "logger.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>

namespace tools {

namespace {
constexpr const char* kLog = "[ModeSelectorBar]";
}

ModeSelectorBar::ModeSelectorBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("modeSelectorBar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setupUi();
    rebuildPages();
    // default: show empty page (no tool selected yet)
    showPageForTool(static_cast<mediators::ToolId>(-1));
}

ModeSelectorBar::~ModeSelectorBar() = default;

void ModeSelectorBar::setupUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    // Q4.2.4 fix: pages 装进 QStackedWidget, 由 setCurrentIndex 切换.
    //   之前 page widgets 直接 new QWidget(this) 不挂 layout, show()/hide() 不生效,
    //   test 里 button 永远 isVisible()=false.
    m_stacked = new QStackedWidget(this);
    root->addWidget(m_stacked);
}

void ModeSelectorBar::rebuildPages()
{
    // ===== Page: Selection mode (MagicWand + Lasso) =====
    m_pageSelection.widget = new QWidget(this);
    auto* selLay = new QHBoxLayout(m_pageSelection.widget);
    selLay->setContentsMargins(0, 0, 0, 0);
    selLay->setSpacing(2);
    m_pageSelection.group = new QButtonGroup(m_pageSelection.widget);
    m_pageSelection.group->setExclusive(true);
    const QStringList selLabels = {
        QStringLiteral("New (Replace)"),
        QStringLiteral("Add (Shift)"),
        QStringLiteral("Subtract (Alt)"),
        QStringLiteral("Intersect"),
    };
    for (int i = 0; i < 4; ++i) {
        auto* btn = new QToolButton(m_pageSelection.widget);
        btn->setText(selLabels[i]);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setMinimumWidth(72);
        btn->setAutoExclusive(true);
        m_pageSelection.group->addButton(btn, i);
        m_pageSelection.buttons << btn;
        selLay->addWidget(btn);
    }
    connect(m_pageSelection.group, &QButtonGroup::idClicked,
            this, &ModeSelectorBar::onSelectionModeBtn);
    m_pageSelection.widget->setObjectName(QStringLiteral("pageSelection"));

    // ===== Page: Crop aspect ratio =====
    m_pageCrop.widget = new QWidget(this);
    auto* cropLay = new QHBoxLayout(m_pageCrop.widget);
    cropLay->setContentsMargins(0, 0, 0, 0);
    cropLay->setSpacing(2);
    m_pageCrop.group = new QButtonGroup(m_pageCrop.widget);
    m_pageCrop.group->setExclusive(true);
    const QStringList cropLabels = {
        QStringLiteral("Free"),
        QStringLiteral("1:1"),
        QStringLiteral("4:3"),
        QStringLiteral("16:9"),
    };
    for (int i = 0; i < 4; ++i) {
        auto* btn = new QToolButton(m_pageCrop.widget);
        btn->setText(cropLabels[i]);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setMinimumWidth(48);
        btn->setAutoExclusive(true);
        m_pageCrop.group->addButton(btn, i);
        m_pageCrop.buttons << btn;
        cropLay->addWidget(btn);
    }
    connect(m_pageCrop.group, &QButtonGroup::idClicked,
            this, &ModeSelectorBar::onAspectBtn);
    m_pageCrop.widget->setObjectName(QStringLiteral("pageCrop"));

    // ===== Page: Brush blend mode =====
    m_pageBrush.widget = new QWidget(this);
    auto* brushLay = new QHBoxLayout(m_pageBrush.widget);
    brushLay->setContentsMargins(0, 0, 0, 0);
    brushLay->setSpacing(2);
    m_pageBrush.group = new QButtonGroup(m_pageBrush.widget);
    m_pageBrush.group->setExclusive(true);
    const QStringList blendLabels = {
        QStringLiteral("Normal"),
        QStringLiteral("Multiply"),
        QStringLiteral("Screen"),
    };
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QToolButton(m_pageBrush.widget);
        btn->setText(blendLabels[i]);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setMinimumWidth(72);
        btn->setAutoExclusive(true);
        m_pageBrush.group->addButton(btn, i);
        m_pageBrush.buttons << btn;
        brushLay->addWidget(btn);
    }
    connect(m_pageBrush.group, &QButtonGroup::idClicked,
            this, &ModeSelectorBar::onBlendBtn);
    m_pageBrush.widget->setObjectName(QStringLiteral("pageBrush"));

    // ===== Page: Eyedropper sample size =====
    m_pageEyedropper.widget = new QWidget(this);
    auto* eyeLay = new QHBoxLayout(m_pageEyedropper.widget);
    eyeLay->setContentsMargins(0, 0, 0, 0);
    eyeLay->setSpacing(2);
    m_pageEyedropper.group = new QButtonGroup(m_pageEyedropper.widget);
    m_pageEyedropper.group->setExclusive(true);
    const QStringList sampleLabels = {
        QStringLiteral("Point"),
        QStringLiteral("3x3"),
        QStringLiteral("5x5"),
        QStringLiteral("11x11"),
    };
    for (int i = 0; i < 4; ++i) {
        auto* btn = new QToolButton(m_pageEyedropper.widget);
        btn->setText(sampleLabels[i]);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setMinimumWidth(56);
        btn->setAutoExclusive(true);
        m_pageEyedropper.group->addButton(btn, i);
        m_pageEyedropper.buttons << btn;
        eyeLay->addWidget(btn);
    }
    connect(m_pageEyedropper.group, &QButtonGroup::idClicked,
            this, &ModeSelectorBar::onSampleBtn);
    m_pageEyedropper.widget->setObjectName(QStringLiteral("pageEyedropper"));

    // ===== Page: Empty (fallback for tools we don't handle) =====
    // P7.1 (2026-10-12): 之前放一个灰色 "(no secondary mode for current tool)"
    //   占位 label; PS 风格下这种 hint 太抢眼, 跟左侧工具栏的紧凑风格冲突.
    //   改为纯空 page — user 切到没二级模式的工具 (Move/Text/Shape etc.) 时,
    //   bar 自身依然显示高度但内部留白, 不放标签也不放占位.
    m_pageEmpty.widget = new QWidget(this);
    auto* emptyLay = new QHBoxLayout(m_pageEmpty.widget);
    emptyLay->setContentsMargins(0, 0, 0, 0);
    emptyLay->addStretch(1);

    // Q4.2.4 fix: 把 5 个 page widget 全加到 QStackedWidget, showPageForTool 用
    //   setCurrentIndex 切换. 顺序: selection(0)/crop(1)/brush(2)/sample(3)/empty(4)
    m_stacked->addWidget(m_pageSelection.widget);  // idx 0
    m_stacked->addWidget(m_pageCrop.widget);        // idx 1
    m_stacked->addWidget(m_pageBrush.widget);       // idx 2
    m_stacked->addWidget(m_pageEyedropper.widget);  // idx 3
    m_stacked->addWidget(m_pageEmpty.widget);       // idx 4 (default)
}

void ModeSelectorBar::attach(ToolContext* ctx)
{
    m_ctx = ctx;
    if (m_ctx) {
        connect(m_ctx, &ToolContext::toolChanged,
                this, &ModeSelectorBar::onToolChanged);
        onToolChanged(m_ctx->currentToolId());
    }
}

void ModeSelectorBar::detach()
{
    if (m_ctx) {
        disconnect(m_ctx, nullptr, this, nullptr);
    }
    m_ctx = nullptr;
}

void ModeSelectorBar::onToolChanged(mediators::ToolId id)
{
    m_currentTool = id;
    showPageForTool(id);
    refreshButtonsFromTool();
}

void ModeSelectorBar::showPageForTool(mediators::ToolId id)
{
    // Q4.2.4 fix: QStackedWidget 用 setCurrentIndex 切 page, 不需要手动 hide().
    //   之前手动 hide/show 失败是因为 page widgets 没挂 layout, show() 无效.
    //   Page 顺序 (rebuildPages 末尾 addWidget): 0=selection, 1=crop, 2=brush, 3=sample, 4=empty.
    int idx = 4;  // default empty
    switch (id) {
    case mediators::ToolId::MagicWand:
    case mediators::ToolId::Lasso:
        idx = 0;
        break;
    case mediators::ToolId::Crop:
        idx = 1;
        break;
    case mediators::ToolId::Brush:
        idx = 2;
        break;
    case mediators::ToolId::Eyedropper:
        idx = 3;
        break;
    default:
        idx = 4;
        break;
    }
    if (m_stacked) m_stacked->setCurrentIndex(idx);
}

void ModeSelectorBar::refreshButtonsFromTool()
{
    if (!m_ctx) return;
    auto* tool = m_ctx->currentState();
    if (!tool) return;

    if (m_currentTool == mediators::ToolId::MagicWand) {
        auto* mw = dynamic_cast<MagicWand*>(tool);
        if (!mw) return;
        QSignalBlocker block(m_pageSelection.group);
        const int mode = static_cast<int>(mw->selectionMode());
        if (auto* btn = m_pageSelection.group->button(mode)) btn->setChecked(true);
    } else if (m_currentTool == mediators::ToolId::Lasso) {
        auto* ls = dynamic_cast<Lasso*>(tool);
        if (!ls) return;
        QSignalBlocker block(m_pageSelection.group);
        const int mode = static_cast<int>(ls->selectionMode());
        if (auto* btn = m_pageSelection.group->button(mode)) btn->setChecked(true);
    } else if (m_currentTool == mediators::ToolId::Crop) {
        auto* cr = dynamic_cast<Crop*>(tool);
        if (!cr) return;
        QSignalBlocker block(m_pageCrop.group);
        const int idx = static_cast<int>(cr->aspectRatio());
        if (auto* btn = m_pageCrop.group->button(idx)) btn->setChecked(true);
    } else if (m_currentTool == mediators::ToolId::Brush) {
        auto* br = dynamic_cast<Brush*>(tool);
        if (!br) return;
        QSignalBlocker block(m_pageBrush.group);
        const int bm = br->blendMode();
        if (auto* btn = m_pageBrush.group->button(bm)) btn->setChecked(true);
    } else if (m_currentTool == mediators::ToolId::Eyedropper) {
        auto* ed = dynamic_cast<EyedropperTool*>(tool);
        if (!ed) return;
        QSignalBlocker block(m_pageEyedropper.group);
        // sample sizes map to button indices: 1->0, 3->1, 5->2, 11->3
        const int sampleSize = ed->sampleSize();
        int idx = 0;
        if      (sampleSize <= 1)  idx = 0;
        else if (sampleSize <= 3)  idx = 1;
        else if (sampleSize <= 5)  idx = 2;
        else                       idx = 3;
        if (auto* btn = m_pageEyedropper.group->button(idx)) btn->setChecked(true);
    }
}

void ModeSelectorBar::onSelectionModeBtn(int idx)
{
    if (!m_ctx) return;
    auto* tool = m_ctx->currentState();
    if (!tool) return;
    const auto mode = static_cast<selection::SelectionModel::Mode>(idx);
    if (auto* mw = dynamic_cast<MagicWand*>(tool)) {
        mw->setSelectionMode(mode);
        LOG_DEBUG("{} MagicWand selectionMode -> {}", kLog, idx);
    } else if (auto* ls = dynamic_cast<Lasso*>(tool)) {
        ls->setSelectionMode(mode);
        LOG_DEBUG("{} Lasso selectionMode -> {}", kLog, idx);
    }
}

void ModeSelectorBar::onAspectBtn(int idx)
{
    if (!m_ctx) return;
    auto* tool = m_ctx->currentState();
    auto* cr = dynamic_cast<Crop*>(tool);
    if (!cr) return;
    cr->setAspectRatio(static_cast<Crop::AspectRatio>(idx));
    LOG_DEBUG("{} Crop aspectRatio -> {}", kLog, idx);
}

void ModeSelectorBar::onBlendBtn(int idx)
{
    if (!m_ctx) return;
    auto* tool = m_ctx->currentState();
    auto* br = dynamic_cast<Brush*>(tool);
    if (!br) return;
    br->setBlendMode(idx);
    LOG_DEBUG("{} Brush blendMode -> {}", kLog, idx);
}

void ModeSelectorBar::onSampleBtn(int idx)
{
    if (!m_ctx) return;
    auto* tool = m_ctx->currentState();
    auto* ed = dynamic_cast<EyedropperTool*>(tool);
    if (!ed) return;
    // idx 0..3 -> 1, 3, 5, 11
    static const int sizes[] = { 1, 3, 5, 11 };
    ed->setSampleSize(sizes[idx]);
    LOG_DEBUG("{} Eyedropper sampleSize -> {}", kLog, sizes[idx]);
}

} // namespace tools
