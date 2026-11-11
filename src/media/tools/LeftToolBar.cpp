// SPDX-License-Identifier: MIT
//
// LeftToolBar implementation
//
#include "LeftToolBar.h"
#include "ui_LeftToolBar.h"
#include "ToolContext.h"
#include "ToolGroupButton.h"
#include "ToolState.h"
#include "ToolStateFactory.h"
#include "logger.h"

#include <QToolButton>
#include <QGridLayout>
#include <QIcon>
#include <QStyle>
#include <QPalette>
#include <QColorDialog>
#include <QVBoxLayout>

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
    // M2-4: the tools that were missing get their icons from the same
    //   SVG set (16x16, blue #4A90E2 + black #202020).
    ui->btnEraser->setIcon(svgIcon("tool-eraser.svg"));
    ui->btnGradient->setIcon(svgIcon("tool-gradient.svg"));
    ui->btnPaintBucket->setIcon(svgIcon("tool-paintbucket.svg"));
    ui->btnDodgeBurn->setIcon(svgIcon("tool-dodgeburn.svg"));
    ui->btnBlurSharpenSmudge->setIcon(svgIcon("tool-blur.svg"));
    ui->btnHand->setIcon(svgIcon("tool-hand.svg"));
    ui->btnZoom->setIcon(svgIcon("tool-zoom.svg"));
    ui->btnRotate->setIcon(svgIcon("tool-rotate.svg"));
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
    // M2-4: the tools that were missing.
    m_buttons[mediators::ToolId::Eraser]     = ui->btnEraser;
    m_buttons[mediators::ToolId::Gradient]   = ui->btnGradient;
    m_buttons[mediators::ToolId::PaintBucket]= ui->btnPaintBucket;
    m_buttons[mediators::ToolId::DodgeBurn]  = ui->btnDodgeBurn;
    m_buttons[mediators::ToolId::BlurSharpenSmudge] = ui->btnBlurSharpenSmudge;
    m_buttons[mediators::ToolId::Hand]       = ui->btnHand;
    m_buttons[mediators::ToolId::Zoom]       = ui->btnZoom;
    m_buttons[mediators::ToolId::Rotate]     = ui->btnRotate;

    // PS-style fg/bg swatch (50x50 fg + 10x50 bg)
    //   Initial paint + click handlers; double-click = swap. The swatches
    //   stay in lockstep with ColorDock via foregroundColorChanged /
    //   backgroundColorChanged signals.
    // M1-8: the initial colours come from the state when one is bound;
    //   without a state they are the PS defaults (black / white).
    paintColorSwatch(ui->fgSwatch, foregroundColor());
    paintColorSwatch(ui->bgSwatch, backgroundColor());
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

    // button click -> Mediator.switchTool.
    //   连接放在构造期: 按钮本身跟文档无关, 每次 attach 再连一遍会叠加,
    //   一次点击就会触发 N 次切工具.
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        const mediators::ToolId id = it.key();
        connect(it.value(), &QToolButton::clicked, this, [this, id](bool checked) {
            // checked 来自 click 触发 (按钮 toggle 状态)
            if (checked && m_toolMed) {
                m_toolMed->switchTool(id);
            } else if (!checked) {
                // 再点一次 -> 切回 None 状态
                if (m_toolMed) m_toolMed->switchTool(mediators::ToolId::None);
            }
        });
    }

    // M2-4: the tool groups (the long-press popup).
    buildToolGroups();
}

LeftToolBar::~LeftToolBar()
{
    delete ui;
}

// ===== M2-4: the tool groups =====

QVector<QPair<QList<int>, QStringList>> LeftToolBar::toolGroupDefinitions()
{
    using T = mediators::ToolId;

    // The Photoshop groups. The order of the tools inside a group is
    // the order the popup shows them in; the first one is the default.
    return {
        // The marquee group: the three selection tools.
        { {int(T::RectSelect), int(T::Lasso), int(T::MagicWand)},
          {"tool-rectselect.svg", "tool-lasso.svg", "tool-magicwand.svg"} },

        // The retouch group: the four healing tools.
        { {int(T::Clone), int(T::Heal), int(T::Patch), int(T::RedEye)},
          {"tool-clone.svg", "tool-heal.svg", "tool-patch.svg", "tool-redeye.svg"} },

        // The brush group: the brush, the eraser and the mask brush.
        { {int(T::Brush), int(T::Eraser), int(T::MaskBrush)},
          {"tool-brush.svg", "tool-eraser.svg", "tool-maskbrush.svg"} },

        // The shape group: the shape tool and the pen.
        { {int(T::Shape), int(T::Pen)},
          {"tool-shape.svg", "tool-pen.svg"} },

        // The fill group: the gradient and the paint bucket.
        { {int(T::Gradient), int(T::PaintBucket)},
          {"tool-gradient.svg", "tool-paintbucket.svg"} },

        // The tone group: the dodge / burn and the blur group.
        { {int(T::DodgeBurn), int(T::BlurSharpenSmudge)},
          {"tool-dodgeburn.svg", "tool-blur.svg"} },

        // The view group: the hand, the zoom and the rotate view.
        { {int(T::Hand), int(T::Zoom), int(T::Rotate)},
          {"tool-hand.svg", "tool-zoom.svg", "tool-rotate.svg"} },
    };
}

void LeftToolBar::buildToolGroups()
{
    // The group buttons go into a second column next to the flat
    // buttons. The flat buttons that are now part of a group are
    // hidden -- the group button takes their place.
    auto* container = ui->toolGridContainer;
    auto* grid = container->findChild<QGridLayout*>("gridLayout");
    if (!grid) return;

    const auto groups = toolGroupDefinitions();

    // Row 0 of the grid is already taken by the flat buttons. The
    // group buttons go into the rows below, starting at the first free
    // row.
    int row = 0;
    for (const auto& b : findChildren<QToolButton*>()) {
        if (b->objectName().startsWith("btn")) {
            // The grid position of the flat buttons is not exposed, so
            // the group buttons simply go into their own column.
        }
    }

    // The group buttons live in their own single-column layout below
    // the flat grid, so the flat grid keeps its 2-column PS look and
    // the groups are clearly a separate section.
    auto* groupLayout = new QVBoxLayout;
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(0);

    auto* groupContainer = new QWidget(container);
    groupContainer->setLayout(groupLayout);

    for (const auto& def : groups) {
        const QList<int>& ids = def.first;
        const QStringList& icons = def.second;
        if (ids.isEmpty()) continue;

        auto* button = new ToolGroupButton(groupContainer);
        button->setGroup(ids, icons);
        button->setFixedSize(28, 28);
        button->setCheckable(true);
        groupLayout->addWidget(button);
        m_groupButtons.append(button);

        // Remember which group a tool belongs to.
        for (const int id : ids) {
            m_groupOfTool.insert(id, button);
        }

        connect(button, &ToolGroupButton::toolActivated, this,
                [this](int toolId) {
                    if (m_toolMed) {
                        m_toolMed->switchTool(static_cast<mediators::ToolId>(toolId));
                    }
                });
    }
    groupLayout->addStretch(1);

    // Put the group container below the flat grid inside the vertical
    // layout of the container.
    auto* outer = container->layout();
    if (outer) {
        outer->addWidget(groupContainer);
    }
    Q_UNUSED(row);
}
// fg/bg setter / swatch paint / click handlers

// M1-8: the toolbar renders the state and pushes changes into it.
QColor LeftToolBar::foregroundColor() const
{
    return m_colorState ? m_colorState->foregroundColor() : QColor(Qt::black);
}

QColor LeftToolBar::backgroundColor() const
{
    return m_colorState ? m_colorState->backgroundColor() : QColor(Qt::white);
}

void LeftToolBar::setColorState(vistella::ColorState* state)
{
    if (m_colorState == state) return;

    // Drop the old connections, otherwise the old document's state keeps
    // writing into this toolbar.
    if (m_colorState) {
        m_colorState->disconnect(this);
    }

    m_colorState = state;

    if (m_colorState) {
        connect(m_colorState, &vistella::ColorState::foregroundColorChanged,
                this, &LeftToolBar::onForegroundColorChanged);
        connect(m_colorState, &vistella::ColorState::backgroundColorChanged,
                this, &LeftToolBar::onBackgroundColorChanged);
        // Show the current value right away.
        onForegroundColorChanged(m_colorState->foregroundColor());
        onBackgroundColorChanged(m_colorState->backgroundColor());
    }
}

// The state the toolbar writes into. Always non-null: when no document
// is bound the toolbar uses its own state.
vistella::ColorState* LeftToolBar::effectiveColorState()
{
    if (!m_colorState) {
        if (!m_ownedColorState) {
            m_ownedColorState = new vistella::ColorState(this);
        }
        m_colorState = m_ownedColorState;
        connect(m_colorState, &vistella::ColorState::foregroundColorChanged,
                this, &LeftToolBar::onForegroundColorChanged);
        connect(m_colorState, &vistella::ColorState::backgroundColorChanged,
                this, &LeftToolBar::onBackgroundColorChanged);
    }
    return m_colorState;
}

void LeftToolBar::setForegroundColor(const QColor& c)
{
    // Push into the state; the state decides whether it is a change
    // (so the same colour does not emit -- that is the echo guard).
    effectiveColorState()->setForegroundColor(c);
}

void LeftToolBar::setBackgroundColor(const QColor& c)
{
    effectiveColorState()->setBackgroundColor(c);
}

// Update the view WITHOUT pushing back into the state (echo guard).
//
// The toolbar re-emits the change: the state is the single source of
// truth, but the toolbar's own signals are part of its public API and
// are what a consumer (a dock, a test) watches. The echo guard is
// that this slot never writes back into the state -- it only renders
// and announces.
void LeftToolBar::onForegroundColorChanged(const QColor& c)
{
    if (ui && ui->fgSwatch) paintColorSwatch(ui->fgSwatch, c);
    emit foregroundColorChanged(c);
}

void LeftToolBar::onBackgroundColorChanged(const QColor& c)
{
    if (ui && ui->bgSwatch) paintColorSwatch(ui->bgSwatch, c);
    emit backgroundColorChanged(c);
}

void LeftToolBar::swapColors()
{
    effectiveColorState()->swapColors();
}

void LeftToolBar::defaultColors()
{
    effectiveColorState()->defaultColors();
}

void LeftToolBar::onFgSwatchClicked()
{
    QColor c = QColorDialog::getColor(foregroundColor(), this, "前景色");
    if (c.isValid() && c != foregroundColor()) {
        setForegroundColor(c);
    }
}

void LeftToolBar::onBgSwatchClicked()
{
    QColor c = QColorDialog::getColor(backgroundColor(), this, "背景色");
    if (c.isValid() && c != backgroundColor()) {
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
// M2-4: the tools that were missing.
ToolState* LeftToolBar::createEraserTool()     { return createToolState(mediators::ToolId::Eraser); }
ToolState* LeftToolBar::createGradientTool()   { return createToolState(mediators::ToolId::Gradient); }
ToolState* LeftToolBar::createPaintBucketTool(){ return createToolState(mediators::ToolId::PaintBucket); }
ToolState* LeftToolBar::createDodgeBurnTool()  { return createToolState(mediators::ToolId::DodgeBurn); }
ToolState* LeftToolBar::createBlurSharpenSmudgeTool() { return createToolState(mediators::ToolId::BlurSharpenSmudge); }
ToolState* LeftToolBar::createHandTool()       { return createToolState(mediators::ToolId::Hand); }
ToolState* LeftToolBar::createZoomTool()       { return createToolState(mediators::ToolId::Zoom); }
ToolState* LeftToolBar::createRotateViewTool() { return createToolState(mediators::ToolId::Rotate); }

void LeftToolBar::attach(mediators::ToolMediator* toolMed, ToolContext* ctx)
{
    // 先断开上一个文档: 不 detach 的话旧连接会留着, 一次点击触发 N 次切工具,
    // 而且旧文档销毁后连接里的裸指针就悬垂了.
    detach();

    m_toolMed = toolMed;
    m_ctx = ctx;

    // 监听 Mediator 切工具 -> 同步 button 高亮 + ctx setState
    if (m_toolMed) {
        connect(m_toolMed.data(), &mediators::ToolMediator::toolSwitched,
                this, &LeftToolBar::onToolMediatorSwitched);
    }
}

void LeftToolBar::detach()
{
    if (m_toolMed) {
        disconnect(m_toolMed.data(), nullptr, this, nullptr);
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

    // M2-4: the group buttons follow the active tool.
    //
    //   The group button shows the icon of the tool that is active
    //   inside the group, and it is checked while one of its tools is
    //   the active one.
    for (auto* group : m_groupButtons) {
        const bool inGroup = m_groupOfTool.contains(static_cast<int>(id));
        group->setChecked(inGroup);
        if (inGroup) {
            group->setCurrentToolId(static_cast<int>(id));
        }
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
        // M2-4: the tools that were missing.
        case mediators::ToolId::Eraser:
            m_ctx->setState(std::unique_ptr<ToolState>(createEraserTool()));
            break;
        case mediators::ToolId::Gradient:
            m_ctx->setState(std::unique_ptr<ToolState>(createGradientTool()));
            break;
        case mediators::ToolId::PaintBucket:
            m_ctx->setState(std::unique_ptr<ToolState>(createPaintBucketTool()));
            break;
        case mediators::ToolId::DodgeBurn:
            m_ctx->setState(std::unique_ptr<ToolState>(createDodgeBurnTool()));
            break;
        case mediators::ToolId::BlurSharpenSmudge:
            m_ctx->setState(std::unique_ptr<ToolState>(createBlurSharpenSmudgeTool()));
            break;
        case mediators::ToolId::Hand:
            m_ctx->setState(std::unique_ptr<ToolState>(createHandTool()));
            break;
        case mediators::ToolId::Zoom:
            m_ctx->setState(std::unique_ptr<ToolState>(createZoomTool()));
            break;
        case mediators::ToolId::Rotate:
            m_ctx->setState(std::unique_ptr<ToolState>(createRotateViewTool()));
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
