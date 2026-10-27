// SPDX-License-Identifier: MIT
//
// LeftToolBar implementation
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
#include <QStyle>
#include <QPalette>
#include <QColorDialog>

namespace tools {

LeftToolBar::LeftToolBar(QWidget* parent) : QWidget(parent)
{
    ui = new Ui::LeftToolBar;
    ui->setupUi(this);
    // 用 QStyle standardIcon 替换字母 placeholder
    //   没 SVG 资源的情况下, Qt 内置 standard pixmap (cross / arrow / file) 视觉比
    //   "M/R/L/W/C/T/B/I" 强得多. 后续 P5 接 SVG 图标, 直接 setIcon(SVG) 即可.
    // TODO(P8.9): six tools still use the QStyle fallback icon instead of the SVG set -- see docs/DEBT.md#A-TOOLICON
    //   Lasso/MagicWand/Crop/Text/Brush/Eyedropper/Shape). 资源通过 vistella_tool_icons
    //   qt_add_resources 加载, PREFIX /icons/tools, setIcon(QIcon(":/icons/tools/tool-xxx.svg")).
    //   剩下 6 个工具 (Pen/Clone/Heal/Patch/RedEye/MaskBrush) 暂时用 QStyle 图标
    auto svgIcon = [](const QString& name) {
        return QIcon(QStringLiteral(":/icons/tools/") + name);
    };
    ui->btnMove->setIcon(svgIcon("tool-move.svg"));
    ui->btnRectSelect->setIcon(svgIcon("tool-rectselect.svg"));
    ui->btnLasso->setIcon(svgIcon("tool-lasso.svg"));
    ui->btnMagicWand->setIcon(svgIcon("tool-magicwand.svg"));
    ui->btnCrop->setIcon(svgIcon("tool-crop.svg"));
    ui->btnText->setIcon(svgIcon("tool-text.svg"));
    ui->btnBrush->setIcon(svgIcon("tool-brush.svg"));
    ui->btnEyedropper->setIcon(svgIcon("tool-eyedropper.svg"));
    ui->btnShape->setIcon(svgIcon("tool-shape.svg"));
    // 6 个二线工具 SVG 补齐 (Pen/Clone/Heal/Patch/RedEye/MaskBrush)
    //   跟 P5 9 个工具同款 16x16 + 蓝 #4A90E2 + 黑 #202020.
    ui->btnPen->setIcon(svgIcon("tool-pen.svg"));
    ui->btnClone->setIcon(svgIcon("tool-clone.svg"));
    ui->btnHeal->setIcon(svgIcon("tool-heal.svg"));
    ui->btnPatch->setIcon(svgIcon("tool-patch.svg"));
    ui->btnRedEye->setIcon(svgIcon("tool-redeye.svg"));
    ui->btnMaskBrush->setIcon(svgIcon("tool-maskbrush.svg"));
    for (auto* b : findChildren<QToolButton*>()) {
        b->setIconSize(QSize(16, 16));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }

    // 8 工具 button 映射 (PS 风格 4 行 2 列)
    m_buttons[mediators::ToolId::Move]       = ui->btnMove;
    m_buttons[mediators::ToolId::RectSelect] = ui->btnRectSelect;
    m_buttons[mediators::ToolId::Lasso]      = ui->btnLasso;
    m_buttons[mediators::ToolId::MagicWand]  = ui->btnMagicWand;
    m_buttons[mediators::ToolId::Crop]       = ui->btnCrop;
    m_buttons[mediators::ToolId::Text]       = ui->btnText;
    m_buttons[mediators::ToolId::Brush]      = ui->btnBrush;
    m_buttons[mediators::ToolId::Eyedropper] = ui->btnEyedropper;
    // 形状/矢量 + 修复工具 6 按钮
    m_buttons[mediators::ToolId::Shape]      = ui->btnShape;
    m_buttons[mediators::ToolId::Pen]        = ui->btnPen;
    m_buttons[mediators::ToolId::Clone]      = ui->btnClone;
    m_buttons[mediators::ToolId::Heal]       = ui->btnHeal;
    m_buttons[mediators::ToolId::Patch]      = ui->btnPatch;
    m_buttons[mediators::ToolId::RedEye]     = ui->btnRedEye;
    // MaskBrush 按钮加到 ui (之前只 switchTool
    //   路由没按钮 — Q4.2 拍板"不放注释",所以 MaskBrush 也必须有可见入口)
    m_buttons[mediators::ToolId::MaskBrush]  = ui->btnMaskBrush;

    // PS-style fg/bg swatch (50x50 fg + 10x50 bg)
    //   Initial paint + click handlers; double-click = swap. The swatches
    //   stay in lockstep with ColorDock via foregroundColorChanged /
    //   backgroundColorChanged signals.
    paintColorSwatch(ui->fgSwatch, m_fgColor);
    paintColorSwatch(ui->bgSwatch, m_bgColor);
    if (ui->fgSwatch) {
        connect(ui->fgSwatch, &QToolButton::clicked,
                this, &LeftToolBar::onFgSwatchClicked);
    }
    if (ui->bgSwatch) {
        connect(ui->bgSwatch, &QToolButton::clicked,
                this, &LeftToolBar::onBgSwatchClicked);
    }
    if (ui->swapBtn) {
        connect(ui->swapBtn, &QToolButton::clicked,
                this, &LeftToolBar::onSwapBtnClicked);
    }
    // P7.1: PS-style double-click swap on fg/bg swatch (event filter).
    if (ui->fgSwatch) ui->fgSwatch->installEventFilter(this);
    if (ui->bgSwatch) ui->bgSwatch->installEventFilter(this);
}

LeftToolBar::~LeftToolBar()
{
    delete ui;
}

// fg/bg setter / swatch paint / click handlers

void LeftToolBar::setForegroundColor(const QColor& c)
{
    if (m_fgColor == c) return;
    m_fgColor = c;
    if (ui && ui->fgSwatch) paintColorSwatch(ui->fgSwatch, c);
    LOG_INFO("[LeftToolBar] foregroundColor: {}", c.name().toStdString());
    emit foregroundColorChanged(c);
}

void LeftToolBar::setBackgroundColor(const QColor& c)
{
    if (m_bgColor == c) return;
    m_bgColor = c;
    if (ui && ui->bgSwatch) paintColorSwatch(ui->bgSwatch, c);
    LOG_INFO("[LeftToolBar] backgroundColor: {}", c.name().toStdString());
    emit backgroundColorChanged(c);
}

void LeftToolBar::swapColors()
{
    QColor tmp = m_fgColor;
    setForegroundColor(m_bgColor);  // emits
    setBackgroundColor(tmp);        // emits
}

void LeftToolBar::defaultColors()
{
    setForegroundColor(Qt::black);  // emits
    setBackgroundColor(Qt::white);  // emits
}

void LeftToolBar::onFgSwatchClicked()
{
    QColor c = QColorDialog::getColor(m_fgColor, this, "前景色");
    if (c.isValid() && c != m_fgColor) {
        setForegroundColor(c);
    }
}

void LeftToolBar::onBgSwatchClicked()
{
    QColor c = QColorDialog::getColor(m_bgColor, this, "背景色");
    if (c.isValid() && c != m_bgColor) {
        setBackgroundColor(c);
    }
}

void LeftToolBar::onSwapBtnClicked()
{
    swapColors();
}

void LeftToolBar::onFgSwatchDoubleClicked()
{
    // PS-style: 双击 fg = swap.
    swapColors();
}

void LeftToolBar::onBgSwatchDoubleClicked()
{
    // PS-style: 双击 bg = swap.
    swapColors();
}

void LeftToolBar::paintColorSwatch(QWidget* w, const QColor& c)
{
    if (!w) return;
    QPalette p = w->palette();
    p.setColor(QPalette::Button, c);
    p.setColor(QPalette::Window, c);
    w->setPalette(p);
    w->setAutoFillBackground(true);
}

// PS-style fg/bg swatch double-click = swap
//   Install event filter on fgSwatch + bgSwatch so we can intercept
//   QEvent::MouseButtonDblClick and trigger swap (instead of opening
//   picker like single-click does).
bool LeftToolBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == ui->fgSwatch || watched == ui->bgSwatch) {
            swapColors();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
        // 形状/矢量 + 修复工具 6 个 case
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
        // MaskBrush (像素蒙版画笔)
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
