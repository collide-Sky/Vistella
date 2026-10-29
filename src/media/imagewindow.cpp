#include "imagewindow.h"
#include "ui_imagewindow.h"
#include "graphicstextitem.h"
#include "logger.h"
#include "mediators/ToolMediator.h"

#include "imageprocessor.h"
// recentmanager.h 移到 ImageIOController.cpp (open/save 段搬走)
#include "../core/ThemeManager.h"

// 组件 include
#include "imagewindow/TextOverlayController.h"
#include "imagewindow/MosaicTool.h"
#include "imagewindow/ImageIOController.h"
#include "imagewindow/ImageCanvas.h"
// LeftToolBar + ImageOptionBar 接入左侧 dock
//   之前写完 cpp/h/ui 但 ctor 从没创建 — 真实接入, 不做假控件
#include "tools/LeftToolBar.h"
#include "tools/ImageOptionBar.h"
#include "tools/BrushOptionsPanel.h"  // m_brushOpt member
#include "tools/LeftSidePanel.h"     // 单容器装 4 左侧 widget
// 新组件 AdjustmentPanel (5 tab 色彩调整)
#include "imagewindow/AdjustmentPanel.h"
#include "mediators/DialogMediator.h"
#include "dialogs/AdjustDialogBase.h"
// RightPanelStack 3 dock (颜色/属性/图层) — 浮动在 imagewindow 右上角
#include "docks/RightPanelStack.h"
#include "docks/RightPanelDock.h"
#include "docks/LayersDock.h"
#include "docks/ChannelPathPanel.h"
#include "docks/PropertiesDock.h"
#include "docks/HistoryDock.h"
#include "docks/ColorDock.h"  // RightPanelDock connectors
#include "tools/Brush.h"      // P6: dynamic_cast to setPaintColor
// LayerPanel 实例化 + 注入 LayersDock tab 0
//   LayerPanel 信号接通需要完整 LayerPanel.h + LayerCommand.h 定义
#include "imageworker/layers/LayerPanel.h"
#include "imageworker/layers/LayerCommand.h"
#include "imageworker/layers/LayerMaskCommand.h"
// Unified transform dialog replaces chained QInputDialog
#include "docks/TransformDialog.h"
#include "../media/mediators/WorkspaceMediator.h"
// selection system
#include "selection/SelectionModel.h"
#include "selection/SelectionStrategy.h"
#include "selection/SelectionCommand.h"
// filter system
#include "filters/FilterStrategy.h"
#include <QMutex>        // P3.1.3 preview registry mutex
#include <QMutexLocker>  // P3.1.3 preview registry lock guard
#include <QHash>         // P3.1.3 preview registry map
#include "filters/FilterFactory.h"
#include "filters/FilterCommand.h"
// ToolContext (state machine) for 8 tools event forwarding
#include "tools/ToolContext.h"
// TransformTool 接入 (Ctrl+T 自由变换)
#include "tools/TransformTool.h"
// transform system (6 menu action + 8 handle 自由变换)
#include "transform/TransformBox.h"
#include "transform/TransformCommand.h"

// QFileDialog / QDir / QMessageBox moved to
//   ImageIOController.cpp (open / save / saveAs segments relocated;
//   no longer used in imagewindow.cpp).
// SmartObject source-changed prompt upgraded to
//   QMessageBox (Yes / No so user can decide whether to refresh).
#include <QFileInfo>
#include <QMessageBox>
// AppBar onCaptureRequested uses QPixmap + QStandardPaths + QDir + QDateTime
#include <QPixmap>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
// LayerPanel 信号接通需要 (EditSource 打开源 / Relink 选源 / URL 包装)
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextDocument>
#include <QToolButton>  // P6: syncQuickMaskUi uses btn->blockSignals + setChecked
#include <QMenuBar>     // P5.5+: AppBar applyScreenMode uses menuBar()
#include <QWheelEvent>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// =============================================================
// TextItemCommand - 文字 item 的增/删/改/移动 撤销命令
//   主流做法: 文字作为 scene item, 撤销栈只记录 item 操作 (不记录像素)
// =============================================================

TextItemCommand::TextItemCommand(ImageWindow *w, GraphicsTextItem *item, Op op,
                                 const QString &oldText, const QString &newText,
                                 const QPointF &oldPos, const QPointF &newPos,
                                 qreal oldRot, qreal newRot,
                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_w(w)
    , m_item(item)
    , m_op(op)
    , m_oldText(oldText)
    , m_newText(newText)
    , m_oldPos(oldPos)
    , m_newPos(newPos)
    , m_oldRot(oldRot)
    , m_newRot(newRot)
{
    if (op == Add) {
        setText(QObject::tr("添加文字"));
    } else if (op == Remove) {
        setText(QObject::tr("删除文字"));
    } else if (op == Change) {
        setText(QObject::tr("修改文字"));
    } else if (op == Move) {
        setText(QObject::tr("移动文字"));
    } else if (op == Rotate) {
        setText(QObject::tr("旋转文字"));
    }
}

void TextItemCommand::undo()
{
    if (m_w.isNull()) return;
    auto *win = m_w.data();
    if (m_op == Add) {
        // Add undo: 移除 item
        if (m_item.isNull()) return;
        win->unregisterTextItem(m_item.data());
        // m_scene 搬到 m_canvas
        win->m_canvas->scene()->removeItem(m_item.data());
        delete m_item.data();
        m_item = nullptr;
    } else if (m_op == Remove) {
        // Remove undo: 重新创建 item (用保存的 oldText)
        auto *item = new GraphicsTextItem();
        item->setPosition(m_oldPos);
        item->setRotationDeg(m_oldRot);
        item->restoreFromText(m_oldText);
        // m_scene 搬到 m_canvas
        win->m_canvas->scene()->addItem(item);
        win->registerTextItem(item);
        m_item = item;
    } else {
        // Change / Move / Rotate: 操作已存在的 item, 安全性 check
        if (m_item.isNull()) return;
        if (m_op == Change) {
            m_item.data()->restoreFromText(m_oldText);
        } else if (m_op == Move) {
            m_item.data()->setPosition(m_oldPos);
        } else if (m_op == Rotate) {
            m_item.data()->setRotationDeg(m_oldRot);
        }
    }
}

void TextItemCommand::redo()
{
    if (m_w.isNull()) return;
    auto *win = m_w.data();
    if (m_op == Add) {
        if (m_firstRedo) {
            // Add redo 第一次: item 已经被 createTextItem 创建并 addItem 了, 不再重复
            m_firstRedo = false;
            return;
        }
        // 后续 redo: 重新创建 (用保存的 oldText) + 关键: 更新 m_item 指向新 item
        auto *item = new GraphicsTextItem();
        item->setPosition(m_oldPos);
        item->setRotationDeg(m_oldRot);
        item->restoreFromText(m_oldText);
        // m_scene 搬到 m_canvas
        win->m_canvas->scene()->addItem(item);
        win->registerTextItem(item);
        // 关键: 重新 connect editingFinished + transformFinished, 否则 redo 后的 item
        //      编辑后不会 push Change, 拖动/旋转后不会 push Move/Rotate
        win->connectTextItemSignals(item);
        m_item = item;  // 关键: 让后续 Change/Move 命令能跟踪到这个新 item
    } else if (m_op == Remove) {
        if (m_firstRedo) {
            // Remove redo 第一次: item 已经在外部删除了
            m_firstRedo = false;
            return;
        }
        if (!m_item.isNull()) {
            win->unregisterTextItem(m_item.data());
            // m_scene 搬到 m_canvas
            win->m_canvas->scene()->removeItem(m_item.data());
            delete m_item.data();
            m_item = nullptr;
        }
    } else {
        // Change / Move / Rotate: 安全性 check
        if (m_item.isNull()) return;
        if (m_op == Change) {
            m_item.data()->restoreFromText(m_newText);
        } else if (m_op == Move) {
            m_item.data()->setPosition(m_newPos);
        } else if (m_op == Rotate) {
            m_item.data()->setRotationDeg(m_newRot);
        }
    }
}

// =============================================================
// ImageEditCommand
// =============================================================

ImageEditCommand::ImageEditCommand(ImageWindow *w,
                                   const ParamSet &before,
                                   const ParamSet &after,
                                   const QString &text,
                                   QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_w(w)
    , m_before(before)
    , m_after(after)
{}

// 第二个构造: 用于马赛克/文字等 in-place 改 m_current 的操作
// imageBefore/imageAfter 分别是操作前/后 m_current 的完整快照
ImageEditCommand::ImageEditCommand(ImageWindow *w,
                                   const cv::Mat &imageBefore,
                                   const cv::Mat &imageAfter,
                                   const QString &text,
                                   QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_w(w)
    , m_imgBefore(imageBefore.clone())  // 深拷贝, 防止调用方 Mat 后续被改
    , m_imgAfter(imageAfter.clone())
    , m_useImage(true)
{}

void ImageEditCommand::undo()
{
    if (m_w.isNull()) return;   // ImageWindow 已销毁, 安全跳过
    if (m_useImage) {
        m_w->setDocumentImage(m_imgBefore);
    } else {
        m_w->applyParams(m_before, /*repaint*/true);
    }
}

void ImageEditCommand::redo()
{
    if (m_w.isNull()) return;
    if (m_useImage) {
        //  文档级写入是幂等的 (setDocumentImage 只换 base layer + 原图基准),
        //  所以 redo 可以安全地重放一次 —— Ctrl+Z 之后还能 Ctrl+Y 回来.
        //  像素级编辑不走这条路, 它们用 LayerCommand::SetLayerImage.
        m_w->setDocumentImage(m_imgAfter);
    } else {
        m_w->applyParams(m_after, /*repaint*/true);
    }
}

// =============================================================
// ImageWindow
// =============================================================

ImageWindow::ImageWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ImageWindow)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("ImageWindow"));
    // 关键: 当作 tabWidget 的子 widget 时, 必须去掉 top-level 标志
    // 否则 QMainWindow 内部会创建隐藏的 native window, 关闭时留下残影
    setWindowFlags(Qt::Widget);

    // EngineContext 实例化
    //   Profile::ImageEditor = 5 池预设 (interactive / background / io / ai / offline)
    //   显式传 hw=2 限 2 线程 - 跟开发机 4 核环境下 background 池开 4 线程的默认行为相比,
    //   减少一些上下文切换开销, 也避免某些 CI 机器 (1-2 核) 起 4 线程池被 OOM
    m_engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 2));
    m_currentTaskId.store(0);

    // 画布搬到 ImageCanvas 组件
    //   m_canvas 替换 ui->graphicsView 实际进入 verticalLayout
    //   之前: m_canvas 创建后 parent = this (顶层), 没进 layout, 跑到 (0,0) 显示成小方块
    //         ui->graphicsView 是空 QGraphicsView 容器, 显示空白
    //   现在: 拿掉 ui->graphicsView, 把 m_canvas 加进 verticalLayout, canvas 占满中央
    //   P7.6.x: raw pointer — parent = this 暂时, verticalLayout->addWidget 会 reparent
    m_canvas = new ImageCanvas(this);
    m_canvas->setHost(this);
    // 用 m_canvas 替换 verticalLayout 里的 ui->graphicsView 容器 (Qt 转移所有权)
    if (ui->graphicsView) {
        // 1. 隐藏/脱离原 ui->graphicsView (空容器)
        ui->graphicsView->setParent(nullptr);
        ui->graphicsView->hide();
        // 2. m_canvas 重新 parent 到 centralWidget (verticalLayout 跟 centralWidget 同 owner)
        m_canvas->setParent(ui->centralWidget);
        // 3. 用 size policy 跟 graphicsView 保持一致 (Expanding/Expanding)
        m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 4. verticalLayout 拿掉 graphicsView, 加 m_canvas (添加到 toolbar 后面)
        ui->verticalLayout->removeWidget(ui->graphicsView);
        ui->verticalLayout->addWidget(m_canvas);
    }
    // 在 m_canvas 的 viewport 上装 eventFilter (原 ui->graphicsView->viewport())
    m_canvas->viewport()->installEventFilter(this);
    // 焦点策略 + ScrollHandDrag 等都在 ImageCanvas ctor 里设

    // 笔刷光标 (默认隐藏, 鼠标移进 viewport 才显示)
    //   P0-1.3: m_brushCursor 加到 m_canvas->scene() (原 m_scene)
    {
        // 默认 30 px (跟 MosaicOptionPanel slider 默认一致)
        const int initialSize = 30;
        const int r0 = initialSize / 2;
        m_brushCursor = m_canvas->scene()->addEllipse(-r0, -r0,
            initialSize, initialSize);
        QPen pen(QColor(76, 175, 128, 220));    // 浅绿
        pen.setWidth(2);
        pen.setCosmetic(true);  // 不随缩放变化
        m_brushCursor->setPen(pen);
        m_brushCursor->setBrush(QColor(76, 175, 128, 40));
        m_brushCursor->setZValue(999);
        m_brushCursor->setVisible(false);
    }

    buildActions();
    // applyPanelTheme removed — applied dead stylesheet
    //   to the now-deleted imagewindow.ui leftPanel. Dock widgets pick up
    //   their visuals from Qt theming instead. ThemeManager::themeChanged
    //   signal connect below also removed.

    // 实例化 5 个组件
    //   这一轮 P0-1.2 只用 m_textCtrl + m_mosaicTool, 其他 nullptr 占位等下一轮
    // 默认值硬编码 (之前从 ui 占位控件读)
    //   - 字体 Microsoft YaHei UI (跟 TextOverlayController::m_textFont 默认一致)
    //   - 字号 24 (跟 P0-7 默认一致)
    //   - 涂抹 size 30 (跟 .ui sliderMosaicSize 默认一致)
    //   - 涂抹 type Pixelate=0 (跟 MosaicTool::m_type 默认一致)
    m_textCtrl    = std::make_unique<TextOverlayController>(this);
    m_textCtrl->setHost(this);
    m_textCtrl->setTextFont(QStringLiteral("Microsoft YaHei UI"));
    m_textCtrl->setTextSize(24);
    m_textCtrl->setTextColor(QColor(Qt::white));

    m_mosaicTool  = std::make_unique<MosaicTool>(this);
    m_mosaicTool->setHost(this);
    m_mosaicTool->setSize(30);
    m_mosaicTool->setType(MosaicTool::Pixelate);

    // IO 控制器 (open / save / saveAs / close) 实例化
    //   接管原 ImageWindow::onOpen / onSave / onSaveAs / onClose 4 个 slot
    //   m_io 持有 m_host (this) 引用, 通过 friend class + 代理方法触发
    //   markSaved / setFilePath / emitEditTimeShouldUpdate / emitCloseRequested
    m_io = std::make_unique<ImageIOController>(this);
    m_io->setHost(this);

    // PS 风格右侧 panel (1 个 widget 装 4 dock + 1 调整 tab)
    //   撤销原 3 个分离 dock (RightPanelStack + adjDock + infoDock) + addDockWidget 抢画布位置
    // 走 Qt 原生 addDockWidget + resizeDocks (替代 setGeometry 浮动)
    //   之前: RightPanelDock 直接 setGeometry 浮动在右上角, 跟 centralWidget 抢画布位置,
    //         resize 不联动 (imagewindow 缩小后 dock 还停在原位置)
    //   现在: 包到 QDockWidget 走 RightDockWidgetArea, Qt 自动管理 dock 与 canvas 边界,
    //         resizeDocks 设初始宽度 340, imagewindow resize 时 dock 跟着缩
    //   P7.6.x: raw pointer — Qt takes ownership via setWidget() later.
    m_rightDock = new docks::RightPanelDock(this);

    // PS 风格色彩调整 UI (5 tab Curves/Levels/HSL/B&W/ChannelMixer)
    //   直接用右侧面板自带的那个 AdjustmentPanel (属性组的
    //   "Adjustments" 页), 不再单独 new 一个.
    //   之前 ImageWindow 自己 new 了一个 AdjustmentPanel(parent = this), 而
    //   setAdjustmentPanel() 早已是 deprecated 空壳 —— 那个面板从没进过任何
    //   layout, 于是它按默认几何画在画布左上角 (那条突兀的 "曲线" 标签就是它),
    //   同时右侧面板里用户真正看到的 Adjustments 页没有 host, 按了没反应.
    //   一个面板一个归属: 右侧面板的属性组.
    m_adjustmentPanel = m_rightDock->adjustmentPage()->inner();
    m_adjustmentPanel->setHost(this);

    // Per-window DialogMediator. Hosts the
    //   standalone Curves / Levels / B&W / ChannelMixer dialogs. The
    //   mediator self-wires to DialogFactory (P0 leftover 4); we add
    //   the applied() -> AdjustmentPanel::setStandaloneParams routing
    //   here so dialog results land in the per-window state.
    m_dialogMed = std::make_unique<mediators::DialogMediator>(this);
    connect(m_dialogMed.get(), &mediators::DialogMediator::dialogCreated,
            this, [this](const QString& dialogId, QObject* dlgObj) {
        auto* dlg = qobject_cast<dialogs::AdjustDialogBase*>(dlgObj);
        if (!dlg) return;
        connect(dlg, &dialogs::AdjustDialogBase::applied, this,
                [this, dialogId](const QVariantMap& args) {
            if (m_adjustmentPanel) {
                m_adjustmentPanel->setStandaloneParams(dialogId, args);
            }
        });
    });

    // wire RightPanelDock connectors that were dangling before
    //   Color fg/bg -> current Brush paint color; ChannelPathPanel selection
    //   save/load + Quick Mask three-way sync (AppBar / ChannelPathPanel / tool).
    // extend ColorDock fg/bg to also push into LeftToolBar
    //   so the swatches at the bottom of the left toolbar stay in lockstep
    //   with the right-side Color dock (PS 风格 one swatch, two docks).
    //   Echo loops are guarded by the `if (m_xxx == c) return;` early-out in
    //   both setters, so the chain terminates after the first round.
    // ColorDock moved into VColorPage ->
    //   access via colorPage()->inner() (the underlying ColorTab widget).
    if (auto* colorPage = m_rightDock->colorPage()) {
        if (auto* colorTab = colorPage->inner()) {
            connect(colorTab, &docks::ColorTab::foregroundColorChanged,
                    this, &ImageWindow::onForegroundColorChanged);
        }
        // ColorDock fg/bg → LeftToolBar 同步搬到 MainWindow
        //   (MainWindow::bindLeftDockWidgets 在 active image 切换时连). 避免 ImageWindow
        //   仍然持有 m_leftToolBar 指针.
    }
    if (auto* chanPathPage = m_rightDock->channelPathPage()) {
        if (auto* selPanel = chanPathPage->inner()) {
            connect(selPanel, &docks::ChannelPathPanel::saveSelectionRequested,
                    this, &ImageWindow::onSaveSelectionRequested);
            connect(selPanel, &docks::ChannelPathPanel::loadSelectionRequested,
                    this, &ImageWindow::onLoadSelectionRequested);
            connect(selPanel, &docks::ChannelPathPanel::quickMaskToggled,
                    this, &ImageWindow::onChannelPathQuickMaskToggled);
        }
    }

    m_rightDockContainer = new QDockWidget(this);
    m_rightDockContainer->setObjectName("rightDockContainer");
    // NoDockWidgetFeatures: 隐藏关闭/浮动按钮 (Stage D 通过 workspace 按钮控制显隐)
    m_rightDockContainer->setFeatures(QDockWidget::NoDockWidgetFeatures);
    // 隐藏 title bar (PS 风格 panel 无标题栏)
    auto* emptyTitle = new QWidget();
    emptyTitle->setFixedHeight(0);
    m_rightDockContainer->setTitleBarWidget(emptyTitle);
    // 把 RightPanelDock 装到 QDockWidget 内部 (Qt 接管所有权, dock 销毁时 delete)
    //   P7.6.x: no release() needed — m_rightDock is raw pointer, Qt adopts it.
    m_rightDockContainer->setWidget(m_rightDock);

    addDockWidget(Qt::RightDockWidgetArea, m_rightDockContainer);
    resizeDocks({m_rightDockContainer}, {340}, Qt::Horizontal);
    // resizeDocks only sets the initial width. Without
    //   a range the user could drag the panel to any width, including widths
    //   that squeeze the layer list and the colour swatches into nothing.
    m_rightDockContainer->setMinimumWidth(280);
    m_rightDockContainer->setMaximumWidth(560);

    // ToolMediator per-ImageWindow (parent = this)
    //   Owns Mediator, hands raw pointer to ToolContext; both LeftToolBar and
    //   MainWindow's menu actions can now reach it through toolMediator().
    m_toolMed = std::make_unique<mediators::ToolMediator>(this);

    // ToolContext 实例化 (state machine for 8 tools)
    //   m_ctx 在 ctor 创建, eventFilter 通过 m_ctx->onMouseXxx 转发给 current ToolState
    //   attach 后 LeftToolBar/ImageOptionBar 可以通过 toolContext() 拿到
    m_ctx = std::make_unique<tools::ToolContext>(this);
    m_ctx->attach(this, m_toolMed.get());

    // LeftSidePanel + 4 子 widget + m_brushOpt + 左 dock 容器
    //   全部搬到 MainWindow (P7.6.x v3 commit 1). ImageWindow 只剩画布 + 业务数据 +
    //   右侧 dock (P7.6.x v3 commit 5+ 处理).
    //
    //   MainWindow 现在负责:
    //     - 创建 4 个 dock widget (LeftToolBar + ImageOptionBar + ModeSelectorBar + AppBar)
    //     - 创建 LeftSidePanel 单容器 + LeftDockContainer
    //     - 在 tabWidget->currentChanged 时重新 attach 到当前 active image 的
    //       toolMed/ctx/mosaicTool/textCtrl/brushOpt
    //
    //   ImageWindow 暴露的接口 (都是 raw pointer):
    //     - toolMediator() / toolContext() / mosaicTool() / textOverlay()
    //   都还在, MainWindow 直接拿.

    // TopOptionBar 删除. 6 控件 (mode/opacity/flow/smoothing/
    //   angle/size) 搬到 ImageOptionBar 内的 BrushOptionsPanel (idx=7).
    //   顶部工具栏 (QToolBar + addToolBar) 不再有, 中央画布上方就是 image 区域.
    //   顶部菜单栏 (QMenuBar) 仍然在 MainWindow 提供 视图→放大/缩小 入口.

    // P5.5+ AppBar 初值: 标准屏幕模式 (启动时默认). 现在由 MainWindow 触发
    //   (AppBar 在 MainWindow 顶层, 不再跟 ImageWindow 挂钩).

    // TopOptionBar 删除. 6 控件 (mode/opacity/flow/smoothing/
    //   angle/size) 搬到 ImageOptionBar 内的 BrushOptionsPanel (idx=7).
    //   撮合在 ImageOptionBar::attach() 第 4 参数 brushOpt 里 (上面已经传入 m_brushOpt).
    //   顶部工具栏 (QToolBar + addToolBar) 不再有, 中央画布上方就是 image 区域.
    //   顶部菜单栏 (QMenuBar) 仍然在 MainWindow 提供 视图→放大/缩小 入口.

    // MosaicOptionPanel + TextOptionPanel no longer hosted
    //   here. ImageOptionBar owns both and routes them per active tool
    //   (Brush -> Mosaic, Text -> Text). The Q4.2.2.1 leftPanel gridLayout_4
    //   hosting left them always visible (~200 + ~200 px blank chrome) which
    //   the user kept flagging; routing them through ImageOptionBar gives
    //   them the same active-tool UX as every other tool option.
    // (imagewindow.ui gridLayout_4 stays empty for now as a reserved slot;
    //  it can host other widgets later, e.g. quick mask / screen mode.)

    // SelectionModel 实例化
    //   ImageCanvas 拿 selection 引用, drawForeground 画 marching ants 边界
    //   setSize 在 loadFile 后调, 跟 image 像素对齐
    m_selection = std::make_unique<selection::SelectionModel>(this);
    if (m_canvas) {
        m_canvas->setSelectionModel(m_selection.get());
    }
    // P0-4.9: PropertiesDock 同步选区 bbox (selection changed -> dock.setSelectionBbox)
    if (m_rightDock) {
        if (auto* props = m_rightDock->findChild<docks::PropertiesDock*>()) {
            connect(m_selection.get(), &selection::SelectionModel::changed,
                    [this, props]() {
                if (m_selection) props->setSelectionBbox(m_selection->boundingRect());
            });
            // PropertiesDock 同步 text item 选中状态
            //   TextOverlayController::currentChanged -> setTextProperties / clearTextProperties
            //   走 QPointer 防 item delete 后 dangling
            if (m_textCtrl) {
                connect(m_textCtrl.get(), &TextOverlayController::currentChanged,
                        this, [this, props](GraphicsTextItem* item) {
                    if (!item) {
                        props->clearTextProperties();
                        return;
                    }
                    QPointer<GraphicsTextItem> weakItem(item);
                    const auto style = m_textCtrl->getCurrentStyle();
                    docks::PropertiesDock::TextProperties p;
                    p.font = style.font;
                    p.size = style.size;
                    p.color = style.color;
                    p.bold = style.bold;
                    p.italic = style.italic;
                    if (!weakItem.isNull()) p.pos = weakItem.data()->position();
                    props->setTextProperties(p);
                });
            }
        }
    }

    // ChannelPathPanel 现在是 RightPanelDock 第 4 tab
    //   (之前是 LayersDock tab 1, 拆出来独立 tab 更 PS 风格)
    //   LayersDock 现在是第 3 tab, 只装 LayerPanel (F-G.3 集成)
    //   ChannelPathPanel 已经在 RightPanelDock ctor 创建

    // 兜底: 显式关闭马赛克按钮, 避免启动就进入涂抹
    //   删 ui->btnMosaicMode 引用, MosaicOptionPanel 默认 unchecked
    m_mosaicTool->setEnabled(false);
    // 兜底: 文字 item 都不锁双击 (启动时非涂抹模式)
    for (auto *ti : m_textCtrl->textItems()) { if (ti) ti->setBlockDoubleClickEdit(false); }

    // 撤销栈
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(200);
    m_savedIndex = 0;
    connect(m_undoStack, &QUndoStack::indexChanged, this, &ImageWindow::onStackChanged);
    // 同步主窗口的 撤销/重做 action: 用 undoStack 的 canUndo/canRedo 状态
    // 主窗口在 onUndo/onRedo 里直接调 m_undoStack->undo/redo

    // 绑定 HistoryDock -> undoStack
    //   forward via RightPanelDock::setUndoStack
    //   which routes to the VHistoryPage adapter.
    if (m_rightDock) {
        m_rightDock->setUndoStack(m_undoStack);
    }

    }

ImageWindow::~ImageWindow()
{
    // reset m_brushCursor FIRST, before delete ui
    //   delete ui destroys m_canvas (a child widget), whose scene owns the
    //   ellipse item and deletes it in ~QGraphicsScene. If m_brushCursor is
    //   not reset before that, any subsequent access (especially
    //   MosaicTool::setSize calling bc->scene()) hits a use-after-free
    //   SEGFAULT. Reset here while dtor body is still valid context.
    m_brushCursor = nullptr;

    // 关键: 先 clear undoStack, 触发 ~ImageEditCommand
    // 这样 QPointer<ImageWindow> m_w 在 ImageWindow 还没半析构时变 null
    // 避免 close 时 "class destructor may have already run" ASSERT
    if (m_undoStack) {
        m_undoStack->disconnect();
        m_undoStack->clear();
    }

    // 清空文字 item 列表 (避免 scene 析构时 item 还在 scene 里)
    //   委托给 m_textCtrl (TextOverlayController)
    if (m_textCtrl) {
        m_textCtrl->removeAllFromSceneAndDelete();
    }
    // 作废在飞的异步任务: bump task id, 让所有还没回来的回调自己判定过期.
    //   (关 tab / 关程序时后台 render 可能还在跑, 回调回来时窗口已经没了)
    m_currentTaskId.fetch_add(1);

    // EngineContext 析构会 ~ThreadPool -> shutdown(false)
    //   等所有 worker 退出后再删, 避免后台 task 跑一半 ImageWindow 已经销毁
    m_engine.reset();
    // m_selection 析构时 ImageCanvas::m_sel 弱引用自动失效
    m_selection.reset();
    delete ui;
}

// selectedLayerIndex public getter
//
//   Returns the visible row index of LayerPanel's QTreeWidget current item.
//   Matches LayerStack index 1:1 (m_tree is flat-ordered). Group child items
//   hold a QString "child:<gid>:<idx>" in UserRole which we cannot return
//   as a top-level index — caller gets -1 in that case.
//
//   Used by MainWindow layer menu actions (Duplicate / Delete / MoveUp /
//   MoveDown / Merge Down) that target a single layer.
int ImageWindow::selectedLayerIndex() const
{
    if (!m_layerPanel) return -1;
    return m_layerPanel->selectedRowForTest();
}

// applyLayerOp — MainWindow menu action dispatcher
//
//   Wraps the layerStack + undoStack + invalidateCurrentCache + renderToView
//   sequence so MainWindow menu handlers don't need to know ImageWindow's
//   internal repaint lifecycle.
//
//   Returns true if the op was applied (or attempted), false if the
//   ImageWindow has no layer stack / undo stack / valid selection.
//
//   Pattern (MosaicTool-style): for ops that mutate the layer set, push a
//   LayerCommand to undoStack BEFORE invoking the mutating call (LayerCommand
//   stores the before-state; undo restores it).
//
bool ImageWindow::applyLayerOp(LayerOp op)
{
    if (!m_layerStack) return false;
    const int totalLayers = m_layerStack->count();
    const int sel = selectedLayerIndex();

    auto pushImageEdit = [this](const QString& cmdText) {
        if (!m_undoStack) return;
        cv::Mat backup = m_current.clone();
        cv::Mat current = m_current.clone();    // same content; kept for symmetry
        m_undoStack->push(new ImageEditCommand(this, backup, current, cmdText));
    };

    switch (op) {
    case LayerOp::NewBitmap: {
        // New blank bitmap layer, push LayerCommand(Add, *l) for undo.
        const int newIdx = m_layerStack->addLayer(
            QStringLiteral("Layer %1").arg(totalLayers + 1), cv::Mat());
        if (newIdx < 0) return false;
        if (m_undoStack) {
            auto l = m_layerStack->at(newIdx);
            if (l) m_undoStack->push(new layers::LayerCommand(
                m_layerStack.get(), layers::LayerCommand::Add, *l));
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("已新建图层"), 2000);
        return true;
    }
    case LayerOp::Duplicate: {
        if (sel < 0 || sel >= totalLayers) {
            statusBar()->showMessage(tr("未选中图层"), 2000); return false;
        }
        if (!m_layerStack->duplicateLayer(sel)) return false;
        const int newIdx = m_layerStack->count() - 1;
        if (m_undoStack) {
            auto l = m_layerStack->at(newIdx);
            if (l) m_undoStack->push(new layers::LayerCommand(
                m_layerStack.get(), layers::LayerCommand::Add, *l));
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("已复制图层"), 2000);
        return true;
    }
    case LayerOp::Remove: {
        if (sel < 0 || sel >= totalLayers) {
            statusBar()->showMessage(tr("未选中图层"), 2000); return false;
        }
        if (m_undoStack) {
            auto* cmd = layers::LayerCommand::makeRemove(m_layerStack.get(), sel);
            if (cmd) m_undoStack->push(cmd);
            else m_layerStack->removeLayer(sel);
        } else {
            m_layerStack->removeLayer(sel);
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("已删除图层"), 2000);
        return true;
    }
    case LayerOp::MoveUp: {
        if (sel < 0 || sel >= totalLayers) return false;
        if (m_undoStack) {
            // makeMoveUp factory applies the move
            //   itself; caller no longer needs an explicit moveUp call.
            //   The old push+moveUp pattern double-moved.
            m_undoStack->push(layers::LayerCommand::makeMoveUp(
                m_layerStack.get(), sel));
        } else {
            m_layerStack->moveUp(sel);
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("上移图层"), 2000);
        return true;
    }
    case LayerOp::MoveDown: {
        if (sel < 0 || sel >= totalLayers) return false;
        if (m_undoStack) {
            // makeMoveDown factory applies the move
            //   itself; caller no longer needs an explicit moveDown call.
            m_undoStack->push(layers::LayerCommand::makeMoveDown(
                m_layerStack.get(), sel));
        } else {
            m_layerStack->moveDown(sel);
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("下移图层"), 2000);
        return true;
    }
    case LayerOp::MergeDown: {
        if (sel <= 0 || sel >= totalLayers) {
            statusBar()->showMessage(tr("无法合并 (已在最底层)"), 2000); return false;
        }
        if (m_undoStack) {
            m_undoStack->push(new layers::LayerCommand(
                m_layerStack.get(), sel));
        } else {
            m_layerStack->mergeDown(sel);
        }
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("已向下合并"), 2000);
        return true;
    }
    case LayerOp::FlattenVisible: {
        if (totalLayers <= 1) {
            statusBar()->showMessage(tr("无需拼合 (图层数 ≤ 1)"), 2000); return false;
        }
        // Flatten: compose all visible layers and replace base layer.
        //   The full undo entry includes both the layer mutation (m_layerStack
        //   is restored from LayerCommand::Flatten cmd) and the image mutation
        //   (ImageEditCommand before/after).
        cv::Mat backup = m_current.clone();
        cv::Mat m = m_layerStack->flattenVisible();
        if (m.empty()) return false;
        m_layerStack->setBaseLayer(m);
        invalidateCurrentCache();
        renderToView();
        // We pushed the image backup before mutation; mirror after with the
        //   flattened m_current so undo restores the old image.
        if (m_undoStack) {
            m_undoStack->push(new ImageEditCommand(this, backup, m_current.clone(),
                tr("拼合图像")));
        }
        statusBar()->showMessage(tr("已拼合图像"), 2000);
        return true;
    }
    }
    Q_UNUSED(pushImageEdit);  // silence unused warning (kept for future ops)
    return false;
}

// cv::Mat -> QImage (BGRA8888 / BGR888 / Gray) for selection strategies
QImage ImageWindow::currentImageAsQImage() const
{
    if (m_current.empty()) return QImage();
    const int W = m_current.cols;
    const int H = m_current.rows;
    if (W <= 0 || H <= 0) return QImage();
    switch (m_current.type()) {
    case CV_8UC3: {
        // BGR -> RGB swap, then wrap as QImage (Format_RGB888)
        cv::Mat rgb;
        cv::cvtColor(m_current, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, W, H, rgb.step, QImage::Format_RGB888).copy();
    }
    case CV_8UC4: {
        // BGRA -> RGBA swap
        cv::Mat rgba;
        cv::cvtColor(m_current, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data, W, H, rgba.step, QImage::Format_RGBA8888).copy();
    }
    case CV_8UC1: {
        // Grayscale
        return QImage(m_current.data, W, H, m_current.step, QImage::Format_Grayscale8).copy();
    }
    default:
        return QImage();
    }
}

// Layer-as-QImage accessor
//   BGR cv::Mat -> QImage::Format_RGB888 (ColorRange consumes QImage).
//   .copy() is required because QImage does not ref-count the borrowed
//   buffer; without .copy() the temp Mat goes out of scope and the
//   returned QImage points to freed memory.
QImage ImageWindow::layerStackAsQImage(int index) const
{
    if (!m_layerStack) return QImage();
    if (index < 0 || index >= m_layerStack->count()) return QImage();
    auto l = m_layerStack->at(index);
    if (!l || l->image.empty()) return QImage();
    const cv::Mat& src = l->image;
    const int W = src.cols;
    const int H = src.rows;
    if (W <= 0 || H <= 0) return QImage();
    cv::Mat rgb;
    if (src.channels() == 4) {
        cv::cvtColor(src, rgb, cv::COLOR_BGRA2RGB);
    } else if (src.channels() == 3) {
        cv::cvtColor(src, rgb, cv::COLOR_BGR2RGB);
    } else if (src.channels() == 1) {
        return QImage(src.data, W, H, src.step, QImage::Format_Grayscale8).copy();
    } else {
        return QImage();
    }
    return QImage(rgb.data, W, H, rgb.step, QImage::Format_RGB888).copy();
}

// =====================================================================
//  工具编辑目标 (活动图层)
// =====================================================================
//
//  工具编辑写进图层, 不写合成缓存 m_current.
//  m_current 是 layerStack->render() 的派生缓存, 任何 invalidate 都会
//  把它重算一遍 —— 直接改它等于把用户的编辑冲掉.
//
//  一次完整的工具编辑:
//    1. activeLayerImage() 拿到活动图层的像素缓冲, 就地涂抹
//    2. commitLayerEdit() 通知图层栈内容变了 + 重绘画布
//    3. pushLayerImageEdit() 把编辑前后的像素作为撤销命令入栈

cv::Mat* ImageWindow::activeLayerImage()
{
    if (!m_layerStack) return nullptr;
    return m_layerStack->activeLayerImage();
}

int ImageWindow::activeLayerIndex() const
{
    if (!m_layerStack) return -1;
    return m_layerStack->activeLayerIndex();
}

void ImageWindow::commitLayerEdit(int index)
{
    if (m_layerStack && index >= 0) {
        m_layerStack->notifyLayerChanged(index);
    }
    refreshCanvasNow();
}

void ImageWindow::pushLayerImageEdit(int index, const cv::Mat &before,
                                     const cv::Mat &after, const QString &text)
{
    if (!m_layerStack || !m_undoStack || index < 0) return;
    if (before.empty() || after.empty()) return;
    if (before.size() != after.size() || before.type() != after.type()) return;
    m_undoStack->push(layers::LayerCommand::makeSetLayerImage(
        m_layerStack.get(), index, before, after, text));
}

//  把 pixels 写进指定图层的像素缓冲, 通知图层栈 + 重绘画布.
void ImageWindow::writeLayerPixels(int index, const cv::Mat &pixels)
{
    if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
    if (pixels.empty()) return;
    auto l = m_layerStack->at(index);
    if (!l) return;
    l->image = pixels.clone();
    commitLayerEdit(index);
}

bool ImageWindow::loadFile(const QString &path, QString *err)
{
    // IMREAD_UNCHANGED 保留原 channels (灰度图保存后还能 reload)
    cv::Mat img = cv::imread(path.toLocal8Bit().toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        if (err) *err = QStringLiteral("cv::imread failed: %1").arg(path);
        return false;
    }
    // 统一转 8U (16bit PNG 之类 ImageProcessor 全部算法都是 8U)
    if (img.depth() != CV_8U) {
        cv::Mat tmp;
        img.convertTo(tmp, CV_8U);
        img = tmp;
    }
    m_original = img.clone();
    m_filePath = path;
    // m_zoom 搬到 m_canvas
    m_canvas->setZoom(1.0);

    // 默认参数 (clean 状态)
    // 旧 m_adjustment 已删除, ParamSet 在 阶段重新设计

    // 清空撤销栈
    m_undoStack->clear();
    m_savedIndex = 0;

    //  初始化 layerStack, 把加载的图片作为 base layer
    //   LayerStack 是 QObject, parent = this (生命周期跟随 ImageWindow)
    ensureLayerStack();
    m_layerStack->setBaseLayer(img);
    // 智能对象文件监听器
    //   监听每个 SmartObject layer 的源文件 mtime, 触发源变化时弹 statusBar 提示
    //   layerAdded/Removed/Changed 后 rewatchAll 同步 watch 列表 (MainWindow 4 slot
    //   也会显式调 rewatchAll, 这是兜底)
    // Upgraded from the 5-second statusBar hint to
    //   a QMessageBox dialog.
    //   Yes -> refreshSmartObject + invalidateCurrentCache + statusBar
    //   confirmation.
    //   No  -> just record a "skipped" status.
    m_smartWatcher = std::make_unique<layers::SmartObjectWatcher>(this);
    connect(m_smartWatcher.get(), &layers::SmartObjectWatcher::sourceFileChanged,
            this, [this](int idx, const QString &path){
        if (!m_layerStack) return;
        const QString name = QFileInfo(path).fileName();
        const QMessageBox::StandardButton ret = QMessageBox::question(
            this, tr("智能对象源已修改"),
            tr("源文件 \"%1\" 已在外部被修改.\n\n是否刷新此 SmartObject 图层?")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) {
            statusBar()->showMessage(tr("已跳过刷新: %1").arg(name), 3000);
            return;
        }
        if (idx < 0 || idx >= m_layerStack->count()) {
            statusBar()->showMessage(
                tr("智能对象索引已失效 (图层顺序已变): %1").arg(name), 5000);
            return;
        }
        if (m_layerStack->refreshSmartObject(idx)) {
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已刷新: %1").arg(name), 3000);
        } else {
            statusBar()->showMessage(
                tr("刷新失败 (源文件不存在或权限不足): %1").arg(name), 5000);
        }
    });
    auto resyncWatcher = [this]{
        if (m_smartWatcher && m_layerStack)
            m_smartWatcher->rewatchAll(m_layerStack.get());
    };
    connect(m_layerStack.get(), &layers::LayerStack::layerAdded, this, resyncWatcher);
    connect(m_layerStack.get(), &layers::LayerStack::layerRemoved, this, resyncWatcher);
    connect(m_layerStack.get(), &layers::LayerStack::layerChanged, this, resyncWatcher);
    resyncWatcher();
    //  监听 selection 变化
    //   切 layer 不改像素, 但要让画布知道, 触发重绘 + 通知 MainWindow
    connect(m_layerStack.get(), &layers::LayerStack::selectionChanged,
            this, &ImageWindow::onLayerSelectionChanged);
    // active layer 切换 → 右侧 panel V*Page 标脏
    //   VPropertyPage 显示当前 layer 的属性 (color/blend/opacity/lock/link),
    //   必须跟 selection 同步刷新. 走 RightPanelDock::invalidateAllPageCaches
    //   fan-out, 其他 V*Page (color/swatch/...) 的 invalidateCache 是 no-op 默认.
    connect(m_layerStack.get(), &layers::LayerStack::selectionChanged, this,
            [this](int) {
                if (m_rightDock) m_rightDock->invalidateAllPageCaches();
            });
    invalidateCurrentCache();   // 强制重算 m_current

    // 实例化 LayerPanel, 注入 LayersDock tab 0
    //   P1.4.2 留了 LayerPanel 类定义完整但没 new 的 bug, 这一轮补上.
    //   LayerPanel::ctor 需要 raw LayerStack*, parent = this (QObject parent-child 关系
    //   保证 unique_ptr 释放前 LayerPanel 不会被 Qt 析构). 注入 LayersDock tab 0 用
    //   setContentWidget + release() 转移, LayersDock reparent 后接管所有权.
    //   P7.6.x: raw pointer — setContentWidget below takes ownership.
    m_layerPanel = new layers::LayerPanel(m_layerStack.get());
    if (m_rightDock) {
        // rightDock->layersDock is now a
        // deprecated shim returning nullptr. Use setLayerPanel() which
        // forwards to VLayerPage->inner() (LayersDock).
        m_rightDock->setLayerPanel(m_layerPanel);
    }

    // LayerPanel 用户操作信号 → ImageWindow API (数据层)
    //   P7.6.x: m_layerPanel is a raw pointer that survives setContentWidget.
    if (m_layerPanel) {
        connect(m_layerPanel, &layers::LayerPanel::selectionChangedFromPanel,
                this, [this](int idx) {
            if (m_layerStack) m_layerStack->setSelection(idx);
        });
        // P1.4.3: 智能对象 Edit Source (复用 P1.4.2 主菜单的 QDesktopServices 路径, 不推 undoStack)
        connect(m_layerPanel, &layers::LayerPanel::editSmartObjectSourceRequested,
                this, [this](int idx) {
            if (!m_layerStack || idx < 0 || idx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(idx);
            if (!l || l->kind != layers::Layer::SmartObject || l->sourceFilePath.isEmpty()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(l->sourceFilePath));
            statusBar()->showMessage(tr("已在系统默认应用打开源文件"), 3000);
        });
        // P1.4.3: 智能对象 Refresh (重新加载源文件, embed/link 都生效)
        connect(m_layerPanel, &layers::LayerPanel::refreshSmartObjectRequested,
                this, [this](int idx) {
            if (!m_layerStack) return;
            m_layerStack->refreshSmartObject(idx);
            invalidateCurrentCache();
            statusBar()->showMessage(tr("智能对象已刷新"), 3000);
        });
        // P1.4.3: 智能对象 Toggle Embed (link <-> embed 切换, 不推 undoStack - 跟 P1.4.2 mainwindow 一致)
        connect(m_layerPanel, &layers::LayerPanel::toggleSmartObjectEmbedRequested,
                this, [this](int idx) {
            if (!m_layerStack) return;
            m_layerStack->toggleSmartObjectEmbed(idx);
            invalidateCurrentCache();
        });
        // P1.4.3: 智能对象 Convert (Bitmap -> SmartObject, 嵌入模式) - 推 undoStack 走 makeConvertToSmartObject
        connect(m_layerPanel, &layers::LayerPanel::convertToSmartObjectRequested,
                this, [this](int idx) {
            if (!m_layerStack || idx < 0 || idx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(idx);
            if (!l || l->kind != layers::Layer::Bitmap || l->image.empty()) return;
            // 备份 before layer 完整状态 (LayerCommand 需要 before 还原)
            const layers::Layer before = *l;
            if (!m_layerStack->convertToSmartObject(idx, /*embed=*/true)) {
                statusBar()->showMessage(tr("转换失败 (源文件不存在或无写入权限)"), 3000);
                return;
            }
            if (m_undoStack) {
                auto* cmd = layers::LayerCommand::makeConvertToSmartObject(
                    m_layerStack.get(), idx, before);
                m_undoStack->push(cmd);
            }
            refreshCanvasNow();
            statusBar()->showMessage(tr("已转换为智能对象 (嵌入模式)"), 3000);
        });
        // P1.4.3: 智能对象 Rasterize (SmartObject -> Bitmap) - 推 undoStack 走 makeRasterizeSmartObject
        connect(m_layerPanel, &layers::LayerPanel::rasterizeSmartObjectRequested,
                this, [this](int idx) {
            if (!m_layerStack || idx < 0 || idx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(idx);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            // 备份 before layer (含 sourceFilePath + sourceEmbedded)
            const layers::Layer before = *l;
            if (!m_layerStack->rasterizeSmartObject(idx)) {
                statusBar()->showMessage(tr("栅格化失败 (源文件不存在)"), 3000);
                return;
            }
            if (m_undoStack) {
                auto* cmd = layers::LayerCommand::makeRasterizeSmartObject(
                    m_layerStack.get(), idx, before);
                m_undoStack->push(cmd);
            }
            refreshCanvasNow();
            statusBar()->showMessage(tr("已栅格化智能对象"), 3000);
        });
        // P1.4.3: 智能对象 Relink (重新链接源文件) - 弹 QFileDialog 选文件, 推 undoStack 走 makeSetSmartObject
        connect(m_layerPanel, &layers::LayerPanel::relinkSmartObjectRequested,
                this, [this](int idx) {
            if (!m_layerStack || idx < 0 || idx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(idx);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            const QString path = QFileDialog::getOpenFileName(this, tr("选择源文件"), QString(),
                tr("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;所有 (*.*)"));
            if (path.isEmpty()) return;
            // 备份旧 path + embed
            const QString oldPath = l->sourceFilePath;
            const bool oldEmbed = l->sourceEmbedded;
            m_layerStack->setSmartObjectSource(idx, path, l->sourceEmbedded);
            if (m_undoStack) {
                auto* cmd = layers::LayerCommand::makeSetSmartObject(
                    m_layerStack.get(), idx, oldPath, oldEmbed);
                m_undoStack->push(cmd);
            }
            refreshCanvasNow();
            statusBar()->showMessage(tr("已重新链接源文件"), 3000);
        });
        // SmartFilter chain entry (PS-style smart filter)
        //   Right-click on a SmartObject layer -> apply filter dialog -> append.
        connect(m_layerPanel, &layers::LayerPanel::applySmartFilterRequested,
                this, [this](int smartIdx) {
            if (!m_layerStack || smartIdx < 0 || smartIdx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(smartIdx);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            static const QStringList kFilters = {
                QStringLiteral("GaussianBlur"),
                QStringLiteral("Sharpen"),
                QStringLiteral("Brightness"),
                QStringLiteral("Contrast"),
                QStringLiteral("Emboss"),
            };
            bool ok = false;
            const QString picked = QInputDialog::getItem(
                this, tr("选择智能滤镜"),
                tr("滤镜类型:"), kFilters, 0, false, &ok);
            if (!ok || picked.isEmpty()) return;
            const cv::Mat baseImage = m_layerStack->rasterizeForRender(smartIdx);
            const int newIdx = m_layerStack->appendSmartFilter(
                smartIdx, picked, 1.0, baseImage);
            if (newIdx < 0) {
                statusBar()->showMessage(tr("应用滤镜失败"), 3000);
                return;
            }
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeAppendSmartFilter(
                    m_layerStack.get(), newIdx, picked, 1.0);
                m_undoStack->push(cmd);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已应用智能滤镜: %1").arg(picked), 3000);
        });
        // SmartObject scale + rotate non-destructive transform
        //   from LayerPanel right-click menu. Pops scale + rotation prompts
        //   then routes through applySmartObjectTransform to apply + push undo.
        // chained QInputDialog upgraded to a single
        //   TransformDialog (Scale
        //   + Rotate + Translate + Link + Reset - 5 fields in one shot).
        connect(m_layerPanel, &layers::LayerPanel::transformSmartObjectRequested,
                this, [this](int smartIdx) {
            if (!m_layerStack || smartIdx < 0 || smartIdx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(smartIdx);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            docks::TransformDialog dlg(this);
            dlg.setInitial(l->transform);
            if (dlg.exec() != QDialog::Accepted) return;
            applySmartObjectTransform(smartIdx, dlg.result());
        });
        // P1.4.5: clear SmartObject transform from LayerPanel right-click.
        connect(m_layerPanel, &layers::LayerPanel::resetSmartObjectTransformRequested,
                this, [this](int smartIdx) {
            if (!m_layerStack || smartIdx < 0 || smartIdx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(smartIdx);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            // Identity QTransform clears hasTransform (PS semantics, see P1.4.1
            // LayerStack::setSmartObjectTransform "identity -> turn off" branch).
            applySmartObjectTransform(smartIdx, QTransform());
        });

        // ================================================================
        //  wire the remaining 26 LayerPanel signals
        //    Each lambda validates the index, calls the LayerStack API,
        //    pushes the matching LayerCommand, then invalidateCurrentCache
        //    + renderToView + statusBar hint. Complex operations (group /
        //    ungroup) that don't yet have complete LayerStack plumbing
        //    get a status-bar TODO rather than a half-implementation.
        // ================================================================

        // ---- Layer management -----------------------------------------
        // addLayerKindRequested: 5 kinds - 0=Bitmap / 1=Vector /
        //   2=Text /
        //   3=SmartObject / 4=Adjustment. Bitmap uses an empty Mat
        //   placeholder, Vector/Text/
        //   SmartObject/Adjustment get sensible defaults (when payload is
        //   absent, Layer::isValid
        //   would return false, but to not block the UI we just call addLayer
        //   so the user can continue editing).
        connect(m_layerPanel, &layers::LayerPanel::addLayerKindRequested,
                this, [this](int kind) {
            if (!m_layerStack) return;
            int newIdx = -1;
            QString suffix;
            switch (kind) {
            case 0: {   // Bitmap
                cv::Mat m = m_current.empty()
                    ? cv::Mat(64, 64, CV_8UC3, cv::Scalar(200, 200, 200))
                    : cv::Mat::zeros(m_current.size(), CV_8UC3);
                newIdx = m_layerStack->addLayer(
                    QStringLiteral("Bitmap %1").arg(m_layerStack->count() + 1),
                    m);
                suffix = tr("位图");
                break;
            }
            case 1: {   // Vector
                QVector<QPainterPath> paths;
                newIdx = m_layerStack->addVectorLayer(
                    QStringLiteral("Vector %1").arg(m_layerStack->count() + 1),
                    paths, QVector<QColor>());
                suffix = tr("矢量");
                break;
            }
            case 2: {   // Text
                newIdx = m_layerStack->addTextLayer(
                    QStringLiteral("Text %1").arg(m_layerStack->count() + 1),
                    tr("输入文字…"), 48, Qt::white, QStringLiteral("Arial"));
                suffix = tr("文字");
                break;
            }
            case 3: {   // SmartObject
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("选择智能对象源文件"), QString(),
                    tr("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;所有 (*.*)"));
                if (path.isEmpty()) return;
                newIdx = m_layerStack->addSmartObjectLayer(
                    QStringLiteral("SmartObject %1")
                        .arg(m_layerStack->count() + 1),
                    path, /*embed=*/false);
                suffix = tr("智能对象");
                break;
            }
            case 4: {   // Adjustment
                newIdx = m_layerStack->addAdjustmentLayer(
                    QStringLiteral("Adjustment %1")
                        .arg(m_layerStack->count() + 1),
                    QStringLiteral("curves"),
                    cv::Mat());
                suffix = tr("调整层");
                break;
            }
            default:
                return;
            }
            if (newIdx < 0) {
                statusBar()->showMessage(tr("新建图层失败"), 3000);
                return;
            }
            if (m_undoStack) {
                auto l = m_layerStack->at(newIdx);
                if (l) m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(), layers::LayerCommand::Add, *l));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("已新建 %1 图层").arg(suffix), 3000);
        });

        // deleteLayerRequested: pushes makeRemove, undo restores the full
        //   layer (including the Mat clone).
        connect(m_layerPanel, &layers::LayerPanel::deleteLayerRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeRemove(
                    m_layerStack.get(), index);
                if (cmd) m_undoStack->push(cmd);
                else m_layerStack->removeLayer(index);
            } else {
                m_layerStack->removeLayer(index);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已删除图层"), 3000);
        });

        // duplicateLayerRequested: goes through LayerStack::duplicateLayer
        //   (deep copy).
        //   Pushes LayerCommand::Add for undo (P1.4.1 already uses
        //   the same pattern).
        connect(m_layerPanel, &layers::LayerPanel::duplicateLayerRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            if (!m_layerStack->duplicateLayer(index)) {
                statusBar()->showMessage(tr("复制图层失败"), 3000);
                return;
            }
            const int newIdx = m_layerStack->count() - 1;
            if (m_undoStack) {
                auto l = m_layerStack->at(newIdx);
                if (l) m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(), layers::LayerCommand::Add, *l));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已复制图层"), 3000);
        });

        // moveUpRequested: index is the position of the layer that should
        //   be moved up.
        //   use makeMoveUp factory (it applies moveUp
        //   itself, undo reverses). The old push+moveUp double-moved.
        connect(m_layerPanel, &layers::LayerPanel::moveUpRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            if (m_undoStack) {
                m_undoStack->push(layers::LayerCommand::makeMoveUp(
                    m_layerStack.get(), index));
            } else {
                m_layerStack->moveUp(index);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("上移图层"), 2000);
        });

        // moveDownRequested: same pattern, direction = -1.
        connect(m_layerPanel, &layers::LayerPanel::moveDownRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            if (m_undoStack) {
                m_undoStack->push(layers::LayerCommand::makeMoveDown(
                    m_layerStack.get(), index));
            } else {
                m_layerStack->moveDown(index);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("下移图层"), 2000);
        });

        // mergeDownRequested: goes through LayerStack::mergeDown +
        //   LayerCommand::Merge.
        //   Known Phase 1 limitation: undo only restores the lower layer
        //   image, cannot fully split the upper layer (P1.4.6 inherits this).
        connect(m_layerPanel, &layers::LayerPanel::mergeDownRequested,
                this, [this](int index) {
            if (!m_layerStack || index <= 0 || index >= m_layerStack->count()) {
                statusBar()->showMessage(tr("无法合并 (已在最底层)"), 3000);
                return;
            }
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(), index));
            } else {
                m_layerStack->mergeDown(index);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已向下合并"), 3000);
        });

        // flattenVisibleRequested: PS "Flatten Image" -- mergeDown all + 1 base
        //   LayerStack::flattenVisible returns the merged cv::Mat
        //   (reserved for future display).
        //   P1.4.6: only re-flatten triggers cache invalidation; we do not
        //   rewrite the composition pipeline for all layers.
        connect(m_layerPanel, &layers::LayerPanel::flattenVisibleRequested,
                this, [this]() {
            if (!m_layerStack || m_layerStack->count() <= 1) {
                statusBar()->showMessage(tr("无需拼合 (图层数 ≤ 1)"), 3000);
                return;
            }
            cv::Mat m = m_layerStack->flattenVisible();
            if (m.empty()) {
                statusBar()->showMessage(tr("拼合失败"), 3000);
                return;
            }
            // P1.4.6 simplification: flattenVisible returns the composed
            //   image; here we just reset the base layer.
            //   Full PS-style flatten (clear all layers + set base) requires
            //   rewriting LayerStack
            //   internal state. Deferred to P1.5+.
            m_layerStack->setBaseLayer(m);
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("已合并所有可见图层到 base (仅 base 保留)"), 3000);
        });

        // wire group/ungroup tool buttons
        //   to the P1.5.2 LayerCommand factories (mergeIntoGroup /
        //   flattenGroup). Previously emitted "TBD" statusBar hint.
        // groupRequested: 面板传**选中的图层下标集合** (LYR-05).
        //   以前面板传的是整个栈 [0, count-1], 底图被吞进组里以后
        //   画布退化成 800x600 白底.
        connect(m_layerPanel, &layers::LayerPanel::groupRequested,
                this, [this](QList<int> indices) {
            if (!m_layerStack) return;
            if (indices.size() < 2 || m_layerStack->count() < 2) {
                statusBar()->showMessage(
                    tr("编组失败: 至少需要 2 个图层"), 3000);
                return;
            }
            // indices 由面板给出 (已排序去重)
            if (m_undoStack) {
                m_undoStack->push(layers::LayerCommand::makeMergeIntoGroup(
                    m_layerStack.get(), indices));
            } else {
                m_layerStack->mergeIntoGroup(indices);
            }
            refreshCanvasNow();
            statusBar()->showMessage(
                tr("已编组 %1 个图层 (入撤销栈)").arg(indices.size()), 3000);
        });

        // ungroupRequested: locate the current selection's Group and
        //   flatten it. If selection is not a Group, status-bar hint.
        connect(m_layerPanel, &layers::LayerPanel::ungroupRequested,
                this, [this]() {
            if (!m_layerStack) return;
            const int sel = m_layerStack->selection();
            if (sel < 0 || sel >= m_layerStack->count()) {
                statusBar()->showMessage(
                    tr("解组失败: 请先选中一个组"), 3000);
                return;
            }
            auto l = m_layerStack->at(sel);
            if (!l || l->kind != layers::Layer::Group) {
                statusBar()->showMessage(
                    tr("解组失败: 当前选中不是组 (kind=%1)")
                        .arg(int(l ? l->kind : -1)), 3000);
                return;
            }
            if (m_undoStack) {
                m_undoStack->push(layers::LayerCommand::makeFlattenGroup(
                    m_layerStack.get(), sel));
            } else {
                m_layerStack->flattenGroup(sel);
            }
            refreshCanvasNow();
            statusBar()->showMessage(
                tr("已解组 (入撤销栈)"), 3000);
        });

        // ---- Layer property -------------------------------------------
        // renameRequested: LayerCommand::Rename, stores the old name.
        connect(m_layerPanel, &layers::LayerPanel::renameRequested,
                this, [this](int index, const QString &newName) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const QString oldName = l->name;
            if (!m_layerStack->rename(index, newName)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Rename, index, oldName));
            }
            invalidateCurrentCache();
            statusBar()->showMessage(
                tr("已重命名为 \"%1\"").arg(newName), 3000);
        });

        // setVisibleRequested: LayerCommand::Visible, stores the old bool.
        connect(m_layerPanel, &layers::LayerPanel::setVisibleRequested,
                this, [this](int index, bool visible) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const bool oldVisible = l->visible;
            if (!m_layerStack->setVisible(index, visible)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Visible, index, oldVisible));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                visible ? tr("已显示图层") : tr("已隐藏图层"), 2000);
        });

        // setLockedRequested: LayerCommand::Locked
        connect(m_layerPanel, &layers::LayerPanel::setLockedRequested,
                this, [this](int index, bool locked) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const bool oldLocked = l->locked;
            if (!m_layerStack->setLocked(index, locked)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Locked, index, oldLocked));
            }
            invalidateCurrentCache();
            statusBar()->showMessage(
                locked ? tr("已锁定图层") : tr("已解锁图层"), 2000);
        });

        // setLinkedRequested: LayerCommand::Linked
        connect(m_layerPanel, &layers::LayerPanel::setLinkedRequested,
                this, [this](int index, bool linked) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const bool oldLinked = l->isLinked;
            if (!m_layerStack->setLinked(index, linked)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Linked, index, oldLinked));
            }
            invalidateCurrentCache();
            statusBar()->showMessage(
                linked ? tr("已链接图层") : tr("已取消链接"), 2000);
        });

        // setOpacityRequested: LayerCommand::Opacity, stores the old float.
        connect(m_layerPanel, &layers::LayerPanel::setOpacityRequested,
                this, [this](int index, float opacity) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const float oldOpacity = l->opacity;
            if (!m_layerStack->setOpacity(index, opacity)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Opacity, index, oldOpacity));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("不透明度 → %1%").arg(qRound(opacity * 100.0f)), 2000);
        });

        // setBlendRequested: note the LayerPanel internal signal is named
        //   setBlendRequested
        //   (the task spec uses setBlendModeRequested - same signal).
        //   LayerCommand::Blend
        //   stores the new blend int (m_intVal); undo reads layer.blend
        //   directly to restore.
        connect(m_layerPanel, &layers::LayerPanel::setBlendRequested,
                this, [this](int index, int blendInt) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const int oldBlend = static_cast<int>(l->blend);
            const auto newMode = static_cast<layers::Layer::BlendMode>(blendInt);
            if (!m_layerStack->setBlend(index, newMode)) return;
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerCommand(
                    m_layerStack.get(),
                    layers::LayerCommand::Blend, index, oldBlend));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("混合模式已更改"), 2000);
        });

        // ---- Layer-type specific --------------------------------------
        // setTextRequested: goes through makeSetText, stores the old text.
        connect(m_layerPanel, &layers::LayerPanel::setTextRequested,
                this, [this](int index, const QString &text) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l || l->kind != layers::Layer::Text) return;
            const QString oldText = l->text;
            if (!m_layerStack->setText(index, text)) return;
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeSetText(
                    m_layerStack.get(), index, oldText);
                m_undoStack->push(cmd);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已更新文字内容"), 2000);
        });

        // setTextFontRequested: goes through makeSetTextFont, stores the old
        //   family/size/color.
        connect(m_layerPanel, &layers::LayerPanel::setTextFontRequested,
                this, [this](int index, const QString &family, int size,
                              const QColor &color) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l || l->kind != layers::Layer::Text) return;
            const QString oldFamily = l->fontFamily;
            const int oldSize = l->fontSize;
            const QColor oldColor = l->textColor;
            if (!m_layerStack->setTextFont(index, family, size, color)) return;
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeSetTextFont(
                    m_layerStack.get(), index, oldFamily, oldSize, oldColor);
                m_undoStack->push(cmd);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("文字字体已更新"), 2000);
        });

        // setSmartObjectSourceRequested: P1.4.3 already uses
        //   makeSetSmartObject
        //   (Relink uses the same factory) - reuse the same path.
        connect(m_layerPanel, &layers::LayerPanel::setSmartObjectSourceRequested,
                this, [this](int index, const QString &path, bool embed) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l || l->kind != layers::Layer::SmartObject) return;
            const QString oldPath = l->sourceFilePath;
            const bool oldEmbed = l->sourceEmbedded;
            if (!m_layerStack->setSmartObjectSource(index, path, embed)) {
                statusBar()->showMessage(tr("源文件失败 (源不存在)"), 3000);
                return;
            }
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeSetSmartObject(
                    m_layerStack.get(), index, oldPath, oldEmbed);
                m_undoStack->push(cmd);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("已更新 SmartObject 源: %1").arg(QFileInfo(path).fileName()),
                3000);
        });

        // setAdjustmentTypeRequested: goes through makeSetAdjustmentType.
        connect(m_layerPanel, &layers::LayerPanel::setAdjustmentTypeRequested,
                this, [this](int index, const QString &type) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l || l->kind != layers::Layer::Adjustment) return;
            const QString oldType = l->adjustmentType;
            if (!m_layerStack->setAdjustmentType(index, type)) return;
            if (m_undoStack) {
                auto *cmd = layers::LayerCommand::makeSetAdjustmentType(
                    m_layerStack.get(), index, oldType);
                m_undoStack->push(cmd);
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("调整类型 → %1").arg(type), 2000);
        });

        // setAdjustmentLutResetRequested: resets the LUT to identity.
        //   LayerCommand has no dedicated "ResetLut" factory; we use
        //   makeSetAdjustmentLut
        //   (which stores a before-image snapshot via the P0-3.3 host path).
        //   P1.4.6 simplified path:
        //   write layer.adjustmentLut to identity directly, no undo push
        //   (consistent with Phase 1).
        connect(m_layerPanel, &layers::LayerPanel::setAdjustmentLutResetRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l || l->kind != layers::Layer::Adjustment) return;
            l->adjustmentLut = cv::Mat(256, 1, CV_8U);
            uchar *p = l->adjustmentLut.ptr<uchar>();
            for (int i = 0; i < 256; ++i) p[i] = static_cast<uchar>(i);
            m_layerStack->setAdjustmentLut(index, l->adjustmentLut);
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("调整层 LUT 已复位 (identity)"), 3000);
        });

        // ---- Mask operations ------------------------------------------
        // addMaskRequested: maskPath comes from LayerPanel through QFileDialog
        //   (used by the P1.3.4 mask panel); here we reuse the path to load
        //   a grayscale image as the mask.
        //   The kind param is derived from
        //   LayerPanel::addMaskRequested(int, QString) -
        //   see LayerPanel.h notes: empty QString = go via
        //   addPixelMaskFromSelection,
        //   non-empty = load as a pixel mask. P1.4.6 simplification: always
        //   load by path as a pixel mask,
        //   otherwise build the mask from selection (if selection non-empty,
        //   addPixelMaskFromSelection).
        connect(m_layerPanel, &layers::LayerPanel::addMaskRequested,
                this, [this](int index, const QString &maskPath) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            // The single mask model: undo goes through LayerMaskCommand (a
            // before/after snapshot).
            //   It used to hold the old layerMask + maskEnabled state, while what
            //   actually changed was Layer::mask -- so undo could not restore it.
            const layers::LayerMask beforeMask = l->mask;
            bool ok = false;
            if (maskPath.isEmpty()) {
                //   go via selection
                if (m_selection && !m_selection->mask().isNull()
                    && m_selection->mask().size().width() > 0) {
                    const QImage sm = m_selection->mask();
                    cv::Mat gray(sm.height(), sm.width(), CV_8UC1,
                                 const_cast<uchar*>(sm.bits()),
                                 sm.bytesPerLine());
                    const cv::Mat grayClone = gray.clone();
                    ok = m_layerStack->addPixelMask(index, grayClone);
                }
            } else {
                const QImage qm = QImage(maskPath).convertToFormat(
                    QImage::Format_Grayscale8);
                if (!qm.isNull()) {
                    cv::Mat gray(qm.height(), qm.width(), CV_8UC1,
                                 const_cast<uchar*>(qm.bits()),
                                 qm.bytesPerLine());
                    const cv::Mat grayClone = gray.clone();
                    ok = m_layerStack->addPixelMask(index, grayClone);
                }
            }
            if (!ok) {
                statusBar()->showMessage(
                    tr("加蒙版失败 (空选区/无效图)"), 3000);
                return;
            }
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Add Mask")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已添加蒙版"), 3000);
        });

        // clearMaskRequested: goes through makeClearMask, fully saves the
        //   before state of the mask.
        connect(m_layerPanel, &layers::LayerPanel::clearMaskRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const layers::LayerMask beforeMask = l->mask;
            if (!m_layerStack->clearMaskFull(index)) {
                statusBar()->showMessage(tr("清除蒙版失败"), 3000);
                return;
            }
            if (m_undoStack) {
                m_undoStack->push(new layers::LayerMaskCommand(
                    m_layerStack.get(), index, beforeMask, layers::LayerMask(),
                    QStringLiteral("Clear Mask")));
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(tr("已删除蒙版"), 3000);
        });

        // toggleMaskRequested: goes through makeEnableMask.
        connect(m_layerPanel, &layers::LayerPanel::toggleMaskRequested,
                this, [this](int index, bool enabled) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const layers::LayerMask beforeMask = l->mask;
            if (!m_layerStack->setMaskEnabled(index, enabled)) return;
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Toggle Mask")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                enabled ? tr("蒙版已启用") : tr("蒙版已禁用"), 2000);
        });

        // setMaskInvertRequested: goes through LayerMaskCommand so the
        //   invert is undoable (LYR-41).
        connect(m_layerPanel, &layers::LayerPanel::setMaskInvertRequested,
                this, [this](int index, bool invert) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const layers::LayerMask beforeMask = l->mask;
            if (!m_layerStack->setMaskInvert(index, invert)) return;
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Invert Mask")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                invert ? tr("蒙版已反相") : tr("蒙版取消反相"), 2000);
        });

        // setMaskDensityRequested: goes through LayerMaskCommand so the
        //   density change is undoable (LYR-41).
        connect(m_layerPanel, &layers::LayerPanel::setMaskDensityRequested,
                this, [this](int index, qreal density) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const layers::LayerMask beforeMask = l->mask;
            if (!m_layerStack->setMaskDensity(index, density)) return;
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Mask Density")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("蒙版密度 → %1").arg(density, 0, 'f', 2), 2000);
        });

        // setMaskFeatherRequested
        connect(m_layerPanel, &layers::LayerPanel::setMaskFeatherRequested,
                this, [this](int index, qreal featherPx) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            auto l = m_layerStack->at(index);
            if (!l) return;
            const layers::LayerMask beforeMask = l->mask;
            if (!m_layerStack->setMaskFeather(index, featherPx)) return;
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Mask Feather")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("蒙版羽化 → %1 px").arg(featherPx, 0, 'f', 1), 2000);
        });

        // addVectorMaskRequested: goes through LayerStack::addVectorMask
        //   (empty path is accepted too).
        //   P1.4.6 simplification: only pushes LayerCommand::Add (mask kind /
        //   state cannot be undone, but the UI can redo).
        connect(m_layerPanel, &layers::LayerPanel::addVectorMaskRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            QVector<QPainterPath> paths;   // empty - lets the user draw paths
                                          //   later with PenTool
            if (!m_layerStack->addVectorMask(index, paths)) {
                statusBar()->showMessage(tr("矢量蒙版添加失败"), 3000);
                return;
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("已添加矢量蒙版 (用 PenTool 画路径)"), 3000);
        });

        // addPixelMaskFromSelectionRequested: similar to addMaskRequested
        //   but goes
        //   through the selection path explicitly (LayerPanel calls this
        //   instead of addMaskRequested with empty path).
        connect(m_layerPanel, &layers::LayerPanel::addPixelMaskFromSelectionRequested,
                this, [this](int index) {
            if (!m_layerStack || index < 0 || index >= m_layerStack->count()) return;
            if (!m_selection || m_selection->mask().isNull()
                || m_selection->boundingRect().isEmpty()) {
                statusBar()->showMessage(tr("当前无有效选区"), 3000);
                return;
            }
            const QImage sm = m_selection->mask();
            cv::Mat gray(sm.height(), sm.width(), CV_8UC1,
                         const_cast<uchar*>(sm.bits()),
                         sm.bytesPerLine());
            const cv::Mat grayClone = gray.clone();
            auto l = m_layerStack->at(index);
            const layers::LayerMask beforeMask = l ? l->mask : layers::LayerMask();
            if (!m_layerStack->addPixelMask(index, grayClone)) return;
            if (m_undoStack) {
                const layers::LayerMask *now = m_layerStack->maskAt(index);
                if (now) {
                    m_undoStack->push(new layers::LayerMaskCommand(
                        m_layerStack.get(), index, beforeMask, *now,
                        QStringLiteral("Add Mask")));
                }
            }
            invalidateCurrentCache();
            renderToView();
            statusBar()->showMessage(
                tr("已从选区添加像素蒙版"), 3000);
        });
    }

    emit filePathChanged(m_filePath);
    // PropertiesDock.setImageInfo 接入
    //   之前 loadFile 完全没调 setImageInfo, PropertiesDock 始终显示 "(无)".
    //   现在从 m_filePath (QFileInfo 取后缀得 format, 默认 dpi=72) + img.size 注入.
    // forward via RightPanelDock::setPropertyImageInfo
    //   which routes to VPropertyPage->inner() (PropertiesDock).
    if (m_rightDock) {
        docks::PropertiesDock::ImageInfo info;
        info.filePath = m_filePath;
        info.width    = img.cols;
        info.height   = img.rows;
        const QString suffix = QFileInfo(m_filePath).suffix().toUpper();
        info.format   = suffix.isEmpty() ? QStringLiteral("Unknown") : suffix;
        info.dpi      = 72;  // 默认 dpi, 后续 P0-8 ICC embed 流程可读 EXIF
        m_rightDock->setPropertyImageInfo(info);
    }
    // 补回 renderToView 触发
    //   Stage B 删 m_adjustment->syncApplyCurrentParams(true) 时连带漏了渲染触发
    //     旧: syncApplyCurrentParams -> setCurrentImage -> renderToView
    //     新: 删了中间人, 直接调 renderToView
    //   不调的后果: loadFile 返回 true 但 canvas pixmap 还是空 (用户看到空白)
    renderToView();
    return true;
}

// filePath 防御式实现
//   之前 inline return m_filePath; 在调用方拿到 QString& 跟 m_filePath 同生命周期,
//   m_filePath 字段被踩时调用方 use-after-free
//   修法: .cpp 实现 + QString() 强拷贝隔离, 跟 P0-1.3 ImageWorkspace::filePath 同样策略
QString ImageWindow::filePath() const
{
    return QString(m_filePath);
}

bool ImageWindow::isDirty() const
{
    return m_undoStack && m_undoStack->index() != m_savedIndex;
}

QString ImageWindow::displayTitle() const
{
    // tst_ImageWorker SEGFAULT 真因修复
    //   之前 QFileInfo(m_filePath).fileName() 直接调, m_filePath 在 loadFile 流程中
    //   某处被破坏时 (7 层 QFileInfo::filePath 递归栈顶异常), 崩在 Qt 6.11.1 内部
    //   修法: 业务级 early return — m_filePath 为空 (loadFile 之前) 直接返, 不进 QFileInfo
    //   兜底: QFileInfo 也加 try 风格检查 (QString 内部 d 指针若被踩坏, isEmpty 也会崩,
    //         但 Qt QString 在 debug build 有越界检查, 不至于让 process 直接死)
    QString base;
    if (m_filePath.isEmpty()) {
        base = tr("未命名图像");
    } else {
        // 双重防御: 先验证 m_filePath 内部 d 指针非空, 再构造 QFileInfo
        //   QString::isEmpty 不读 d->data, 只读 d->size (size 为 0 视为空)
        //   真要 d 指针野了, QFileInfo 构造时 Qt 自己会处理 (不会让进程死)
        const QFileInfo fi(m_filePath);
        base = fi.fileName();
        if (base.isEmpty()) base = m_filePath;   // 兜底: fileName() 返空就用原 path
    }
    if (isDirty())
        base.prepend(QLatin1String("*"));
    return base;
}

//  文档级变更: 文档本身变了 (尺寸 / 色彩模式 / 裁切 / 旋转 / 翻转).
//  原图基准和 base layer 一起换, 上层图层保持不动 —— 它们叠在新的底图上.
//  m_current 不在这里写: 它是 layerStack->render() 的缓存, invalidate 之后
//  由渲染器重算, 直接赋值只会造出第二份“真相”.
void ImageWindow::setDocumentImage(const cv::Mat &img)
{
    if (img.empty()) return;
    m_original = img.clone();
    ensureLayerStack();
    // 文档级画布尺寸跟着像素走 (LYR-05):
    //   LayerStack::canvasSize() 不再从 index 0 反推, 裁切 / 旋转 / 翻转
    //   改了尺寸以后必须显式写回去.
    m_layerStack->setCanvasSize(img.size());
    if (m_layerStack->count() > 0) {
        auto base = m_layerStack->baseLayer();
        if (base) base->image = img.clone();
    } else {
        m_layerStack->setBaseLayer(img);
    }
    invalidateCurrentCache();
    refreshAll();
    // sync selection mask size with image size
    if (m_selection) {
        m_selection->setSize(QSize(img.cols, img.rows));
    }
}

//  图层栈是文档像素的容器. 文档一旦有像素就必须有图层栈,
//  否则 m_current (合成缓存) 就没有唯一的来源.
void ImageWindow::ensureLayerStack()
{
    if (m_layerStack) return;
    //   LayerStack 是 QObject, parent = this (生命周期跟随 ImageWindow)
    m_layerStack = std::make_unique<layers::LayerStack>(this);
    // 监听 layer 变化 → invalidate cache
    connect(m_layerStack.get(), &layers::LayerStack::layerChanged,
            this, [this](int){ invalidateCurrentCache(); });
    connect(m_layerStack.get(), &layers::LayerStack::layerAdded,
            this, [this](int){ invalidateCurrentCache(); });
    connect(m_layerStack.get(), &layers::LayerStack::layerRemoved,
            this, [this](int){ invalidateCurrentCache(); });
}

void ImageWindow::refreshAll()
{
    renderToView();
}

// ROOT-CAUSE FIX: filter Apply 预览 — 不放 ImageWindow 成员!
//   调试 1+ 小时定位到的根因: 在 ImageWindow 里加 cv::Mat 字段 (144 字节)
//   会让 tst_ImageWorker::test_routePng 在 ImageWindow() 构造期 segfault.
//   崩溃栈: Ui_ImageWindow::setupUi → QFileInfo::filePath 递归 (QString d-pointer
//   被踩). 加 unique_ptr<cv::Mat> (8 字节) 部分修好了 tst_ImageEditCommand,
//   但 tst_ImageWorker 仍 crash — class layout shift 影响 QString 内部 QStringData
//   共享 / 写时复制路径, 跟 OpenCV 的 cv::Mat 静态初始化顺序敏感.
//   最终 fix: 完全不进 class layout. 预览 cv::Mat 放文件作用域 static registry
//   (ImageWindow* → shared_ptr<cv::Mat>). registry 自动随 ImageWindow 析构清空
//   (weak_ptr 不持 owner). renderToView 每次 O(1) hash 查找.
namespace {
    QMutex s_previewRegistryMutex;
    QHash<ImageWindow*, std::shared_ptr<cv::Mat>> s_previewRegistry;
    std::shared_ptr<cv::Mat> lookupPreview(ImageWindow* w)
    {
        QMutexLocker lock(&s_previewRegistryMutex);
        auto it = s_previewRegistry.find(w);
        if (it == s_previewRegistry.end()) return nullptr;
        if (it.value()->empty()) return nullptr;
        return it.value();
    }
}

// TODO(P8.4): renderToView and invalidateCurrentCache are called 95 times in this file, so one missed pair is a silent stale frame -- see docs/DEBT.md#A-REFRESHDUP
void ImageWindow::renderToView()
{
    //  m_current 来自 layerStack.render 缓存
    if (m_currentDirty) rebuildCurrentCache();
    // 滤镜预览优先 — 如果 registry 里有 m_previewImage 非空, 用它显示
    //   临时预览层不动 m_current, Apply 每次都从 m_current 重新生成
    //   clearPreview (OK/Cancel) 把控制权交回 m_current
    auto preview = lookupPreview(this);
    const cv::Mat* display = preview ? preview.get() : &m_current;
    if (display->empty()) {
        // m_item 搬到 m_canvas
        m_canvas->pixmapItem()->setPixmap(QPixmap());
        return;
    }
    const QImage qimg = ImageProcessor::matToQImage(*display);
    m_canvas->pixmapItem()->setPixmap(QPixmap::fromImage(qimg));
    m_canvas->scene()->setSceneRect(m_canvas->pixmapItem()->pixmap().rect());
    // 关键: 重置变换 + 按当前 m_canvas->zoom() 缩放 (跟原逻辑一致, 保持每次渲染都重设 transform)
    m_canvas->resetTransform();
    m_canvas->scale(m_canvas->zoom(), m_canvas->zoom());
}

//  从 layerStack.render 重算 m_current 缓存
// 大图走 LayerStack::renderAsync 走 background pool
//   小图 (<= 1024x1024) 走 sync render 避免线程切换开销.
//   保留 sync 行为给 loadFile / onStackChanged 等调用方 (它们依赖同步语义)
//
// 设计要点 (一次性到位, P0-2.5 不留补丁):
//   1. 小图同步: m_current = render(); m_currentDirty = false (跟原行为完全一致)
//   2. 大图异步: 分配 myTaskId, m_currentDirty 暂留 true 防止重入
//      → renderAsync 在 background pool 跑完, 切回主线程刷 m_current + 标 dirty=false + renderToView
//      → callback 里再校验 m_currentTaskId, 不等于 myTaskId 直接 return
//   3. m_currentDirty 状态机:
//      - sync 路径: 先 render() 再 false (旧行为, 保证 renderToView 不会重入)
//      - async 路径: 先 false (in-flight 期间), callback 完成时 renderToView()
//        这里用 false 防止 renderToView 在 callback 回来之前被调 (那会调 rebuildCurrentCache 重入)
//   4. m_layerStack == nullptr: 保持原行为, 啥也不做
//   5. m_asyncEnabled == false: 走 sync (用户 setAsyncEnabled(false) 时整体 fallback 同步)
void ImageWindow::rebuildCurrentCache()
{
    if (!m_layerStack) {
        m_currentDirty = false;
        return;
    }

    // P0-2.5: setAsyncEnabled(false) → 全局 fallback 同步
    if (!m_asyncEnabled) {
        m_current = m_layerStack->render();
        m_currentDirty = false;
        return;
    }

    // 小图同步 (避免线程切换开销)
    // 阈值 1024*1024 = 1M 像素; 经验值, 大于这个值 OpenCV 单次 render 耗时 > 50ms
    //   → 走 background pool 后台跑, UI 不卡
    //  阈值按当前文档尺寸算 (base layer 的大小).
    //  m_original 是载入时的尺寸, 裁切/缩放之后就跟文档对不上了.
    int docW = 0;
    int docH = 0;
    if (m_layerStack) {
        if (auto base = m_layerStack->baseLayer()) {
            docW = base->image.cols;
            docH = base->image.rows;
        }
    }
    if (docW <= 0 || docH <= 0) {
        docW = m_current.cols;
        docH = m_current.rows;
    }
    const long long pixelCount = static_cast<long long>(docW)
                                * static_cast<long long>(docH);
    if (pixelCount <= 1024LL * 1024LL) {
        m_current = m_layerStack->render();
        m_currentDirty = false;
        return;
    }

    // 大图异步: 走 LayerStack::renderAsync
    //   1. 分配新 task id, 旧 in-flight task 完成后主线程发现 id 变了, 直接 return
    //   2. m_currentDirty 暂留 true → renderToView 不会被重入 (重入会调 rebuildCurrentCache 再次 submit)
    //   3. callback 里 m_currentDirty = false + renderToView (callback 在主线程)
    if (!m_engine) {
        // 兜底: engine 没了 → 走 sync
        m_current = m_layerStack->render();
        m_currentDirty = false;
        return;
    }
    const uint64_t myTaskId = m_currentTaskId.fetch_add(1) + 1;
    // QPointer 守卫: 窗口销毁后指针自动置 null, 回调直接返回,
    // 不会碰到已经析构的 ImageWindow (关 tab / 关程序时在飞的任务会晚到).
    QPointer<ImageWindow> guard(this);
    m_layerStack->renderAsync(m_engine.get(),
        [guard, myTaskId](cv::Mat result) -> void {
            ImageWindow* self = guard.data();
            if (!self) return;                                          // 窗口已销毁
            if (self->m_currentTaskId.load() != myTaskId) return;        // 已被新 task 取代, 丢弃
            self->m_current = std::move(result);
            self->m_currentDirty = false;
            self->renderToView();
        });
}

//  LayerPanel 切图层
//   当前不直接改渲染 (切 layer 不改像素), 但触发重绘让画布激活层指示器更新
//   也方便 MainWindow 挂钩 (e.g. 切到 Adjustment 时自动展开属性面板)
void ImageWindow::onLayerSelectionChanged(int /*index*/)
{
    // 触发重绘 (画布可能在 active layer 边框/调色板上画指示)
    update();
}

// 滤镜应用 (主菜单 4 action 接真)
//   流程: FilterFactory::createFilter + strategy->apply + push FilterCommand
//   FilterCommand 内部 ctor 时 apply 一次, undo/redo 复用
// pop a standalone adjust dialog via the
//   per-window DialogMediator. dialogId is one of {"Curves","Levels",
//   "B&W","ChannelMixer"}. DialogMediator (P0 leftover 4) self-wires
//   to DialogFactory. dialog->applied is routed to
//   AdjustmentPanel::setStandaloneParams via the dialogCreated signal
//   connection set up in the ctor.
void ImageWindow::showAdjustDialog(const QString& dialogId)
{
    if (!m_dialogMed) return;
    if (m_current.empty()) {
        LOG_WARN("[ImageWindow] showAdjustDialog: no image loaded, id={}",
                 dialogId.toStdString());
        return;
    }
    m_dialogMed->showDialog(dialogId);
}

void ImageWindow::applyFilter(filter::FilterKind kind)
{
    if (m_current.empty()) {
        LOG_WARN("[ImageWindow] applyFilter: no image loaded");
        return;
    }
    if (!m_undoStack) {
        LOG_ERROR("[ImageWindow] applyFilter: m_undoStack null");
        return;
    }
    auto strategy = filter::FilterFactory::createFilter(kind);
    if (!strategy) {
        LOG_ERROR("[ImageWindow] applyFilter: failed to create filter kind={}",
                  static_cast<int>(kind));
        return;
    }
    QString filterName = strategy->name();
    LOG_INFO("[ImageWindow] applyFilter: kind={} ({})", static_cast<int>(kind),
             filterName.toStdString());
    //  滤镜作用在活动图层上 (不是合成图) —— 合成图会把上层图层重复合成
    m_undoStack->push(new filter::FilterCommand(this, kind, filterName));
}

// 滤镜 Apply 实时预览 — FilterDialog::onApplyClicked 调
//   preview cv::Mat 存文件作用域 static registry (不进 ImageWindow class layout,
//   调试 1+ 小时定位到的关键 root cause fix, 见 renderToView 上面注释)
//   关键: 每次 Apply 都从当前 m_current 重新生成, 不在旧预览基础上叠加
void ImageWindow::previewFilter(filter::FilterStrategy* strategy)
{
    if (!strategy) {
        LOG_WARN("[ImageWindow] previewFilter: null strategy");
        return;
    }
    //  预览必须跟最终提交的结果一致: 滤镜作用在活动图层上,
    //  预览就是把该图层换成滤镜结果之后的整张合成图
    cv::Mat* layer = activeLayerImage();
    if (!layer || layer->empty()) {
        LOG_WARN("[ImageWindow] previewFilter: no writable layer");
        return;
    }
    const cv::Mat saved = layer->clone();
    cv::Mat filtered;
    strategy->apply(saved, filtered);
    if (filtered.empty()) {
        LOG_WARN("[ImageWindow] previewFilter: strategy produced empty output");
        return;
    }
    *layer = filtered;
    cv::Mat composite = filtered;
    if (m_layerStack) {
        cv::Mat rendered = m_layerStack->render();
        if (!rendered.empty()) composite = rendered;
    }
    *layer = saved;   // 预览不改文档
    auto mat = std::make_shared<cv::Mat>(composite);
    {
        QMutexLocker lock(&s_previewRegistryMutex);
        s_previewRegistry[this] = mat;
    }
    LOG_DEBUG("[ImageWindow] previewFilter: {}x{} preview ready",
              mat->cols, mat->rows);
    renderToView();
}

void ImageWindow::clearPreview()
{
    bool had = false;
    {
        QMutexLocker lock(&s_previewRegistryMutex);
        auto it = s_previewRegistry.find(this);
        had = (it != s_previewRegistry.end());
        if (had) s_previewRegistry.erase(it);
    }
    if (had) {
        LOG_DEBUG("[ImageWindow] clearPreview");
        renderToView();
    }
}

void ImageWindow::applyFilterWithStrategy(filter::FilterStrategy* strategy, const QString& text)
{
    if (m_current.empty()) {
        LOG_WARN("[ImageWindow] applyFilterWithStrategy: no image loaded");
        return;
    }
    if (!m_undoStack) {
        LOG_ERROR("[ImageWindow] applyFilterWithStrategy: m_undoStack null");
        return;
    }
    if (!strategy) {
        LOG_ERROR("[ImageWindow] applyFilterWithStrategy: null strategy");
        return;
    }
    LOG_INFO("[ImageWindow] applyFilterWithStrategy: text={}", text.toStdString());
    // 走新 ctor (strategy-based), 用 dialog 提供的 strategy (含 slider 当前值)
    m_undoStack->push(new filter::FilterCommand(this, strategy, text));
}

// ---------------- 撤销栈 ----------------

void ImageWindow::markSaved()
{
    m_savedIndex = m_undoStack->index();
    emit dirtyChanged(false);
}

void ImageWindow::onStackChanged()
{
    emit dirtyChanged(isDirty());
}

// 撤销 ImageAdjustmentPanel (5 toggle + 10 slider 旧 UI)
//   UI 元素 (groupAdjust 块 + 5 toggle + 10 slider) 全部从 imagewindow.ui 删除
//   UI 操作入口没了, ParamSet 路径变成 no-op. ImageEditCommand::undo/redo 的
//   m_useImage=false 分支还会调到本方法, 保留 API (tests 用 ParamSet 类型) 但不做事.
//   等 P1 阶段把旧 ParamSet 语义重新设计 (5 tab UI 用 curve / level / hsl 各自的 cmd)
void ImageWindow::applyParams(const ImageEditCommand::ParamSet &p, bool repaint)
{
    (void)p;
    (void)repaint;
}

void ImageWindow::onUndo()
{
    if (m_undoStack) m_undoStack->undo();
    // applyParams 已经把 m_params 更新成 before
}

void ImageWindow::onRedo()
{
    if (m_undoStack) m_undoStack->redo();
}

// 当前活动图层 idx. 没图 -> -1
int ImageWindow::currentLayerIndex() const
{
    if (!m_layerStack) return -1;
    return m_layerStack->selection();
}

// 滤镜 → 转换为智能滤镜 实现
//   1) 拿当前图层 idx + 当前 cached image (m_current) 作为 imageBefore
//   2) 把图层 idx 转 SmartObject (in-place convertToSmartObject)
//   3) append 目标 filter kind 作为 SmartFilter sub-layer
//   两步都进 undoStack, undo 可回滚 (P7.3 简化: convert 命令走 before 镜像).
//   返回 true 表示两道都成功.
bool ImageWindow::convertCurrentLayerToSmartObjectWithFilter(filter::FilterKind kind)
{
    if (!m_layerStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    auto* stack = m_undoStack;
    if (!stack) return false;

    namespace lyr = layers;
    // 1) 备份原图层 (Layer 内自带 cv::Mat clone for undo)
    const cv::Mat imageBefore = m_current.clone();
    lyr::LayerPtr beforeLayerPtr = m_layerStack->at(idx);
    if (!beforeLayerPtr) return false;
    const lyr::Layer beforeLayerCopy = *beforeLayerPtr;   // copy by value

    // 2) Convert Bitmap -> SmartObject (in-place)
    if (!m_layerStack->convertToSmartObject(idx, /*embed=*/true)) {
        LOG_WARN("[ImageWindow] convertToSmartObject failed at idx={}", idx);
        return false;
    }
    // P1.4.x 已有: convertToSmartObject undo 回滚命令
    if (auto* convCmd = lyr::LayerCommand::makeConvertToSmartObject(
            m_layerStack.get(), idx, beforeLayerCopy)) {
        stack->push(convCmd);
    }

    // 3) append SmartFilter (sub-layer 接到 idx 上, imageBefore 用于 chain re-render)
    const QString filterType = QString::fromUtf8(filter::filterName(kind));
    const int newIdx = m_layerStack->appendSmartFilter(idx, filterType, /*strength=*/1.0, imageBefore);
    if (newIdx < 0) {
        LOG_WARN("[ImageWindow] appendSmartFilter failed at idx={}, kind={}", idx, filterType.toStdString());
        return false;
    }
    if (auto* sfCmd = lyr::LayerCommand::makeAppendSmartFilter(
            m_layerStack.get(), newIdx, filterType, /*oldStrength=*/1.0)) {
        stack->push(sfCmd);
    }

    rebuildCurrentCache();   // 缓存层
    renderToView();
    return true;
}

// 图层 → 蒙版 submenu — 4 helper + 1 query
//   内部已有对应 lambda (setMaskEnabled/clearMaskFull/setMaskInvert 等, ~1715 行)
//   这里暴露成 public method 给 MainWindow 主菜单 submenu 调用.
//   所有 helper 失败都返 false (无 stack / 无活动图层 / 该图层无 mask).
bool ImageWindow::hasLayerMask() const
{
    if (!m_layerStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    const layers::LayerMask* m = m_layerStack->maskAt(idx);
    return m && m->kind != layers::LayerMask::None;
}

bool ImageWindow::toggleLayerMask()
{
    if (!m_layerStack || !m_undoStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    const layers::LayerMask* m = m_layerStack->maskAt(idx);
    if (!m || m->kind == layers::LayerMask::None) return false;
    const bool newEnabled = !m->enabled;
    if (!m_layerStack->setMaskEnabled(idx, newEnabled)) return false;
    invalidateCurrentCache();
    renderToView();
    statusBar()->showMessage(
        newEnabled ? tr("蒙版已启用") : tr("蒙版已禁用"), 2000);
    return true;
}

bool ImageWindow::removeLayerMask()
{
    if (!m_layerStack || !m_undoStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    if (!m_layerStack->clearMaskFull(idx)) return false;
    invalidateCurrentCache();
    renderToView();
    statusBar()->showMessage(tr("已删除蒙版"), 2000);
    return true;
}

bool ImageWindow::invertLayerMask()
{
    if (!m_layerStack || !m_undoStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    const layers::LayerMask* m = m_layerStack->maskAt(idx);
    if (!m || m->kind == layers::LayerMask::None) return false;
    const bool newInvert = !m->invert;
    if (!m_layerStack->setMaskInvert(idx, newInvert)) return false;
    invalidateCurrentCache();
    renderToView();
    statusBar()->showMessage(
        newInvert ? tr("蒙版已反相") : tr("蒙版取消反相"), 2000);
    return true;
}

bool ImageWindow::applyLayerMask()
{
    if (!m_layerStack || !m_undoStack) return false;
    const int idx = currentLayerIndex();
    if (idx < 0) return false;
    const layers::LayerMask* m = m_layerStack->maskAt(idx);
    if (!m || m->kind == layers::LayerMask::None) return false;

    // P7.4 简化 applyLayerMask:
    //   对 Pixel mask: 遍历 layer 图层 + mask 像素, alpha=0 处像素置 0 (黑);
    //     然后 clearMaskFull. 把 mask 烧进图层本身.
    //   对 Vector mask: placeholder (需 renderer 重置 + 加剪贴算法).
    if (m->kind == layers::LayerMask::Pixel) {
        layers::LayerPtr layerPtr = m_layerStack->at(idx);
        if (!layerPtr || layerPtr->image.empty()) return false;
        cv::Mat img = layerPtr->image.clone();
        const cv::Mat& mask = m->pixel;
        if (img.size() != mask.size()) return false;
        const int ch = img.channels();
        for (int y = 0; y < img.rows; ++y) {
            if (ch == 1) {
                const uchar* mp = mask.ptr<uchar>(y);
                uchar* ip = img.ptr<uchar>(y);
                for (int x = 0; x < img.cols; ++x) {
                    if (mp[x] < 128) ip[x] = 0;   // 黑
                }
            } else {
                const uchar* mp = mask.ptr<uchar>(y);
                uchar* ip = img.ptr<uchar>(y);
                for (int x = 0; x < img.cols; ++x) {
                    if (mp[x] < 128) {
                        ip[x * 3 + 0] = 0;
                        ip[x * 3 + 1] = 0;
                        ip[x * 3 + 2] = 0;
                    }
                }
            }
        }
        layerPtr->image = img;       // 烧进图层
        m_layerStack->clearMaskFull(idx);   // 删 mask
        invalidateCurrentCache();
        renderToView();
        statusBar()->showMessage(tr("蒙版已应用到图层"), 3000);
        return true;
    }
    // Vector mask 暂未实装
    statusBar()->showMessage(tr("矢量蒙版应用暂未实装"), 2000);
    return false;
}

// 4 个空 slot 已删, 由 MosaicOptionPanel / TextOptionPanel
//   自己接管信号 (panel 直接 connect MosaicTool / TextOverlayController).
//   onMosaicSizeChanged / onMosaicModeToggled / onTextSizeChanged /
//   onTextColorClicked / onTextFontChanged 全在 imagewindow.h/.cpp 删除.
//   buildActions 里对应的 4 个 connect 也删. 这里保留 no-op stub 防其他
//   派生类还在用 (虽然目前没有 ImageWindow 派生类, 但 ImageEditCommand friend).
void ImageWindow::onMosaicSizeChanged(int v)
{
    // MosaicOptionPanel 接管: slider valueChanged -> MosaicOptionPanel::onSizeSliderChanged
    //   -> m_mosaicTool->setSize. ImageWindow 不用再管.
    (void)v;
}

void ImageWindow::onMosaicModeToggled(bool on)
{
    // MosaicOptionPanel 接管: btnMosaicMode toggled -> MosaicOptionPanel::onModeButtonToggled
    //   -> m_mosaicTool->setEnabled.
    (void)on;
}

// =============================================================
// 文字 item 管理 (主流做法: 文字作为 scene item, 永远不烧图)
//   - createTextItem(scenePos): 创建新 item, 入栈, 进入编辑
//   - registerTextItem / unregisterTextItem: 维护 m_textItems 列表
//   - 写盘/导出时由 ImageIOController 走 TextOverlayController::renderComposite
//     做一次性合成, 文档像素和撤销栈都不动
// =============================================================

// 连接文字 item 的信号 (editingFinished + transformFinished) - 撤销栈用
//   抽成 helper, 让 createTextItem 和 Add.redo 重建 item 时都调, 避免 redo 后的 item 没 connect
//   关键: lambda 用 QPointer 捕获, 防止 item 被 delete 后 dangling raw ptr
//   委托给 m_textCtrl (TextOverlayController)
void ImageWindow::connectTextItemSignals(GraphicsTextItem *item)
{
    if (m_textCtrl) m_textCtrl->connectItemSignals(item);
}

// 委托给 m_textCtrl
//   字体/字号从 TextOptionPanel 同步过来 (createTextItem
//   用 m_textFont/m_textSize, TextOptionPanel 在 font/size 变化时调
//   setTextFont/setTextSize. 这里只调 createTextItem 即可.
GraphicsTextItem *ImageWindow::createTextItem(const QPointF &scenePos)
{
    if (!m_textCtrl) return nullptr;
    return m_textCtrl->createTextItem(scenePos);
}

QList<GraphicsTextItem*> ImageWindow::textItems() const
{
    return m_textCtrl ? m_textCtrl->textItems() : QList<GraphicsTextItem*>();
}

void ImageWindow::registerTextItem(GraphicsTextItem *item)
{
    if (m_textCtrl) m_textCtrl->registerTextItem(item);
}

void ImageWindow::unregisterTextItem(GraphicsTextItem *item)
{
    if (m_textCtrl) m_textCtrl->unregisterTextItem(item);
}

// 字体变化 -> 应用到 m_currentTextItem
//   委托给 m_textCtrl
// TextOptionPanel 接管字体/字号/颜色变化, 这里 no-op
void ImageWindow::onTextFontChanged()
{
    // TextOptionPanel 接管: comboTextFont currentFontChanged -> onFontComboChanged
    //   -> m_textCtrl->setTextFont + applyStyleToCurrent
}

// 字号变化 -> 应用到 m_currentTextItem
//   委托给 m_textCtrl
// TextOptionPanel 接管字号变化, 这里 no-op
void ImageWindow::onTextSizeChanged(int v)
{
    // TextOptionPanel 接管: spinTextSize valueChanged -> onSizeSpinChanged
    //   -> m_textCtrl->setTextSize + applyStyleToCurrent
    (void)v;
}

// 文字颜色按钮 -> 应用到 m_currentTextItem
//   调 QColorDialog 拿颜色, 委托给 m_textCtrl
// TextOptionPanel 接管颜色 button + dialog, 这里 no-op
void ImageWindow::onTextColorClicked()
{
    // TextOptionPanel 接管: btnTextColor clicked -> onColorClicked -> QColorDialog
    //   -> m_textCtrl->setTextColor. 这里不再重复弹 dialog.
}

// eventFilter 集中处理: 文字输入框 Escape 取消 + viewport 鼠标滚轮拖动涂抹
// (实现见下面)

// ---------------- 工具栏 ----------------

void ImageWindow::buildActions()
{
    // 4 zoom actions (actZoomIn / actZoomOut / actFit / actResetZoom)
    //   removed from imagewindow.ui toolbar (P7.6.x UI layout fix: 4 zoom buttons
    //   不应该出现在中央画布上方 — PS 没有这些. MainWindow 自己拥有 m_actZoomIn/ZoomOut
    //   并 forward 到 active ImageWindow, 这里不再需要 connect).
    //
    // MosaicOptionPanel + TextOptionPanel connect 全部移到各自 panel
    //   内部, buildActions 不再管 imagewindow.ui 的空 group
}

// ---------------- 缩放 ----------------
// 薄包装, 实际逻辑在 m_canvas (ImageCanvas) 组件里

void ImageWindow::onZoomIn()    { m_canvas->onZoomIn(); }
void ImageWindow::onZoomOut()   { m_canvas->onZoomOut(); }
void ImageWindow::onResetZoom() { m_canvas->onResetZoom(); }
void ImageWindow::onFitWindow() { m_canvas->onFitWindow(); }

// ---------------- P5.5+ AppBar (Quick Mask / Screen Mode / Capture) ----------------

void ImageWindow::onQuickMaskToggled(bool on)
{
    // Quick Mask toggle -> switch into MaskBrush (ToolId::MaskBrush) and
    //   remember the previous tool to restore on toggle-off. PS behavior.
    if (!m_ctx) return;
    if (on && !m_quickMaskActive) {
        m_quickMaskSavedToolId = m_ctx->currentToolId();
        m_quickMaskActive = true;
        if (m_toolMed) {
            m_toolMed->switchTool(mediators::ToolId::MaskBrush);
        }
        statusBar()->showMessage(tr("快速蒙版: 启用 (Mask Brush active)"), 4000);
    } else if (!on && m_quickMaskActive) {
        m_quickMaskActive = false;
        if (m_toolMed && m_quickMaskSavedToolId != mediators::ToolId::None) {
            m_toolMed->switchTool(m_quickMaskSavedToolId);
        }
        m_quickMaskSavedToolId = mediators::ToolId::None;
        statusBar()->showMessage(tr("快速蒙版: 关闭"), 2000);
    }
    // keep AppBar btn + ChannelPathPanel btn + tool state in
    //   lockstep so any of the three sources the user clicks drives the same
    //   composite mask mode toggle.
    syncQuickMaskUi(on);
}

void ImageWindow::onChannelPathQuickMaskToggled(bool on)
{
    // ChannelPathPanel's "快速蒙版 (Q)" button emits the same logic as the
    //   AppBar button — funnel through the unified handler.
    onQuickMaskToggled(on);
}

void ImageWindow::syncQuickMaskUi(bool on)
{
    // AppBar 在 MainWindow 顶层, ImageWindow 不再持有 m_appBar
    //   通过 quickMaskUiSync 信号通知 MainWindow, MainWindow 接到 AppBar 的 btnQuickMask.
    //   ChannelPathPanel 仍在 m_rightDock (ImageWindow 内的右侧 dock) 内, 保留同步.
    emit quickMaskUiSync(on);
    if (m_rightDock) {
        // forward via setChannelPathQuickMask
        m_rightDock->setChannelPathQuickMask(on);
    }
}

void ImageWindow::onScreenModeSelected(int mode)
{
    applyScreenMode(mode);
}

void ImageWindow::applyScreenMode(int mode)
{
    // 0 = Standard, 1 = Full Screen, 2 = Full Screen with Menu Bar.
    if (mode == tools::AppBar::ScreenStandard) {
        showNormal();
        if (menuBar()) menuBar()->show();
        if (statusBar()) statusBar()->show();
        // restore left + right dock visibility from settings (default visible).
    } else if (mode == tools::AppBar::ScreenFullScreen) {
        if (menuBar()) menuBar()->hide();
        if (statusBar()) statusBar()->hide();
        showFullScreen();
    } else if (mode == tools::AppBar::ScreenFullScreenMenu) {
        if (menuBar()) menuBar()->show();
        if (statusBar()) statusBar()->hide();
        showFullScreen();
    }
}

void ImageWindow::onCaptureRequested(int kind)
{
    // 0 = Selected Region (use current selection bbox), 1 = This Window,
    //   2 = Full Screen (primary screen).
    QPixmap pix;
    QString suffix;

    auto* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        statusBar()->showMessage(tr("截图失败: 无 primary screen"), 4000);
        return;
    }

    if (kind == tools::AppBar::CaptureSelected) {
        if (!m_selection) {
            statusBar()->showMessage(tr("截图失败: 没有选区 (先 RectSelect)"), 4000);
            return;
        }
        QRectF bbox = m_selection->boundingRect();
        if (bbox.isEmpty()) {
            statusBar()->showMessage(tr("截图失败: 选区为空"), 4000);
            return;
        }
        pix = primaryScreen->grabWindow(winId(),
                                        int(bbox.x()), int(bbox.y()),
                                        int(bbox.width()), int(bbox.height()));
        suffix = QString("selected");
    } else if (kind == tools::AppBar::CaptureWindow) {
        pix = primaryScreen->grabWindow(winId());
        suffix = QString("window");
    } else {
        pix = primaryScreen->grabWindow(0);
        suffix = QString("screen");
    }

    if (pix.isNull()) {
        statusBar()->showMessage(tr("截图失败: 抓图为空"), 4000);
        return;
    }

    QDir pictures(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (!pictures.exists()) pictures.mkpath(QString("."));
    const QString stamp = QDateTime::currentDateTime().toString(QString("yyyyMMdd-HHmmss"));
    const QString filename = pictures.filePath(
        QString("vistella-capture-%1-%2.png").arg(suffix, stamp));
    if (pix.save(filename, "PNG")) {
        statusBar()->showMessage(tr("已保存: %1").arg(filename), 6000);
    } else {
        statusBar()->showMessage(tr("保存失败: %1").arg(filename), 4000);
    }
}

// RightPanelDock connectors

void ImageWindow::onForegroundColorChanged(const QColor& c)
{
    // P6: Color dock foreground change -> push into the active painting tool.
    //   Brush has setPaintColor(); dynamic_cast is fine because only Brush /
    //   Pencil / similar paint tools carry m_paintColor. Move / Crop / Text
    //   etc. ignore foreground color (matches PS behavior).
    if (!m_ctx) return;
    if (auto* brush = dynamic_cast<tools::Brush*>(m_ctx->currentState())) {
        brush->setPaintColor(c);
    }
}

void ImageWindow::onSaveSelectionRequested()
{
    // Snapshot current SelectionModel bounding rect into a 1-channel mask
    //   sized to the current image. Persists on m_pSavedSelection until
    //   the user saves again or closes the window.
    if (!m_selection) {
        statusBar()->showMessage(tr("保存选区失败: 没有选区模型"), 3000);
        return;
    }
    QRect bbox = m_selection->boundingRect();
    if (bbox.isEmpty()) {
        statusBar()->showMessage(tr("保存选区失败: 选区为空"), 3000);
        return;
    }
    if (m_current.empty()) {
        statusBar()->showMessage(tr("保存选区失败: 没有加载图片"), 3000);
        return;
    }
    cv::Mat mask = cv::Mat::zeros(m_current.rows, m_current.cols, CV_8UC1);
    const int x0 = std::max(0, bbox.left());
    const int y0 = std::max(0, bbox.top());
    const int w  = std::max(1, std::min(m_current.cols - x0, bbox.width()));
    const int h  = std::max(1, std::min(m_current.rows - y0, bbox.height()));
    cv::rectangle(mask, cv::Point(x0, y0), cv::Point(x0 + w, y0 + h),
                  cv::Scalar(255), cv::FILLED);
    m_pSavedSelection = std::make_shared<cv::Mat>(std::move(mask));
    statusBar()->showMessage(tr("选区已保存 (%1x%2)").arg(w).arg(h), 3000);
}

void ImageWindow::onLoadSelectionRequested()
{
    if (!m_selection) return;
    if (!m_pSavedSelection || m_pSavedSelection->empty()) {
        statusBar()->showMessage(tr("载入选区失败: 没有保存的选区"), 3000);
        return;
    }
    cv::Mat m = *m_pSavedSelection;
    if (m.channels() != 1 || m.depth() != CV_8U) {
        statusBar()->showMessage(tr("载入选区失败: 保存格式不对"), 3000);
        return;
    }
    // Find bounding rect of non-zero pixels.
    cv::Rect bbox = cv::boundingRect(m);
    if (bbox.area() <= 0) {
        statusBar()->showMessage(tr("载入选区失败: 保存为空"), 3000);
        return;
    }
    // Build a QImage (Format_Alpha8, 1 channel) from the saved mask and
    //   push it into SelectionModel via setMask(Replace).
    QImage qimg(m.data, m.cols, m.rows, m.step, QImage::Format_Alpha8);
    m_selection->setSize(QSize(m.cols, m.rows));
    m_selection->setMask(qimg, selection::SelectionModel::Mode::Replace);
    statusBar()->showMessage(tr("选区已载入 (%1x%2)").arg(bbox.width).arg(bbox.height), 3000);
}



void ImageWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Control) m_ctrlDown = true;
    if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Z) { onUndo(); return; }
    if ((e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && e->key() == Qt::Key_Z) ||
        (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Y)) { onRedo(); return; }
    QMainWindow::keyPressEvent(e);
}
void ImageWindow::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Control) m_ctrlDown = false;
    QMainWindow::keyReleaseEvent(e);
}

bool ImageWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 关键 debug: 写所有 event 到文件
    static QFile efLog("D:/vistella_events.log");
    static bool efOpened = false;
    if (!efOpened) { efLog.open(QIODevice::Append | QIODevice::Text); efOpened = true; }
    QString typeName;
    switch (event->type()) {
        case QEvent::MouseButtonPress:    typeName = "MouseButtonPress"; break;
        case QEvent::MouseButtonRelease:  typeName = "MouseButtonRelease"; break;
        case QEvent::MouseMove:           typeName = "MouseMove"; break;
        case QEvent::MouseButtonDblClick: typeName = "MouseButtonDblClick"; break;
        default: return QMainWindow::eventFilter(watched, event);
    }
    auto *me = static_cast<QMouseEvent*>(event);
    // ui->graphicsView -> m_canvas (画布搬到 ImageCanvas 组件)
    QPointF sp = m_canvas->mapToScene(me->pos());
    QGraphicsItem *hit = m_canvas->scene()->itemAt(sp, m_canvas->transform());
    QString hitName = "null";
    if (hit) {
        if (qgraphicsitem_cast<GraphicsTextItem*>(hit)) hitName = "GraphicsTextItem";
        else hitName = "other";
    }
    efLog.write(QString("[EVF] %1 pos=(%2,%3) scene=(%4,%5) hit=%6 m_mosaicMode=%7\n")
                .arg(typeName).arg(me->pos().x()).arg(me->pos().y())
                .arg(sp.x()).arg(sp.y()).arg(hitName)
                .arg(m_mosaicTool ? m_mosaicTool->isEnabled() : false).toUtf8());
    efLog.flush();

    // 注: 旧版本的"m_textEdit Escape 取消" 已废弃 - QGraphicsTextItem 自带 focusOut 处理
    // (GraphicsTextItem::focusOutEvent 自动调 endEditing)

    // eventFilter 现在装在 m_canvas->viewport (原 ui->graphicsView->viewport)
    if (watched == m_canvas->viewport()) {
        auto scenePt = [this](const QMouseEvent *me) -> QPointF {
            return m_canvas->mapToScene(me->pos());
        };
        auto imgPt = [this](const QMouseEvent *me) -> QPoint {
            const QPointF sp = m_canvas->mapToScene(me->pos());
            return QPoint(int(sp.x()), int(sp.y()));
        };

        // 滚轮缩放
        // Ctrl+wheel 缩放逻辑搬到 m_canvas->wheelEvent
        //   这里不再处理 Wheel, 让事件自然传到 m_canvas::wheelEvent
        // 鼠标移动 - 笔刷光标位置 + 涂抹
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QPointF sp = scenePt(me);

            // Plan B: handle 拖动中 -> 路由到 item (绕开 Qt itemAt)
            //   m_handleDragItem 搬到 m_textCtrl
            if (m_textCtrl && m_textCtrl->handleDragItem()) {
                m_textCtrl->handleDragItem()->continueHandleDragFromView(sp);
                return true;
            }

            // 笔刷光标跟着鼠标 (仅在涂抹模式下显示)
            if (m_brushCursor) {
                m_brushCursor->setPos(sp);
                m_brushCursor->setVisible(m_mosaicTool && m_mosaicTool->isEnabled());
            }
            // 涂抹: 必须左键按下 (用 me->buttons() 实时判断, 不依赖 m_mosaicDragging 状态)
            //   委托给 m_mosaicTool
            if (m_mosaicTool && m_mosaicTool->isEnabled() && !m_current.empty()
                && (me->buttons() & Qt::LeftButton)) {
                m_mosaicTool->onSceneDragMove(sp);
            }
            // forward to current ToolState (default no-op, harmless)
            //   放在最后: handle drag / mosaic 优先级高于 tool (已经 return true 的不进入这里)
            if (m_ctx) m_ctx->onMouseMove(me, sp);
        }
        // 鼠标按下 - 左键: 涂抹模式按下立即在点击处应用一次, 之后 MouseMove 继续 (松开会自动停)
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QPointF sp = scenePt(me);
                // 优先**主动** hitTest handle (Plan B - 绕过 Qt itemAt)
                //   原因: handles 是 paint() 画出来的, 不在 scene item 树里, Qt 的 itemAt/shape()
                //         命中不可靠 (m_brushCursor/image pixmap 可能遮挡, shape cache 等).
                //   主流商业软件 (Photoshop/Sketch) 都在 view 层主动遍历 item 做 hitTest.
                // 关键: handle 拖动 / 单击空白 unselect 始终可用 (不受文字模式控制, 文字模式已经废除)
                {
                    GraphicsTextItem::Handle hh = GraphicsTextItem::None;
                    // m_textItems -> m_textCtrl->textItems
                    const auto items = m_textCtrl ? m_textCtrl->textItems() : QList<GraphicsTextItem*>{};
                    GraphicsTextItem *hit = GraphicsTextItem::hitTestHandle(items, sp, &hh);
                    if (hit) {
                        hit->beginHandleDragFromView(hh, sp);
                        if (m_textCtrl) m_textCtrl->setHandleDragItem(hit);
                        if (m_brushCursor) m_brushCursor->setVisible(false);
                        return true;  // 拦截, 完全自己处理 (不走 graphicsView pan)
                    }
                }
                // 点击空白处 → unselect 全部文字 item (隐藏手柄, 主流做法)
                // 之前 handles 一直显示, user 看着不舒服
                // 同时: 如果有正在编辑的 item, 退出编辑 (单击其他区域完成编辑)
                {
                    QGraphicsItem *hit = m_canvas->scene()->itemAt(sp, m_canvas->transform());
                    // 关键: itemAt 返回最上面的 item, 可能是 m_rotateHandle/m_handles child
                    // 向上找直到 GraphicsTextItem 父
                    while (hit && qgraphicsitem_cast<GraphicsTextItem*>(hit) == nullptr) {
                        hit = hit->parentItem();
                    }
                    auto *ti = qgraphicsitem_cast<GraphicsTextItem*>(hit);
                    if (!ti) {
                        // m_textItems / m_currentTextItem -> m_textCtrl
                        if (m_textCtrl) {
                            for (auto *item : m_textCtrl->textItems()) {
                                if (item) item->setSelected2(false);
                            }
                            auto* cur = m_textCtrl->current();
                            if (cur) {
                                cur->endEditing();
                                m_textCtrl->setCurrent(nullptr);
                            }
                        }
                    }
                    // 注意: 这里不 return, 让 graphicsView 正常处理 (例如拖动 view)
                }
                // 涂抹模式: 委托给 m_mosaicTool (snapshot + 立即应用一次)
                //   整体搬过去
                if (m_mosaicTool && m_mosaicTool->isEnabled() && !m_current.empty()) {
                    m_mosaicTool->onSceneClicked(sp);
                    return true;  // 拦截, 不让 graphicsView 启动拖动
                }
                // 文字模式 + 普通模式 单击 -> 忽略 (让 graphicsView 处理 pan)
                // forward to current ToolState (放在左键分支末尾, 让 graphicsView 仍能 pan)
                if (m_ctx) m_ctx->onMousePress(me, sp);
            } else if (me->button() == Qt::RightButton) {
                // 右键: 如果击中文字 item 则删除, 否则清空当前编辑
                // 关键: 始终可用 (不依赖 m_textMode, 文字模式已废除)
                {
                    const QPointF sp = scenePt(me);
                    QGraphicsItem *hit = m_canvas->scene()->itemAt(sp, m_canvas->transform());
                    // 向上找直到 GraphicsTextItem 父 (itemAt 可能返回 child handle)
                    while (hit && qgraphicsitem_cast<GraphicsTextItem*>(hit) == nullptr) {
                        hit = hit->parentItem();
                    }
                    auto *ti = qgraphicsitem_cast<GraphicsTextItem*>(hit);
                    if (ti) {
                        // 删除击中的文字 item, 入 Remove 栈
                        //   TextItemCommand::undo 会调 m_textCtrl->unregisterTextItem
                        m_undoStack->push(new TextItemCommand(
                            this, ti, TextItemCommand::Remove, ti->serializedText()));
                    } else if (m_textCtrl && m_textCtrl->current()) {
                        // 没击中, 但有正在编辑的 item -> 退出编辑
                        m_textCtrl->current()->endEditing();
                        m_textCtrl->setCurrent(nullptr);
                    }
                }
                return true;
            }
        }
        // 鼠标双击 - 主流做法: 双击图片空白/已有文字直接进入文字编辑
        //   - 双击空白: 创建新文字 item 并进入编辑
        //   - 双击已有文字 item: 切到该 item 进入编辑
        //   - 马赛克模式: 跳过 (避免涂抹和文字输入冲突)
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton
                && !(m_mosaicTool && m_mosaicTool->isEnabled())) {
                const QPointF sp = scenePt(me);
                // 命中已有文字 item?
                QGraphicsItem *hit = m_canvas->scene()->itemAt(sp, m_canvas->transform());
                // 向上找直到 GraphicsTextItem 父
                while (hit && qgraphicsitem_cast<GraphicsTextItem*>(hit) == nullptr) {
                    hit = hit->parentItem();
                }
                auto *ti = qgraphicsitem_cast<GraphicsTextItem*>(hit);
                if (ti) {
                    // GraphicsTextItem::mouseDoubleClickEvent 自己会处理, 但我们这里也走 startEditing
                    //   (因为 m_blockDoubleClickEdit=false, 它会 startEditing)
                    //   双重保险: 即使 GraphicsTextItem::mouseDoubleClickEvent 没触发 (eventFilter 拦截),
                    //   我们也直接 startEditing
                    if (m_textCtrl) {
                        auto* cur = m_textCtrl->current();
                        if (cur && cur != ti) {
                            cur->endEditing();
                        }
                        m_textCtrl->setCurrent(ti);
                    }
                    ti->startEditing();
                } else {
                    // 空白处: 创建新文字 item (createTextItem 内部会退出当前编辑)
                    createTextItem(sp);
                }
                return true;
            }
        }
        // 鼠标松开 - 涂抹结束, push 图像快照 command (支持 Ctrl+Z 撤销)
        //   涂抹相关委托给 m_mosaicTool
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            // Plan B: handle 拖动释放 (绕开 Qt itemAt)
            if (me->button() == Qt::LeftButton && m_textCtrl && m_textCtrl->handleDragItem()) {
                const QPointF sp = scenePt(me);
                m_textCtrl->handleDragItem()->endHandleDragFromView(sp);
                m_textCtrl->setHandleDragItem(nullptr);
                return true;
            }
            if (me->button() == Qt::LeftButton && m_mosaicTool && m_mosaicTool->isEnabled()) {
                m_mosaicTool->onSceneDragEnd();
                return true;
            }
            // forward to current ToolState (default no-op, harmless)
            //   放在最后: handle drag / mosaic 已经 return true 的不进入这里
            if (m_ctx) m_ctx->onMouseRelease(me, m_canvas->mapToScene(me->pos()));
        }
        // 鼠标离开 viewport - 隐藏笔刷光标
        if (event->type() == QEvent::Leave) {
            if (m_brushCursor) m_brushCursor->setVisible(false);
        }
        if (event->type() == QEvent::Enter) {
            // 关键修复: 只在涂抹模式才显示笔刷圆圈, 不然用户感觉"鼠标全程马赛克"
            if (m_brushCursor) m_brushCursor->setVisible(m_mosaicTool && m_mosaicTool->isEnabled());
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------- 打开 / 存 ----------------
// 4 个 IO slot (onOpen / onSave / onSaveAs / onClose) 全部搬到
//   src/media/imagewindow/ImageIOController.cpp 实现, 这里不再保留.

// 字体/字号/颜色变化应用到当前选中/正在编辑的 item
// (已拆成 onTextFontChanged / onTextSizeChanged / onTextColorClicked 三个函数)
void ImageWindow::applyTextStyleToCurrent() {}

// 注入 WorkspaceMediator
void ImageWindow::attachWorkspaceMed(mediators::WorkspaceMediator* wsMed)
{
    if (m_rightDock) {
        m_rightDock->attach(wsMed);
    }
}

// 右侧 panel 显隐 (给 mainwindow 视图菜单 toggle 用)
void ImageWindow::setRightPanelVisible(bool visible)
{
    if (m_rightDockContainer) {
        m_rightDockContainer->setVisible(visible);
    }
}

bool ImageWindow::isRightPanelVisible() const
{
    return m_rightDockContainer && m_rightDockContainer->isVisible();
}

// ============================================================================
// 图像变换 6 槽 + applyTransformImage
//   6 menu action (MainWindow::onXxx) 通过 ImageWindow::onXxx 调到下面
//   实现: cv::flip / cv::rotate / cv::warpAffine + 推 TransformCommand 到 m_undoStack
// ============================================================================

void ImageWindow::applyTransformImage(const cv::Mat& img, const transform::TransformBox* newBox)
{
    // P0-6.6: TransformCommand::redo/undo 入口 — 接受变换后 cv::Mat + 可选 box 状态
    if (img.empty()) {
        LOG_WARN("[ImageWindow] applyTransformImage: empty image");
        return;
    }
    //  变换改的是整张文档 (几何变了) -> 文档级写入
    setDocumentImage(img);
    if (m_canvas) m_canvas->update();
}

// 应用 QTransform 矩阵入口 (4 mode 自由变换主路径)
//   内部: cv::warpAffine 应用变换 + 推 TransformCommand 推 undoStack
void ImageWindow::applyImageTransform(const QTransform& t, const QString& text)
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    if (!t.isAffine()) {
        LOG_WARN("[ImageWindow] applyImageTransform: QTransform not affine (perspective not supported in P0-6.6)");
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat M = ImageProcessor::qTransformToAffine(t);
    cv::Mat after;
    // 输出 size = 输入 size (PS 风格: 自由变换不裁切, 仅变换)
    ImageProcessor::warpAffine(before, after, M, before.size());
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, text));
    } else {
        setDocumentImage(after);
    }
}

// apply non-destructive transform to a SmartObject layer
//   Used by MainWindow menu actions and LayerPanel right-click "Transform..."
//   entry to drive LayerStack::setSmartObjectTransform + LayerCommand undo.
//
// Identity / no-op short-circuits avoid pushing empty undo entries, matching
// LayerStack::setSmartObjectTransform's "returns false on no-change" semantics.
void ImageWindow::applySmartObjectTransform(int idx, const QTransform& newTransform)
{
    if (!m_layerStack) return;
    if (idx < 0 || idx >= m_layerStack->count()) return;
    auto l = m_layerStack->at(idx);
    if (!l || l->kind != layers::Layer::SmartObject) return;
    const QTransform oldT = l->transform;
    const bool     oldHad = l->hasTransform;
    // Identity input on a layer without transform is a complete no-op.
    if (newTransform.isIdentity() && !oldHad) return;
    // Identity input on a layer WITH transform clears it (still meaningful
    // for undo); only skip if already cleared (defensive duplicate-guard).
    if (oldHad && newTransform.isIdentity() == false && newTransform == oldT) return;
    if (!m_layerStack->setSmartObjectTransform(idx, newTransform)) {
        // LayerStack returned false: same transform already applied
        // (Idempotent guard inside LayerStack itself)
        return;
    }
    if (m_undoStack) {
        // Read post-state for the redo-side stamp. The post-undo transform
        // and has-flag are required for redo symmetry (LayerCommand stores
        // old + new pairs even though redo is no-op for transform).
        auto lAfter = m_layerStack->at(idx);
        const QTransform newT = lAfter ? lAfter->transform : QTransform();
        const bool       newHad = lAfter ? lAfter->hasTransform : false;
        auto *cmd = layers::LayerCommand::makeSetSmartObjectTransform(
            m_layerStack.get(), idx, oldT, oldHad, newT, newHad);
        m_undoStack->push(cmd);
    }
    refreshCanvasNow();
    statusBar()->showMessage(tr("已应用 SmartObject 变换"), 3000);
}

void ImageWindow::onFreeTransform()
{
    // 切到 TransformTool (Ctrl+T), ToolContext 接管鼠标事件转发
    if (!m_ctx) {
        statusBar()->showMessage(tr("ToolContext 未初始化"), 2000);
        return;
    }
    m_ctx->setState(std::make_unique<tools::TransformTool>());
    // P0-6.10: 把 TransformTool 的 box weak ref 给 ImageCanvas, drawForeground 画 10 handle
    if (auto* tool = dynamic_cast<tools::TransformTool*>(m_ctx->currentState())) {
        if (m_canvas) m_canvas->setTransformBox(tool->boxRef());
        // P0-6.12: 注入 rotation 同步 callback → PropertiesDock.setRotation
        if (m_rightDock) {
            if (auto* props = m_rightDock->findChild<docks::PropertiesDock*>()) {
                tool->setRotationCallback([props](qreal deg) {
                    props->setRotation(deg);
                });
                // 初始 clear (无 transform)
                props->clearRotation();
            }
        }
    }
    statusBar()->showMessage(tr("自由变换 — 拖动 8 handle 或中心点 (Esc 退出)"), 3000);
}

void ImageWindow::onImageFlipH()
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat after;
    ImageProcessor::flip(before, after, /*around y*/ 1);
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, tr("水平翻转")));
    } else {
        setDocumentImage(after);
    }
}

void ImageWindow::onImageFlipV()
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat after;
    ImageProcessor::flip(before, after, /*around x*/ 0);
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, tr("垂直翻转")));
    } else {
        setDocumentImage(after);
    }
}

void ImageWindow::onImageRotate90CW()
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat after;
    ImageProcessor::rotate90(before, after, /*90 CW*/ 1);
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, tr("旋转 90° 顺时针")));
    } else {
        setDocumentImage(after);
    }
}

void ImageWindow::onImageRotate90CCW()
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat after;
    ImageProcessor::rotate90(before, after, /*270 CW = 90 CCW*/ 3);
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, tr("旋转 90° 逆时针")));
    } else {
        setDocumentImage(after);
    }
}

void ImageWindow::onImageRotate180()
{
    if (m_current.empty()) {
        statusBar()->showMessage(tr("当前页面没有图像"), 2000);
        return;
    }
    cv::Mat before = m_current.clone();
    cv::Mat after;
    ImageProcessor::rotate90(before, after, /*180*/ 2);
    if (m_undoStack) {
        m_undoStack->push(new transform::TransformCommand(this, before, after, tr("旋转 180°")));
    } else {
        setDocumentImage(after);
    }
}
// onOpen / onSave / onSaveAs / onClose 4 个 IO slot 已搬到
//   src/media/imagewindow/ImageIOController.cpp 实现, 这里不再保留.

