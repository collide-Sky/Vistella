#include "imagewindow.h"
#include "ui_imagewindow.h"
#include "graphicstextitem.h"
#include "logger.h"

#include "imageprocessor.h"
#include "imageinfopanel.h"
// P0-1.4 (2026-09-07): recentmanager.h 移到 ImageIOController.cpp (open/save 段搬走)
#include "../core/ThemeManager.h"

// P0-1.2 (2026-09-07): 组件 include
#include "imagewindow/TextOverlayController.h"
#include "imagewindow/MosaicTool.h"
#include "imagewindow/ImageIOController.h"
#include "imagewindow/ImageCanvas.h"
#include "imagewindow/ImageAdjustmentPanel.h"
// P0-3.2 (2026-09-08): 新组件 AdjustmentPanel (5 tab 色彩调整)
#include "imagewindow/AdjustmentPanel.h"
// F-G.3 (2026-09-09): RightPanelStack 3 dock (颜色/属性/图层) — 浮动在 imagewindow 右上角
#include "docks/RightPanelStack.h"
#include "docks/RightPanelDock.h"
#include "docks/LayersDock.h"
#include "docks/ChannelPathPanel.h"
#include "docks/PropertiesDock.h"
#include "docks/HistoryDock.h"
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
    //   .ui 保持 QGraphicsView, 仍叫 ui->graphicsView
    //   m_canvas 是独立组件 (this 子对象, 不放进 ui->graphicsView 的 layout)
    //   P0-1.3 后续轮次: m_canvas 替换 ui->graphicsView 的显示位置
    m_canvas = std::make_unique<ImageCanvas>(this);
    m_canvas->setHost(this);
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

    // P0-1.3 (2026-09-07): 调整面板搬到 ImageAdjustmentPanel 组件
    //   m_adjustment 必须在 buildActions() 之前创建, 因为 buildActions 里
    //   connect slider/button 到 m_adjustment 的 slot
    m_adjustment = std::make_unique<ImageAdjustmentPanel>(this);
    m_adjustment->setHost(this);
    m_adjustment->setUi(ui);

    buildActions();
    // F-G.3 Fix (2026-09-10): 撤销 buildInfoPanel (infoDock 跟 RightPanelDock 重叠)
    //   infoDock 显示的内容 (基本/统计/直方图) 后续整合到 PropertiesDock
    // buildInfoPanel();   // <-- 撤销 (2026-09-10)
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
    //   跟旧 m_adjustment (5 toggle + 10 slider) 共存
    m_adjustmentPanel = std::make_unique<AdjustmentPanel>(this);
    m_adjustmentPanel->setHost(this);

    // F-G.3 Fix (2026-09-10): PS 风格右侧 panel (1 个 widget 装 4 dock + 1 调整 tab)
    //   撤销原 3 个分离 dock (RightPanelStack + adjDock + infoDock) + addDockWidget 抢画布位置
    m_rightDock = std::make_unique<docks::RightPanelDock>(this);
    // F-G.3: release() 转移所有权给 m_rightDock (QTabWidget::addTab reparent 后 Qt 析构会 delete 一次)
    m_rightDock->setAdjustmentPanel(m_adjustmentPanel.release());
    m_rightDock->setFixedWidth(340);
    m_rightDock->setMinimumHeight(500);
    // 浮动位置: imagewindow 右上角 (跟原 m_rightPanel 同样模式)
    m_rightDock->setGeometry(width() - 340, 0, 340, height());
    m_rightDock->show();
    m_rightDock->raise();

    // F-N (2026-09-10): ToolContext 实例化 (state machine for 8 tools)
    //   m_ctx 在 ctor 创建, eventFilter 通过 m_ctx->onMouseXxx 转发给 current ToolState
    //   attach 后 LeftToolBar/ImageOptionBar 可以通过 toolContext() 拿到
    m_ctx = std::make_unique<tools::ToolContext>(this);
    m_ctx->attach(this, nullptr);

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
    if (m_infoPanel) m_infoPanel->updateInfo(m_current, m_filePath);
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
    // P0-1.3 (2026-09-07): m_params 搬到 m_adjustment
    m_adjustment->setParams(ImageEditCommand::ParamSet());
    // 阶段 1 Step A Bug 3 (2026-09-04): 新文件 invalidate ParamSet hash 缓存
    //   图像内容变了, 即便 params 相同, 结果像素也必须重算
    m_adjustment->invalidateParamCache();

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
    // 阶段 1 Step A Bug 1 (2026-09-04): 监听 selection 变化
    //   切 layer 不改像素, 但要让画布知道, 触发重绘 + 通知 MainWindow
    connect(m_layerStack.get(), &layers::LayerStack::selectionChanged,
            this, &ImageWindow::onLayerSelectionChanged);
    invalidateCurrentCache();   // 强制重算 m_current

    emit filePathChanged(m_filePath);
    // P0-1.3 (2026-09-07): applyCurrentParams 搬到 m_adjustment
    // P0-2 (2026-09-08): loadFile 必须等图加载完才能继续 (后续逻辑依赖 m_current),
    //   强制走 sync 版本. applyCurrentParams() 走 asyncEnabled 控制,
    //   调 syncApplyCurrentParams 跳过 dispatch.
    m_adjustment->syncApplyCurrentParams(/*repaint*/true);
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
    if (m_infoPanel) m_infoPanel->updateInfo(m_current, m_filePath);
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
//      → callback 里再校验 m_currentTaskId, 不等于 myTaskId 直接 return (跟 P0-2
//        ImageAdjustmentPanel::asyncApplyCurrentParams 同样取消语义)
//   3. m_currentDirty 状态机:
//      - sync 路径: 先 render() 再 false (旧行为, 保证 renderToView 不会重入)
//      - async 路径: 先 false (in-flight 期间), callback 完成时 renderToView()
//        这里用 false 防止 renderToView 在 callback 回来之前被调 (那会调 rebuildCurrentCache 重入)
//   4. m_layerStack == nullptr: 保持原行为, 啥也不做
//   5. m_asyncEnabled == false: 走 sync (跟 ImageAdjustmentPanel 行为一致,
//      用户 setAsyncEnabled(false) 时整体 fallback 同步)
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

// P0-1.3 (2026-09-07): applyCurrentParams / setupSliderRanges / syncUiFromParams
//   / onAnyParamChanged / onSatChanged / onHueChanged / onExposureChanged /
//   updateLabel / paramsUnchanged 全部搬到 ImageAdjustmentPanel 组件
//   这里只剩一个薄包装: applyParams 负责设置 m_inUndoRedo 标志, 然后委托给 m_adjustment
void ImageWindow::applyParams(const ImageEditCommand::ParamSet &p, bool repaint)
{
    m_inUndoRedo = true;
    m_adjustment->setParams(p);
    m_adjustment->applyCurrentParams(repaint);
    m_inUndoRedo = false;
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

    // 5 个 toggle 按钮 -> 触发 m_adjustment 的 onAnyParamChanged
    // P0-1.3 (2026-09-07): 信号槽从 this->onAnyParamChanged 改到 m_adjustment->onAnyParamChanged
    auto* adj = m_adjustment.get();
    connect(ui->btnGray,    &QToolButton::toggled, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    connect(ui->btnInvert,  &QToolButton::toggled, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    connect(ui->btnBinary,  &QToolButton::toggled, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    connect(ui->btnSharpen, &QToolButton::toggled, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    connect(ui->btnEdge,    &QToolButton::toggled, adj, &ImageAdjustmentPanel::onAnyParamChanged);

    // 阶段 1 Step A Bug (2026-09-04) 关键: Qt 官方要求先初始化控件再连信号槽
    //   之前: connect 在前, setupSliderRanges 在后 → setValue 触发 valueChanged 时 slot 还在监听
    //   现在: setupSliderRanges 在前, 控件初始值设好 (无信号), 再连信号 (用户操作才触发)
    // P0-1.3: 委托给 m_adjustment
    m_adjustment->setupSliderRanges();

    // 滑条
    // 阶段 1 Step A Bug DEBUG (2026-09-04): 临时 log 验 connect 成功
    // P0-1.3: 全部连到 m_adjustment
    bool c1 = connect(ui->sliderAlpha,     &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c2 = connect(ui->sliderBeta,      &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c3 = connect(ui->sliderBinary,    &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c4 = connect(ui->sliderBlur,      &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c5 = connect(ui->sliderSharpen,   &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c6 = connect(ui->sliderCannyLow,  &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    bool c7 = connect(ui->sliderCannyHigh, &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onAnyParamChanged);
    qDebug() << "[connect] alpha=" << c1 << "beta=" << c2 << "binary=" << c3 << "blur=" << c4
             << "sharpen=" << c5 << "cannyLow=" << c6 << "cannyHigh=" << c7;

    // 3 个新滑条 (饱和度/色相/曝光) - label 实时更新, 然后触发 apply
    // P0-1.3: 连到 m_adjustment 的 onSatChanged/...
    connect(ui->sliderSat,      &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onSatChanged);
    connect(ui->sliderHue,      &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onHueChanged);
    connect(ui->sliderExposure, &QSlider::valueChanged, adj, &ImageAdjustmentPanel::onExposureChanged);

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

    // 阶段 1 Step A Bug (2026-09-04) 关键根因修复:
    //   .ui 文件里 sliders 全部没设 range, QSlider 默认 0-99
    //   代码 setValue(100) (alphaPct 默认 100) 被 clamp 到 99 → alpha=0.99 vs 1.00 用户完全看不出
    //   亮度/二值化/模糊看起来"没反应" 的根因就是这
    //   修法: 在代码里 setRange, 不动 .ui (避免重新生成 .ui 麻烦)
    m_adjustment->setupSliderRanges();

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

    // 滑条范围
    ui->sliderAlpha->setRange(0, 300);
    ui->sliderBeta->setRange(-100, 100);
    ui->sliderBinary->setRange(0, 255);
    ui->sliderBlur->setRange(1, 31);
    ui->sliderBlur->setSingleStep(2);
    ui->sliderBlur->setPageStep(2);
    ui->sliderSharpen->setRange(1, 5);
    ui->sliderCannyLow->setRange(0, 200);
    ui->sliderCannyHigh->setRange(0, 300);
    // 颜色/曝光范围已在 .ui 里设过 (sat 0..200, hue -180..180, exposure -100..100)

    // P0-1.3 (2026-09-07): 委托给 m_adjustment
    m_adjustment->syncUiFromParams(m_adjustment->params());
    m_adjustment->updateLabel();
}

void ImageWindow::buildInfoPanel()
{
    // F-G.3 Fix (2026-09-10): 撤销 infoDock (跟 RightPanelDock 重叠, 改用 hide)
    //   infoDock 显示的图像信息 (基本/统计/直方图) 后续整合到 PropertiesDock
    if (ui && ui->infoDock) {
        ui->infoDock->hide();
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
        // 鼠标移动 - 像素信息 + 笔刷光标位置 + 涂抹
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QPointF sp = scenePt(me);
            if (m_infoPanel) m_infoPanel->updatePixel(m_current, QPoint(int(sp.x()), int(sp.y())));

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
