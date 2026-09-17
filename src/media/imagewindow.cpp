#include "imagewindow.h"
#include "ui_imagewindow.h"
#include "graphicstextitem.h"
#include "logger.h"
#include "mediators/ToolMediator.h"

#include "imageprocessor.h"
// P0-1.4 (2026-09-07): recentmanager.h 移到 ImageIOController.cpp (open/save 段搬走)
#include "../core/ThemeManager.h"

// P0-1.2 (2026-09-07): 组件 include
#include "imagewindow/TextOverlayController.h"
#include "imagewindow/MosaicTool.h"
#include "imagewindow/ImageIOController.h"
#include "imagewindow/ImageCanvas.h"
// P0-3.2 (2026-09-08): 新组件 AdjustmentPanel (5 tab 色彩调整)
#include "imagewindow/AdjustmentPanel.h"
// F-G.3 (2026-09-09): RightPanelStack 3 dock (颜色/属性/图层) — 浮动在 imagewindow 右上角
#include "docks/RightPanelStack.h"
#include "docks/RightPanelDock.h"
#include "docks/LayersDock.h"
#include "docks/ChannelPathPanel.h"
#include "docks/PropertiesDock.h"
#include "docks/HistoryDock.h"
// P1.4.3 (2026-09-17): LayerPanel 实例化 + 注入 LayersDock tab 0
//   LayerPanel 信号接通需要完整 LayerPanel.h + LayerCommand.h 定义
#include "imageworker/layers/LayerPanel.h"
#include "imageworker/layers/LayerCommand.h"
#include "../media/mediators/WorkspaceMediator.h"
// P0-4 (2026-09-10): selection system
#include "selection/SelectionModel.h"
#include "selection/SelectionStrategy.h"
#include "selection/SelectionCommand.h"
// P0-5 (2026-09-10): filter system
#include "filters/FilterStrategy.h"
#include "filters/FilterFactory.h"
#include "filters/FilterCommand.h"
// F-N (2026-09-10): ToolContext (state machine) for 8 tools event forwarding
#include "tools/ToolContext.h"
// P0-6.9 (2026-09-14): TransformTool 接入 (Ctrl+T 自由变换)
#include "tools/TransformTool.h"
// P0-6.6 (2026-09-14): transform system (6 menu action + 8 handle 自由变换)
#include "transform/TransformBox.h"
#include "transform/TransformCommand.h"

// P0-1.4 (2026-09-07): QFileDialog / QDir / QMessageBox 移到 ImageIOController.cpp
//   (open / save / saveAs 段搬走, imagewindow.cpp 不再用)
#include <QFileInfo>
// P1.4.3 (2026-09-17): LayerPanel 信号接通需要 (EditSource 打开源 / Relink 选源 / URL 包装)
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
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
        // P0-1.3 (2026-09-07): m_scene 搬到 m_canvas
        win->m_canvas->scene()->removeItem(m_item.data());
        delete m_item.data();
        m_item = nullptr;
    } else if (m_op == Remove) {
        // Remove undo: 重新创建 item (用保存的 oldText)
        auto *item = new GraphicsTextItem();
        item->setPosition(m_oldPos);
        item->setRotationDeg(m_oldRot);
        item->restoreFromText(m_oldText);
        // P0-1.3 (2026-09-07): m_scene 搬到 m_canvas
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
        // P0-1.3 (2026-09-07): m_scene 搬到 m_canvas
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
            // P0-1.3 (2026-09-07): m_scene 搬到 m_canvas
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
        m_w->replaceCurrentImage(m_imgBefore);
    } else {
        m_w->applyParams(m_before, /*repaint*/true);
    }
}

void ImageEditCommand::redo()
{
    if (m_w.isNull()) return;
    if (m_useImage) {
        m_w->replaceCurrentImage(m_imgAfter);
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

    // P0-2 (2026-09-08): EngineContext 实例化
    //   Profile::ImageEditor = 5 池预设 (interactive / background / io / ai / offline)
    //   显式传 hw=2 限 2 线程 - 跟开发机 4 核环境下 background 池开 4 线程的默认行为相比,
    //   减少一些上下文切换开销, 也避免某些 CI 机器 (1-2 核) 起 4 线程池被 OOM
    m_engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 2));
    m_currentTaskId.store(0);

    // P0-1.3 (2026-09-07): 画布搬到 ImageCanvas 组件
    //   Stage E (2026-09-15): m_canvas 替换 ui->graphicsView 实际进入 verticalLayout
    //   之前: m_canvas 创建后 parent = this (顶层), 没进 layout, 跑到 (0,0) 显示成小方块
    //         ui->graphicsView 是空 QGraphicsView 占位, 显示空白
    //   现在: 拿掉 ui->graphicsView, 把 m_canvas 加进 verticalLayout, canvas 占满中央
    m_canvas = std::make_unique<ImageCanvas>(this);
    m_canvas->setHost(this);
    // 用 m_canvas 替换 verticalLayout 里的 ui->graphicsView 占位 (Qt 转移所有权)
    if (ui->graphicsView) {
        // 1. 隐藏/脱离原 ui->graphicsView (空占位)
        ui->graphicsView->setParent(nullptr);
        ui->graphicsView->hide();
        // 2. m_canvas 重新 parent 到 centralWidget (verticalLayout 跟 centralWidget 同 owner)
        m_canvas->setParent(ui->centralWidget);
        // 3. 用 size policy 跟 graphicsView 保持一致 (Expanding/Expanding)
        m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 4. verticalLayout 拿掉 graphicsView, 加 m_canvas (添加到 toolbar 后面)
        ui->verticalLayout->removeWidget(ui->graphicsView);
        ui->verticalLayout->addWidget(m_canvas.get());
    }
    // 在 m_canvas 的 viewport 上装 eventFilter (原 ui->graphicsView->viewport())
    m_canvas->viewport()->installEventFilter(this);
    // 焦点策略 + ScrollHandDrag 等都在 ImageCanvas ctor 里设

    // 笔刷光标 (默认隐藏, 鼠标移进 viewport 才显示)
    //   P0-1.3: m_brushCursor 加到 m_canvas->scene() (原 m_scene)
    {
        const int r0 = ui->sliderMosaicSize->value() / 2;
        m_brushCursor = m_canvas->scene()->addEllipse(-r0, -r0,
            ui->sliderMosaicSize->value(), ui->sliderMosaicSize->value());
        QPen pen(QColor(76, 175, 128, 220));    // 浅绿
        pen.setWidth(2);
        pen.setCosmetic(true);  // 不随缩放变化
        m_brushCursor->setPen(pen);
        m_brushCursor->setBrush(QColor(76, 175, 128, 40));
        m_brushCursor->setZValue(999);
        m_brushCursor->setVisible(false);
    }

    buildActions();
    applyPanelTheme();

    // P0-1.2 (2026-09-07): 实例化 5 个组件
    //   这一轮 P0-1.2 只用 m_textCtrl + m_mosaicTool, 其他 nullptr 占位等下一轮
    m_textCtrl    = std::make_unique<TextOverlayController>(this);
    m_textCtrl->setHost(this);
    // 同步左面板的字体/字号到组件 (createTextItem 用)
    m_textCtrl->setTextFont(ui->comboTextFont->currentFont().family());
    m_textCtrl->setTextSize(ui->spinTextSize->value());
    m_textCtrl->setTextColor(QColor(Qt::white));

    m_mosaicTool  = std::make_unique<MosaicTool>(this);
    m_mosaicTool->setHost(this);
    // 同步左面板的涂抹 size/type 到组件
    m_mosaicTool->setSize(ui->sliderMosaicSize->value());
    m_mosaicTool->setType(static_cast<MosaicTool::Type>(ui->comboMosaicType->currentIndex()));

    // P0-1.4 (2026-09-07): IO 控制器 (open / save / saveAs / close) 实例化
    //   接管原 ImageWindow::onOpen / onSave / onSaveAs / onClose 4 个 slot
    //   m_io 持有 m_host (this) 引用, 通过 friend class + 代理方法触发
    //   markSaved / setFilePath / emitEditTimeShouldUpdate / emitCloseRequested
    m_io = std::make_unique<ImageIOController>(this);
    m_io->setHost(this);

    // P0-3.2 (2026-09-08): PS 风格色彩调整 UI (5 tab Curves/Levels/HSL/B&W/ChannelMixer)
    m_adjustmentPanel = std::make_unique<AdjustmentPanel>(this);
    m_adjustmentPanel->setHost(this);

    // F-G.3 Fix (2026-09-10): PS 风格右侧 panel (1 个 widget 装 4 dock + 1 调整 tab)
    //   撤销原 3 个分离 dock (RightPanelStack + adjDock + infoDock) + addDockWidget 抢画布位置
    // Stage C (2026-09-15): 走 Qt 原生 addDockWidget + resizeDocks (替代 setGeometry 浮动)
    //   之前: RightPanelDock 直接 setGeometry 浮动在右上角, 跟 centralWidget 抢画布位置,
    //         resize 不联动 (imagewindow 缩小后 dock 还停在原位置)
    //   现在: 包到 QDockWidget 走 RightDockWidgetArea, Qt 自动管理 dock 与 canvas 边界,
    //         resizeDocks 设初始宽度 340, imagewindow resize 时 dock 跟着缩
    m_rightDock = std::make_unique<docks::RightPanelDock>();
    // F-G.3: release() 转移所有权给 m_rightDock (QTabWidget::addTab reparent 后 Qt 析构会 delete 一次)
    m_rightDock->setAdjustmentPanel(m_adjustmentPanel.release());

    m_rightDockContainer = new QDockWidget(this);
    m_rightDockContainer->setObjectName("rightDockContainer");
    // NoDockWidgetFeatures: 隐藏关闭/浮动按钮 (Stage D 通过 workspace 按钮控制显隐)
    m_rightDockContainer->setFeatures(QDockWidget::NoDockWidgetFeatures);
    // 隐藏 title bar (PS 风格 panel 无标题栏)
    auto* emptyTitle = new QWidget();
    emptyTitle->setFixedHeight(0);
    m_rightDockContainer->setTitleBarWidget(emptyTitle);
    // 把 RightPanelDock 装到 QDockWidget 内部 (Qt 接管所有权, dock 销毁时 delete)
    m_rightDockContainer->setWidget(m_rightDock.release());

    addDockWidget(Qt::RightDockWidgetArea, m_rightDockContainer);
    resizeDocks({m_rightDockContainer}, {340}, Qt::Horizontal);

    // P1.3.4 (2026-09-17): ToolMediator per-ImageWindow (parent = this)
    //   Owns Mediator, hands raw pointer to ToolContext; both LeftToolBar and
    //   MainWindow's menu actions can now reach it through toolMediator().
    m_toolMed = std::make_unique<mediators::ToolMediator>(this);

    // F-N (2026-09-10): ToolContext 实例化 (state machine for 8 tools)
    //   m_ctx 在 ctor 创建, eventFilter 通过 m_ctx->onMouseXxx 转发给 current ToolState
    //   attach 后 LeftToolBar/ImageOptionBar 可以通过 toolContext() 拿到
    m_ctx = std::make_unique<tools::ToolContext>(this);
    m_ctx->attach(this, m_toolMed.get());

    // P0-4 (2026-09-10): SelectionModel 实例化
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
            // P0-7.4 (2026-09-14): PropertiesDock 同步 text item 选中状态
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

    // F-G.3 Fix (2026-09-10): ChannelPathPanel 现在是 RightPanelDock 第 4 tab
    //   (之前是 LayersDock tab 1, 拆出来独立 tab 更 PS 风格)
    //   LayersDock 现在是第 3 tab, 只装 LayerPanel (F-G.3 集成)
    //   ChannelPathPanel 已经在 RightPanelDock ctor 创建

    // 兜底: 显式关闭马赛克按钮, 避免 .ui 默认状态导致启动就进入涂抹
    ui->btnMosaicMode->setChecked(false);
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

    // P0-8.1 (2026-09-15): 绑定 HistoryDock -> undoStack
    //   m_rightDock 已经创建, historyDock() 现在非 null
    if (m_rightDock) {
        if (auto* histDock = m_rightDock->historyDock()) {
            histDock->setUndoStack(m_undoStack);
        }
    }

    // 主题变化
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ImageWindow::applyPanelTheme);
}

ImageWindow::~ImageWindow()
{
    // 关键: 先 clear undoStack, 触发 ~ImageEditCommand
    // 这样 QPointer<ImageWindow> m_w 在 ImageWindow 还没半析构时变 null
    // 避免 close 时 "class destructor may have already run" ASSERT
    if (m_undoStack) {
        m_undoStack->disconnect();
        m_undoStack->clear();
    }
    // P0-1.2 (2026-09-07): 清空文字 item 列表 (避免 scene 析构时 item 还在 scene 里)
    //   委托给 m_textCtrl (TextOverlayController)
    if (m_textCtrl) {
        m_textCtrl->removeAllFromSceneAndDelete();
    }
    // P0-2 (2026-09-08): EngineContext 析构会 ~ThreadPool -> shutdown(false)
    //   等所有 worker 退出后再删, 避免后台 task 跑一半 ImageWindow 已经销毁
    m_engine.reset();
    // P0-4 (2026-09-10): m_selection 析构时 ImageCanvas::m_sel 弱引用自动失效
    m_selection.reset();
    delete ui;
}

// P0-4 (2026-09-10): cv::Mat -> QImage (BGRA8888 / BGR888 / Gray) for selection strategies
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

// P1.3.7 (2026-09-17): Layer-as-QImage accessor
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

// 公开: 给 ImageEditCommand (m_useImage 模式) 还原 m_current
void ImageWindow::replaceCurrentImage(const cv::Mat &img)
{
    if (img.empty()) return;
    // 阶段 1 W4.3 Phase 1: 同步更新 base layer image + invalidate cache
    if (m_layerStack && m_layerStack->count() > 0) {
        auto base = m_layerStack->baseLayer();
        if (base) base->image = img.clone();
    }
    invalidateCurrentCache();
    m_current = img.clone();   // 兼容: 旧引用直接 m_current
    renderToView();
    // P0-4 (2026-09-10): sync selection mask size with image size
    if (m_selection) {
        m_selection->setSize(QSize(m_current.cols, m_current.rows));
    }
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
    // P0-1.3 (2026-09-07): m_zoom 搬到 m_canvas
    m_canvas->setZoom(1.0);

    // 默认参数 (clean 状态)
    // Stage B (2026-09-15): 旧 m_adjustment 已删除, ParamSet 在 P1 阶段重新设计

    // 清空撤销栈
    m_undoStack->clear();
    m_savedIndex = 0;

    // 阶段 1 W4.3 Phase 1: 初始化 layerStack, 把加载的图片作为 base layer
    //   LayerStack 是 QObject, parent = this (生命周期跟随 ImageWindow)
    m_layerStack = std::make_unique<layers::LayerStack>(this);
    m_layerStack->setBaseLayer(img);
    // 监听 layer 变化 → invalidate cache
    connect(m_layerStack.get(), &layers::LayerStack::layerChanged,
            this, [this](int){ invalidateCurrentCache(); });
    connect(m_layerStack.get(), &layers::LayerStack::layerAdded,
            this, [this](int){ invalidateCurrentCache(); });
    connect(m_layerStack.get(), &layers::LayerStack::layerRemoved,
            this, [this](int){ invalidateCurrentCache(); });
    // P1.4.2 (2026-09-17): 智能对象文件监听器
    //   监听每个 SmartObject layer 的源文件 mtime, 触发源变化时弹 statusBar 提示
    //   layerAdded/Removed/Changed 后 rewatchAll 同步 watch 列表 (MainWindow 4 slot
    //   也会显式调 rewatchAll, 这是兜底)
    m_smartWatcher = std::make_unique<layers::SmartObjectWatcher>(this);
    connect(m_smartWatcher.get(), &layers::SmartObjectWatcher::sourceFileChanged,
            this, [this](int, const QString &path){
        const QString name = QFileInfo(path).fileName();
        statusBar()->showMessage(
            tr("智能对象源文件已修改: %1 (右键图层 → 刷新)").arg(name), 5000);
    });
    auto resyncWatcher = [this]{
        if (m_smartWatcher && m_layerStack)
            m_smartWatcher->rewatchAll(m_layerStack.get());
    };
    connect(m_layerStack.get(), &layers::LayerStack::layerAdded, this, resyncWatcher);
    connect(m_layerStack.get(), &layers::LayerStack::layerRemoved, this, resyncWatcher);
    connect(m_layerStack.get(), &layers::LayerStack::layerChanged, this, resyncWatcher);
    resyncWatcher();
    // 阶段 1 Step A Bug 1 (2026-09-04): 监听 selection 变化
    //   切 layer 不改像素, 但要让画布知道, 触发重绘 + 通知 MainWindow
    connect(m_layerStack.get(), &layers::LayerStack::selectionChanged,
            this, &ImageWindow::onLayerSelectionChanged);
    invalidateCurrentCache();   // 强制重算 m_current

    // P1.4.3 (2026-09-17): 实例化 LayerPanel, 注入 LayersDock tab 0
    //   P1.4.2 留了 LayerPanel 类定义完整但没 new 的 bug, 这一轮补上.
    //   LayerPanel::ctor 需要 raw LayerStack*, parent = this (QObject parent-child 关系
    //   保证 unique_ptr 释放前 LayerPanel 不会被 Qt 析构). 注入 LayersDock tab 0 用
    //   setContentWidget + release() 转移, LayersDock reparent 后接管所有权.
    m_layerPanel = std::make_unique<layers::LayerPanel>(m_layerStack.get());
    layers::LayerPanel* rawPanel = m_layerPanel.get();   // release() 之后 unique_ptr 变 null, 先存 raw
    if (m_rightDock) {
        if (auto* layersDock = m_rightDock->layersDock()) {
            // 转移所有权: LayersDock::setContentWidget 会 setParent 到自己 (m_layersDock 是其成员)
            layersDock->setContentWidget(0, m_layerPanel.release());
        }
    }
    // rawPanel 在 LayersDock 接管期间保持有效 (LayersDock 析构会 delete 它, 比 ImageWindow 早)

    // P1.4.3 (2026-09-17): LayerPanel 用户操作信号 → ImageWindow API (数据层)
    //   用 rawPanel (非 m_layerPanel.get()) 连接信号, 因为 release() 后 unique_ptr 为空
    if (rawPanel) {
        connect(rawPanel, &layers::LayerPanel::selectionChangedFromPanel,
                this, [this](int idx) {
            if (m_layerStack) m_layerStack->setSelection(idx);
        });
        // P1.4.3: 智能对象 Edit Source (复用 P1.4.2 主菜单的 QDesktopServices 路径, 不推 undoStack)
        connect(rawPanel, &layers::LayerPanel::editSmartObjectSourceRequested,
                this, [this](int idx) {
            if (!m_layerStack || idx < 0 || idx >= m_layerStack->count()) return;
            auto l = m_layerStack->at(idx);
            if (!l || l->kind != layers::Layer::SmartObject || l->sourceFilePath.isEmpty()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(l->sourceFilePath));
            statusBar()->showMessage(tr("已在系统默认应用打开源文件"), 3000);
        });
        // P1.4.3: 智能对象 Refresh (重新加载源文件, embed/link 都生效)
        connect(rawPanel, &layers::LayerPanel::refreshSmartObjectRequested,
                this, [this](int idx) {
            if (!m_layerStack) return;
            m_layerStack->refreshSmartObject(idx);
            invalidateCurrentCache();
            statusBar()->showMessage(tr("智能对象已刷新"), 3000);
        });
        // P1.4.3: 智能对象 Toggle Embed (link <-> embed 切换, 不推 undoStack - 跟 P1.4.2 mainwindow 一致)
        connect(rawPanel, &layers::LayerPanel::toggleSmartObjectEmbedRequested,
                this, [this](int idx) {
            if (!m_layerStack) return;
            m_layerStack->toggleSmartObjectEmbed(idx);
            invalidateCurrentCache();
        });
        // P1.4.3: 智能对象 Convert (Bitmap -> SmartObject, 嵌入模式) - 推 undoStack 走 makeConvertToSmartObject
        connect(rawPanel, &layers::LayerPanel::convertToSmartObjectRequested,
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
            invalidateCurrentCache();
            statusBar()->showMessage(tr("已转换为智能对象 (嵌入模式)"), 3000);
        });
        // P1.4.3: 智能对象 Rasterize (SmartObject -> Bitmap) - 推 undoStack 走 makeRasterizeSmartObject
        connect(rawPanel, &layers::LayerPanel::rasterizeSmartObjectRequested,
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
            invalidateCurrentCache();
            statusBar()->showMessage(tr("已栅格化智能对象"), 3000);
        });
        // P1.4.3: 智能对象 Relink (重新链接源文件) - 弹 QFileDialog 选文件, 推 undoStack 走 makeSetSmartObject
        connect(rawPanel, &layers::LayerPanel::relinkSmartObjectRequested,
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
            invalidateCurrentCache();
            statusBar()->showMessage(tr("已重新链接源文件"), 3000);
        });
    }

    emit filePathChanged(m_filePath);
    // Stage F (2026-09-15): 补回 renderToView 触发
    //   Stage B 删 m_adjustment->syncApplyCurrentParams(true) 时连带漏了渲染触发
    //     旧: syncApplyCurrentParams -> setCurrentImage -> renderToView
    //     新: 删了中间人, 直接调 renderToView
    //   不调的后果: loadFile 返回 true 但 canvas pixmap 还是空 (用户看到空白)
    renderToView();
    return true;
}

// P0-1.2 fix (2026-09-08): filePath() 防御式实现
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
    // P0-1.2 fix (2026-09-08): tst_ImageWorker SEGFAULT 真因修复
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

void ImageWindow::setCurrentImage(const cv::Mat &img)
{
    m_current = img.clone();
    // 阶段 1 W4.3 Phase 1: 同步 base layer image (适用 setCurrentImage 流程)
    if (m_layerStack && m_layerStack->count() > 0) {
        auto base = m_layerStack->baseLayer();
        if (base) base->image = img.clone();
    }
    invalidateCurrentCache();
    refreshAll();
}

void ImageWindow::refreshAll()
{
    renderToView();
}

void ImageWindow::renderToView()
{
    // 阶段 1 W4.3 Phase 1: m_current 来自 layerStack.render() 缓存
    if (m_currentDirty) rebuildCurrentCache();
    if (m_current.empty()) {
        // P0-1.3 (2026-09-07): m_item 搬到 m_canvas
        m_canvas->pixmapItem()->setPixmap(QPixmap());
        return;
    }
    const QImage qimg = ImageProcessor::matToQImage(m_current);
    m_canvas->pixmapItem()->setPixmap(QPixmap::fromImage(qimg));
    m_canvas->scene()->setSceneRect(m_canvas->pixmapItem()->pixmap().rect());
    // 关键: 重置变换 + 按当前 m_canvas->zoom() 缩放 (跟原逻辑一致, 保持每次渲染都重设 transform)
    m_canvas->resetTransform();
    m_canvas->scale(m_canvas->zoom(), m_canvas->zoom());
}

// 阶段 1 W4.3 Phase 1: 从 layerStack.render() 重算 m_current 缓存
// P0-2.5 (2026-09-08): 大图走 LayerStack::renderAsync 走 background pool,
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
    const long long pixelCount = static_cast<long long>(m_original.cols)
                                * static_cast<long long>(m_original.rows);
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
    m_layerStack->renderAsync(m_engine.get(),
        [this, myTaskId](cv::Mat result) -> void {
            if (m_currentTaskId.load() != myTaskId) return;   // 已被新 task 取代, 丢弃
            m_current = std::move(result);
            m_currentDirty = false;
            renderToView();
        });
}

// 阶段 1 Step A Bug 1 (2026-09-04): LayerPanel 切图层
//   当前不直接改渲染 (切 layer 不改像素), 但触发重绘让画布激活层指示器更新
//   也方便 MainWindow 挂钩 (e.g. 切到 Adjustment 时自动展开属性面板)
void ImageWindow::onLayerSelectionChanged(int /*index*/)
{
    // 触发重绘 (画布可能在 active layer 边框/调色板上画指示)
    update();
}

// P0-5 (2026-09-10): 滤镜应用 (主菜单 4 action 接真)
//   流程: FilterFactory::createFilter + strategy->apply + push FilterCommand
//   FilterCommand 内部 ctor 时 apply 一次, undo/redo 复用
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
    m_undoStack->push(new filter::FilterCommand(
        this, m_current, kind, filterName));
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

// Stage B (2026-09-15): 撤销 P0-1.2 ImageAdjustmentPanel (5 toggle + 10 slider 旧 UI)
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

// --- 离散操作 (马赛克改为拖动涂抹) ---

// 笔刷大小变 -> 圆圈跟着变
//   P0-1.2 (2026-09-07): 委托给 m_mosaicTool, 组件自己管 brush cursor
void ImageWindow::onMosaicSizeChanged(int v)
{
    ui->lblMosaicSizeVal->setText(QStringLiteral("%1 px").arg(v));
    if (m_mosaicTool) m_mosaicTool->setSize(v);
}

// 马赛克模式切换 - 主流做法: 涂抹和文字编辑互斥
//   进马赛克: 锁所有文字 item 的双击编辑 (setBlockDoubleClickEdit=true)
//             退出当前文字 item 的编辑状态 (避免边涂抹边编辑)
//   退马赛克: 解锁所有文字 item 的双击编辑
//   拖动/resize/rotate 文字 item 在马赛克模式下仍可用 (主流做法: 滤镜激活时, 文字图层仍可 transform)
//   P0-1.2 (2026-09-07): 主体逻辑迁到 MosaicTool::setEnabled, 这里只剩按钮文字 + 调组件
void ImageWindow::onMosaicModeToggled(bool on)
{
    if (m_mosaicTool) m_mosaicTool->setEnabled(on);
    if (m_brushCursor) m_brushCursor->setVisible(false);  // 默认隐藏, 进入图区再显示
    if (on) {
        ui->btnMosaicMode->setText(tr("✓  涂抹中 (再点退出)"));
    } else {
        ui->btnMosaicMode->setText(tr("●  进入涂抹模式 (再点退出)"));
    }
}

// =============================================================
// 文字 item 管理 (主流做法: 文字作为 scene item, 永远不烧图)
//   - createTextItem(scenePos): 创建新 item, 入栈, 进入编辑
//   - registerTextItem / unregisterTextItem: 维护 m_textItems 列表
//   - flattenText(): Ctrl+S 前调用, 把所有文字 item 渲染到 m_current, 清空列表
// =============================================================

// 连接文字 item 的信号 (editingFinished + transformFinished) - 撤销栈用
//   抽成 helper, 让 createTextItem 和 Add.redo 重建 item 时都调, 避免 redo 后的 item 没 connect
//   关键: lambda 用 QPointer 捕获, 防止 item 被 delete 后 dangling raw ptr
//   P0-1.2 (2026-09-07): 委托给 m_textCtrl (TextOverlayController)
void ImageWindow::connectTextItemSignals(GraphicsTextItem *item)
{
    if (m_textCtrl) m_textCtrl->connectItemSignals(item);
}

// P0-1.2 (2026-09-07): 委托给 m_textCtrl
GraphicsTextItem *ImageWindow::createTextItem(const QPointF &scenePos)
{
    if (!m_textCtrl) return nullptr;
    // 同步左面板的字体/字号到组件 (createTextItem 内部读 m_textFont/m_textSize)
    m_textCtrl->setTextFont(ui->comboTextFont->currentFont().family());
    m_textCtrl->setTextSize(ui->spinTextSize->value());
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

// 扁平化: 把所有文字 item 用 QPainter 渲染到 QImage, 转 cv::Mat, 烧到 m_current
// 烧图后 m_textItems 清空 (字变成图片像素, 不可再编辑/撤销)
//   P0-1.2 (2026-09-07): 委托给 m_textCtrl
void ImageWindow::flattenText()
{
    if (m_textCtrl) m_textCtrl->flattenText();
}

// 字体变化 -> 应用到 m_currentTextItem
//   P0-1.2 (2026-09-07): 委托给 m_textCtrl
void ImageWindow::onTextFontChanged()
{
    if (!m_textCtrl) return;
    m_textCtrl->onFontChanged(ui->comboTextFont->currentFont().family());
}

// 字号变化 -> 应用到 m_currentTextItem
//   P0-1.2 (2026-09-07): 委托给 m_textCtrl
void ImageWindow::onTextSizeChanged(int v)
{
    if (m_textCtrl) m_textCtrl->onSizeChanged(v);
}

// 文字颜色按钮 -> 应用到 m_currentTextItem
//   P0-1.2 (2026-09-07): 调 QColorDialog 拿颜色, 委托给 m_textCtrl
void ImageWindow::onTextColorClicked()
{
    if (!m_textCtrl) return;
    const QColor cur = m_textCtrl->textColor();
    QColor c = QColorDialog::getColor(cur, this, tr("选择文字颜色"));
    if (!c.isValid()) return;
    m_textCtrl->onColorSelected(c);
    statusBar()->showMessage(
        tr("文字颜色: RGB(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()),
        2000);
}

// eventFilter 集中处理: 文字输入框 Escape 取消 + viewport 鼠标滚轮拖动涂抹
// (实现见下面)

// ---------------- 工具栏 ----------------

void ImageWindow::buildActions()
{
    // 注: actOpen/actSave/actSaveAs/actClose 故意没接 — 顶部菜单已经有, 这里只接 zoom
    connect(ui->actZoomIn,    &QAction::triggered, this, &ImageWindow::onZoomIn);
    connect(ui->actZoomOut,   &QAction::triggered, this, &ImageWindow::onZoomOut);
    connect(ui->actFit,       &QAction::triggered, this, &ImageWindow::onFitWindow);
    connect(ui->actResetZoom, &QAction::triggered, this, &ImageWindow::onResetZoom);

    // Stage B (2026-09-15): 撤销 P0-1.2 5 toggle + 10 slider UI, ImageAdjustmentPanel 类已删

    // 离散操作按钮
    connect(ui->sliderMosaicSize, &QSlider::valueChanged, this, &ImageWindow::onMosaicSizeChanged);
    connect(ui->btnMosaicMode,   &QToolButton::toggled, this, &ImageWindow::onMosaicModeToggled);
    // 关键: 不再 connect btnApplyText — 文字编辑改为"双击图片直接进入"模式, 无需按钮开关
    connect(ui->btnTextColor,    &QToolButton::clicked, this, &ImageWindow::onTextColorClicked);

    // P0-1.2 (2026-09-07): comboMosaicType 变化 -> 同步到 m_mosaicTool
    //   原代码: eventFilter 每次 applyMosaic 读 ui->comboMosaicType->currentIndex()
    //   现在: 组件管 m_type, host 同步过来
    if (m_mosaicTool && ui->comboMosaicType) {
        connect(ui->comboMosaicType, QOverload<int>::of(&QComboBox::currentIndexChanged),
                m_mosaicTool.get(), [this](int idx) {
                    m_mosaicTool->setType(static_cast<MosaicTool::Type>(idx));
                });
    }

    // 文字字体/字号变化 -> 应用到当前选中/正在编辑的 item
    connect(ui->comboTextFont, &QFontComboBox::currentFontChanged,
            this, &ImageWindow::onTextFontChanged);
    connect(ui->spinTextSize, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ImageWindow::onTextSizeChanged);

    // Stage B (2026-09-15): 撤销旧 5 toggle + 10 slider 滑条范围设置 (UI 已删)

    // 字体下拉默认填几个常用中文字体 (QFontComboBox 已经默认装了系统字体, 这里补充几个中文)
    if (ui->comboTextFont->findText(QStringLiteral("Microsoft YaHei UI")) < 0) {
        ui->comboTextFont->addItem(QStringLiteral("Microsoft YaHei UI"));
    }
    if (ui->comboTextFont->findText(QStringLiteral("SimSun")) < 0) {
        ui->comboTextFont->addItem(QStringLiteral("SimSun"));
    }
    if (ui->comboTextFont->findText(QStringLiteral("Microsoft YaHei")) < 0) {
        ui->comboTextFont->addItem(QStringLiteral("Microsoft YaHei"));
    }
}

void ImageWindow::applyPanelTheme()
{
    // 功能面板统一字体 9pt (user 要求)
    QFont panelFont = ui->leftPanel->font();
    panelFont.setPointSize(9);
    ui->leftPanel->setFont(panelFont);

    const auto &p = ThemeManager::instance().palette();
    const QString css = QString(
        "QWidget#leftPanel { background: %1; }"
        "QWidget#leftPanel QGroupBox { color: %2; border: 1px solid %3; border-radius: 5px;"
        "  margin-top: 14px; padding: 12px 6px 6px 6px; font-weight: 600; }"
        "QWidget#leftPanel QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QWidget#leftPanel QLabel { color: %2; }"
        "QWidget#leftPanel QToolButton {"
        "  color: %2; background: transparent; border: 1px solid %3; border-radius: 4px; padding: 4px 8px;"
        "}"
        "QWidget#leftPanel QToolButton:hover { background: %4; }"
        "QWidget#leftPanel QToolButton:checked { background: %5; border-color: %5; color: %6; }"
        "QWidget#leftPanel QSlider::groove:horizontal { background: %3; height: 3px; border-radius: 1px; }"
        "QWidget#leftPanel QSlider::handle:horizontal { background: %2; width: 8px; height: 8px; margin: -3px 0; border-radius: 4px; }"
        "QWidget#leftPanel QSlider::sub-page:horizontal { background: %7; border-radius: 1px; }"
    )
    .arg(p.panelBg.name())
    .arg(p.panelHeader.name())
    .arg(p.panelBorder.name())
    .arg(p.menuHover.name())
    .arg(p.accent.name())
    .arg(p.accentText.name())
    .arg(p.accent.name());
    ui->leftPanel->setStyleSheet(css);
}

// ---------------- 缩放 ----------------
// P0-1.3 (2026-09-07): 薄包装, 实际逻辑在 m_canvas (ImageCanvas) 组件里

void ImageWindow::onZoomIn()    { m_canvas->onZoomIn(); }
void ImageWindow::onZoomOut()   { m_canvas->onZoomOut(); }
void ImageWindow::onResetZoom() { m_canvas->onResetZoom(); }
void ImageWindow::onFitWindow() { m_canvas->onFitWindow(); }

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
    // P0-1.3 (2026-09-07): ui->graphicsView -> m_canvas (画布搬到 ImageCanvas 组件)
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

    // P0-1.3 (2026-09-07): eventFilter 现在装在 m_canvas->viewport() (原 ui->graphicsView->viewport())
    if (watched == m_canvas->viewport()) {
        auto scenePt = [this](const QMouseEvent *me) -> QPointF {
            return m_canvas->mapToScene(me->pos());
        };
        auto imgPt = [this](const QMouseEvent *me) -> QPoint {
            const QPointF sp = m_canvas->mapToScene(me->pos());
            return QPoint(int(sp.x()), int(sp.y()));
        };

        // 滚轮缩放
        // P0-1.3 (2026-09-07): Ctrl+wheel 缩放逻辑搬到 m_canvas->wheelEvent
        //   这里不再处理 Wheel, 让事件自然传到 m_canvas::wheelEvent
        // 鼠标移动 - 笔刷光标位置 + 涂抹
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QPointF sp = scenePt(me);

            // Plan B: handle 拖动中 -> 路由到 item (绕开 Qt itemAt)
            //   P0-1.2 (2026-09-07): m_handleDragItem 搬到 m_textCtrl
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
            //   P0-1.2 (2026-09-07): 委托给 m_mosaicTool
            if (m_mosaicTool && m_mosaicTool->isEnabled() && !m_current.empty()
                && (me->buttons() & Qt::LeftButton)) {
                m_mosaicTool->onSceneDragMove(sp);
            }
            // F-N (2026-09-10): forward to current ToolState (default no-op, harmless)
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
                    // P0-1.2 (2026-09-07): m_textItems -> m_textCtrl->textItems()
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
                        // P0-1.2 (2026-09-07): m_textItems / m_currentTextItem -> m_textCtrl
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
                //   P0-1.2 (2026-09-07): 整体搬过去
                if (m_mosaicTool && m_mosaicTool->isEnabled() && !m_current.empty()) {
                    m_mosaicTool->onSceneClicked(sp);
                    return true;  // 拦截, 不让 graphicsView 启动拖动
                }
                // 文字模式 + 普通模式 单击 -> 忽略 (让 graphicsView 处理 pan)
                // F-N (2026-09-10): forward to current ToolState (放在左键分支末尾, 让 graphicsView 仍能 pan)
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
                        //   P0-1.2 (2026-09-07): TextItemCommand::undo 会调 m_textCtrl->unregisterTextItem
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
        //   P0-1.2 (2026-09-07): 涂抹相关委托给 m_mosaicTool
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
            // F-N (2026-09-10): forward to current ToolState (default no-op, harmless)
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
// P0-1.4 (2026-09-07): 4 个 IO slot (onOpen / onSave / onSaveAs / onClose) 全部搬到
//   src/media/imagewindow/ImageIOController.cpp 实现, 这里不再保留.

// 字体/字号/颜色变化应用到当前选中/正在编辑的 item
// (已拆成 onTextFontChanged / onTextSizeChanged / onTextColorClicked 三个函数)
void ImageWindow::applyTextStyleToCurrent() {}

// F-G.3 (2026-09-09): 注入 WorkspaceMediator
void ImageWindow::attachWorkspaceMed(mediators::WorkspaceMediator* wsMed)
{
    if (m_rightDock) {
        m_rightDock->attach(wsMed);
    }
}

// Stage D (2026-09-15): 右侧 panel 显隐 (给 mainwindow 视图菜单 toggle 用)
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
// P0-6.6 (2026-09-14): 图像变换 6 槽 + applyTransformImage
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
    setCurrentImage(img);
    // 同步 m_original (applyTransformImage 通常产生 new original — 翻转/旋转后)
    // P0-6.6 简化: 不改 m_original, 由 P0-6.7+ 实装完整 original 同步
    //   if (m_original.empty() == false) m_original = img.clone();  // 简化
    if (m_canvas) m_canvas->update();
}

// P0-6.6 (2026-09-14): 应用 QTransform 矩阵入口 (4 mode 自由变换主路径)
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
        setCurrentImage(after);
    }
}

void ImageWindow::onFreeTransform()
{
    // P0-6.9 (2026-09-14): 切到 TransformTool (Ctrl+T), ToolContext 接管鼠标事件转发
    if (!m_ctx) {
        statusBar()->showMessage(tr("ToolContext 未初始化"), 2000);
        return;
    }
    m_ctx->setState(std::make_unique<tools::TransformTool>());
    // P0-6.10: 把 TransformTool 的 box weak ref 给 ImageCanvas, drawForeground 画 10 handle
    if (auto* tool = dynamic_cast<tools::TransformTool*>(m_ctx->currentState())) {
        if (m_canvas) m_canvas->setTransformBox(tool->box());
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
        setCurrentImage(after);
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
        setCurrentImage(after);
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
        setCurrentImage(after);
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
        setCurrentImage(after);
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
        setCurrentImage(after);
    }
}
// P0-1.4 (2026-09-07): onOpen / onSave / onSaveAs / onClose 4 个 IO slot 已搬到
//   src/media/imagewindow/ImageIOController.cpp 实现, 这里不再保留.
