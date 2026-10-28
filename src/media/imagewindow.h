#ifndef IMAGEWINDOW_H
#define IMAGEWINDOW_H

#include <QWidget>
#include <QPointer>
#include <QPointF>
#include <QColor>
#include <QString>
#include <QRectF>
#include <QList>

#include <atomic>
#include <cstdint>
#include <memory>

#include <opencv2/core.hpp>

#include <QMainWindow>
#include <QToolBar>
#include <QDockWidget>
#include <QUndoStack>
#include <QGraphicsRectItem>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>

#include "imageworker/layers/LayerStack.h"
#include "imageworker/layers/SmartObjectWatcher.h"
// LayerPanel accessor 返回 raw pointer, m_layerPanel unique_ptr 需要完整定义
#include "imageworker/layers/LayerPanel.h"

// 接 ThreadPool v2 异步化入口
//   EngineContext 是 src/core/ThreadPool/ 提供的 6 池 + GPU serial 包装
//   配 Profile::ImageEditor = 5 池预设 (interactive / background / io / ai / offline)
//   imageWindow 用 background().submit(...) 把 OpenCV pipeline 挪到后台
//   拿 Task::Fn 签名 (TaskContext&) - 不用 future, 回调 QMetaObject::invokeMethod 主线程刷显示
#include "../core/ThreadPool/engine_context.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ImageWindow; }
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsProxyWidget;
class QPainter;
class QStyleOptionGraphicsItem;
class QGraphicsSceneMouseEvent;
class QFocusEvent;
QT_END_NAMESPACE

class GraphicsTextItem;
#include "imagewindow/ImageCanvas.h"
// ImageIOController full include (saveAsPublic / savePublic
//   inline at line 145-146 调 m_io->onSaveAs / m_io->onSave, 需要完整定义)
//   跟 ImageCanvas 同等地位, 组件全量 include
#include "imagewindow/ImageIOController.h"
// AdjustmentPanel forward decl (新组件, 5 tab 色彩调整 UI)
//   在 imagewindow.cpp 调 applyCurrentTab / pushUndoCommand, 用 forward decl 即可
class AdjustmentPanel;
class MosaicTool;
class TextOverlayController;
// OptionPanel 完整 include (unique_ptr 需要完整类型, 析构能正确 delete)
// P5.1: MosaicOptionPanel + TextOptionPanel include removed (moved to ImageOptionBar).
// RightPanelDock forward decl (右侧 panel 1 widget 4+1 tab)
namespace docks { class RightPanelDock; }
namespace mediators { class WorkspaceMediator; class DialogMediator; }
namespace dialogs { class AdjustDialogBase; }
// ToolMediator full include needed - m_toolMed member
//   requires complete type for std::unique_ptr<mediators::ToolMediator>.
//   WorkspaceMediator stays forward decl (only used by attachWorkspaceMed).
#include "mediators/ToolMediator.h"
// ToolContext forward decl (state machine for tools)
namespace tools { class ToolContext; }
// LeftToolBar + ImageOptionBar 实装接入 (之前 follow-up
//   写完 cpp/h/ui 但 ctor 从没创建 — 整个图片阶段做完, 左侧 panel 只有 imagewindow.ui
//   硬编码 groupMosaic + groupText 空 group). 真实 include 进来用, 不 forward decl
#include "tools/AppBar.h"  // P7.6.x v3: AppBar 在 MainWindow 顶层, ImageWindow 仍持有 ScreenMode/Capture 引用
// TopOptionBar 已删 - 6 控件 (mode/opacity/flow/smoothing/angle/size)
//   搬到 ImageOptionBar 内的 BrushOptionsPanel (cmbBlendMode + 5 重叠字段).
//   撮合 BrushOptionsPanel <-> ToolContext.brushSharedState 在 ImageOptionBar::attach() 里.
//   P7.6.x v3 commit 1: LeftToolBar + ImageOptionBar + ModeSelectorBar + LeftSidePanel +
//   BrushOptionsPanel 全部从 ImageWindow 搬到 MainWindow. ImageWindow 不再 #include 它们.
// SelectionModel forward decl (selection state holder)
namespace selection { class SelectionModel; }
// FilterStrategy forward decl (20 滤镜 PS 同款)
//   改成全量 include — previewFilter/applyFilterWithStrategy
//   接口签名需要 filter::FilterStrategy* (完整类型, 包括 kind() / apply())
#include "filters/FilterStrategy.h"
// TransformBox forward decl (8 handle 自由变换)
namespace transform { class TransformBox; }

// =============================================================
// TextItemCommand — 文字 item 的增/删/改/移动 撤销命令
//   主流做法: 文字作为 scene item, 撤销栈只记录 item 操作 (不记录像素)
//   写盘时文字走一次性合成, 文档里的 item 不清空
// =============================================================
class TextItemCommand : public QUndoCommand
{
public:
    enum Op { Add, Remove, Change, Move, Rotate };

    // Add/Remove: text = 当前文本 (用于 undo 恢复)
    // Change: oldText / newText 是文字前后
    // Move: text 不需要
    // Rotate: text 不需要
    TextItemCommand(class ImageWindow *w, GraphicsTextItem *item, Op op,
                    const QString &oldText, const QString &newText = QString(),
                    const QPointF &oldPos = QPointF(), const QPointF &newPos = QPointF(),
                    qreal oldRot = 0, qreal newRot = 0,
                    QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<ImageWindow>     m_w;
    QPointer<GraphicsTextItem> m_item;
    Op          m_op;
    QString     m_oldText;   // Change: 旧文字; Add/Remove: 当前文字
    QString     m_newText;   // Change: 新文字 (冗余, 但方便)
    QPointF     m_oldPos;
    QPointF     m_newPos;
    qreal       m_oldRot   = 0;
    qreal       m_newRot   = 0;
    bool        m_firstRedo = true;   // 区分 push 时的第一次 redo
};

// 一个编辑命令: 记录 before/after 状态字符串, 描述"调了哪个参数"
class ImageEditCommand : public QUndoCommand
{
public:
    // 描述"参数名=值" 的内部状态
    struct ParamSet {
        bool toggleGray = false;
        bool toggleInvert = false;
        bool toggleBinary = false;
        bool toggleSharpen = false;
        bool toggleEdge = false;
        //  显式加 toggleBlur
        //   之前 blur 无条件跑 (默认 ksize=5), 用户看到"灰度 + 5×5 模糊"叠加
        //   修法: blur 必须显式开启, 默认 false (跟 gray/binary/sharpen 一致)
        bool toggleBlur  = false;
        int  alphaPct   = 100;
        int  beta       = 0;
        int  binaryThresh = 128;
        int  ksizeBlur  = 5;
        int  sharpenAmt = 1;
        int  cannyLow   = 80;
        int  cannyHigh  = 180;
        // 新增: 颜色/曝光 (slider 0-200 / 0-100, 100 = 中性)
        int  satPct     = 100;   // 0..200, 100=原图
        int  hueShift   = 0;     // -180..180
        int  exposurePct = 0;    // -100..100, 0=中性
    };

    ImageEditCommand(class ImageWindow *w, const ParamSet &before, const ParamSet &after,
                     const QString &text, QUndoCommand *parent = nullptr);
    // 第二个构造: 用于马赛克涂抹 in-place 改 m_current 的操作
    // imageBefore/imageAfter 分别是操作前/后 m_current 的完整快照
    ImageEditCommand(class ImageWindow *w, const cv::Mat &imageBefore, const cv::Mat &imageAfter,
                     const QString &text, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    // 用 QPointer 而非 raw pointer, ImageWindow 销毁时 m_w 自动变 null
    // 避免 undo/redo 时访问已销毁对象触发 "class destructor may have already run" ASSERT
    QPointer<ImageWindow> m_w;
    ParamSet    m_before;
    ParamSet    m_after;
    cv::Mat     m_imgBefore;  // 图像快照 (mosaic 用)
    cv::Mat     m_imgAfter;
    bool        m_useImage = false;
};

class ImageWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ImageWindow(QWidget *parent = nullptr);
    ~ImageWindow() override;

    bool loadFile(const QString &path, QString *err = nullptr);

    // filePath 加防御性检查
    //   之前: return m_filePath; 在 m_filePath 字段内存被踩时崩 (tst_ImageWorker
    //         7 层 QFileInfo::filePath 递归就是这种情况, 实际在 QFileInfo::fileName
    //         内部访问野指针 m_filePath.d)
    //   现在: 返回前 m_filePath.isEmpty 检查 + d 指针非空, 万一 m_filePath 字段
    //         损坏, 返空 QString 而不是崩. 业务上 ImageWorkspace::filePath
    //         (P0-1.3 已经 QString() 强拷贝) 拿到空串跟 "未加载文件" 一致.
    //   调试: qWarning 打印可疑情况, 便于后续查 m_filePath 被谁踩
    QString filePath() const;
    bool    isDirty()  const;
    QString displayTitle() const;

    // 应用一个参数集 (UI 改 / undo / redo 共用)
    void applyParams(const ImageEditCommand::ParamSet &p, bool repaint);

    // 公开保存接口: 给 MainWindow 的 onTabCloseRequested 调用
    // 内部委托给 m_io (ImageIOController), 不再调 onSaveAs/onSave
    //   (这 4 个 IO slot 已搬到 ImageIOController 实现)
    void saveAsPublic() { if (m_io) m_io->onSaveAs(); }
    void savePublic()   { if (m_io) m_io->onSave(); }
    // 多格式导出 (走 ExportDialog)
    void exportPublic() { if (m_io) m_io->onExport(); }

    // 公开 undo/redo: 给 MainWindow 的菜单栏 / 快捷键 (Ctrl+Z / Ctrl+Y) 转发
    void undoPublic() { onUndo(); }
    void redoPublic() { onRedo(); }

    // 公开: 给 ImageEditCommand (m_useImage 模式) 用 - 还原 m_current 到指定 Mat
    // m_w 已经在 ImageEditCommand 里检查过 alive, 这里直接用
    void replaceCurrentImage(const cv::Mat &img);

    // 公开: 给 TextItemCommand 用 - 文字 item 增/删/改
    // 内部委托给 m_textCtrl (TextOverlayController)
    void registerTextItem(GraphicsTextItem *item);
    void unregisterTextItem(GraphicsTextItem *item);
    // 公开: 连接文字 item 的 editingFinished + transformFinished signal
    //   给 createTextItem 和 TextItemCommand::Add.redo 都调
    //   (避免 redo 重建 item 后没 connect, 后续编辑/拖动/旋转没撤销栈)
    // 内部委托给 m_textCtrl
    void connectTextItemSignals(GraphicsTextItem *item);
    QList<GraphicsTextItem*> textItems() const;

    // 公开: 创建文字 item (scene 坐标) - 给 eventFilter 双击空白处调用
    // 内部委托给 m_textCtrl
    GraphicsTextItem *createTextItem(const QPointF &scenePos);

    // TransformCommand::redo/undo 调用
    //   接受变换后 cv::Mat, 同步 m_current + 可选 box 状态 (4 mode 切换时)
    void applyTransformImage(const cv::Mat& img, const transform::TransformBox* newBox = nullptr);

    // 给 TextOverlayController / MosaicTool 用
    //  返回/接收 cv::Mat& (mutable 引用, 涂抹场景下 in-place 修改)
    cv::Mat&       currentImage()       { return m_current; }
    const cv::Mat& currentImage() const { return m_current; }
    cv::Mat&       originalImage()       { return m_original; }
    const cv::Mat& originalImage() const { return m_original; }

    // AdjustmentPanel 公开访问 (供 MainWindow 等)
    class AdjustmentPanel* adjustmentPanel() { return m_adjustmentPanel; }
    // Per-window DialogMediator access
    class mediators::DialogMediator* dialogMediator() { return m_dialogMed.get(); }
    // Convenience to pop a standalone adjust
    //   dialog via the per-window DialogMediator. MainWindow menu
    //   actions call this with dialogId in {"Curves","Levels","B&W",
    //   "ChannelMixer"}.
    void showAdjustDialog(const QString& dialogId);
    // 公开 (MosaicTool 调, 触发 refreshAll)
    //   主流做法: 内部 clone m_current, 同步 base layer, invalidate cache, refreshAll
    void setCurrentImage(const cv::Mat &img);

    // 给 TextOverlayController 用
    //   sceneRef: 组件 addItem/removeItem 用
    //   statusBarRef: 组件 statusBar()->showMessage 用
    // 转发到 m_canvas
    QGraphicsScene& sceneRef() { return *m_canvas->scene(); }
    QStatusBar*     statusBarRef() { return statusBar(); }

    // 给 MosaicTool 用
    //   textOverlay(): 锁/解锁文字 item 双击编辑 (setMosaicMode)
    //   brushCursor(): brush cursor 控件 (P0-1.2 这一刻**不动**控件位置)
    //   renderToViewPublic(): 涂抹 in-place 修改后触发重绘
    class TextOverlayController* textOverlay() { return m_textCtrl.get(); }
    // 公开 MosaicTool / RightPanelDock 给 MainWindow 用
    //   docks 搬出 ImageWindow 后, MainWindow::bindLeftDockWidgets 通过这些拿当前
    //   image 的组件. raw pointer, 仅在 ImageWindow 还活着时有效 (Qt 父对象关系保证).
    class MosaicTool* mosaicTool() { return m_mosaicTool.get(); }
    class docks::RightPanelDock* colorDock() { return m_rightDock; }
    // returns raw pointer. Safe to use only when checked
    //   immediately before dereferencing (see MosaicTool::setSize pattern).
    //   The pointer becomes dangling when ImageCanvas->scene() deletes the
    //   ellipse item, but ~ImageWindow resets m_brushCursor to nullptr
    //   BEFORE delete ui (see MosaicTool::setSize comment for the bug
    //   history; originally we tried QPointer<QObject> + reinterpret_cast but
    //   the multi-level inheritance defeated Qt 6.11's QPointer<T>).
    QGraphicsEllipseItem* brushCursor() { return m_brushCursor; }
    void renderToViewPublic() { renderToView(); }

    //  作废渲染缓存 + 重绘画布.
    //  LayerCommand 在 undo/redo 末尾统一调这个: 撤销/重做改的是图层状态,
    //  画布必须立刻跟着重画, 否则用户看到的是上一帧.
    void refreshCanvasNow() { invalidateCurrentCache(); renderToViewPublic(); }

    //  给 LayerPanel 调 (MainWindow 通过这些访问 layerStack + undoStack)
    layers::LayerStack* layerStack() const { return m_layerStack.get(); }
    int currentLayerIndex() const;   // 当前活动图层 idx for "转换为智能滤镜"

    // 图层 → 蒙版 submenu 4 action (启用/禁用, 删除, 应用, 反相)
    //   调 layerStack->setMaskEnabled/clearMaskFull/applyMask/setMaskInvert.
    //   成功 true; 失败 (无 stack / 无活动图层 / 该图层无 mask) 返 false.
    bool toggleLayerMask();
    bool removeLayerMask();
    bool applyLayerMask();
    bool invertLayerMask();
    bool hasLayerMask() const;

    // 滤镜 → 转换为智能滤镜 走 ImageWindow 实现
    //   流程: convertToSmartObject(idx) + appendSmartFilter(filterKind) +
    //         LayerCommand 双 push 进 undoStack.
    //   成功返回 true, 失败 (无 stack / 离线活动图层 / SmartObject 转换失败) 返 false.
    bool convertCurrentLayerToSmartObjectWithFilter(filter::FilterKind kind);

    // 注入 WorkspaceMediator (mainwindow 创建 imagewindow 后调一次)
    void attachWorkspaceMed(mediators::WorkspaceMediator* wsMed);
    QUndoStack*        undoStack() const { return m_undoStack; }

    // 右侧 panel 显隐 + 状态查询 (mainwindow 视图菜单 toggle)
    //   之前: setGeometry 浮动覆盖 + 没有 toggle 入口, 用户没法隐藏 dock
    //   现在: QDockWidget 容器, MainWindow "视图 > 右侧面板" 调 setRightPanelVisible
    //         关闭 dock 后 QAction 仍在菜单栏 (菜单不会随 dock 消失)
    void setRightPanelVisible(bool visible);
    bool isRightPanelVisible() const;

    // 滤镜应用 (主菜单 4 action 接真)
    //   调 FilterFactory + strategy + push FilterCommand
    void applyFilter(filter::FilterKind kind);

    // 滤镜 Apply 实时预览接口 (FilterDialog 调)
    //   previewFilter: 用 strategy 当前参数应用到 m_current, 存到 m_previewImage 临时层
    //                  renderToView 会优先用 m_previewImage 显示, 直到 clearPreview
    //   clearPreview: 释放 m_previewImage, 恢复 m_current 显示
    // applyFilterWithStrategy: 用 caller 提供的 strategy
    //                  (含用户在 dialog 里调好的参数) 走 FilterCommand 入撤销栈
    //                  替换之前的 applyFilter(kind) — kind 模式不再需要
    void previewFilter(filter::FilterStrategy* strategy);
    void clearPreview();
    void applyFilterWithStrategy(filter::FilterStrategy* strategy, const QString& text);

    // ToolContext accessor (eventFilter forwards to current tool via this)
    //   m_ctx is owned by ImageWindow (constructed in ctor, no need to inject from outside)
    tools::ToolContext* toolContext() const { return m_ctx.get(); }

    // ToolMediator accessor
    //   MainWindow menu actions (e.g. onSwitchToMaskBrush) call
    //   img->toolMediator()->switchTool(ToolId::MaskBrush) to drive the
    //   tool state machine. m_toolMed is owned by ImageWindow and parented
    //   to it so lifetime follows ImageWindow destruction.
    mediators::ToolMediator* toolMediator() const { return m_toolMed.get(); }

    // Layer-as-QImage accessor
    //   ColorRange algorithm takes a QImage input; this converts a layer's
    //   cv::Mat BGR payload to QImage::Format_RGB888 for it. Returns an
    //   empty QImage if the layer / stack is invalid or the image empty.
    QImage layerStackAsQImage(int index) const;

    // ImageCanvas accessor (Text 工具 hitTest scenePos 用)
    //   m_canvas 暴露给 tools::Text 走 QGraphicsScene::itemAt 找 GraphicsTextItem
    ImageCanvas* imageCanvas() const { return m_canvas; }

    // Selection model accessor (for selection tools + main menu actions)
    selection::SelectionModel* selectionModel() const { return m_selection.get(); }
    // P0-4: Convert m_current (cv::Mat) to QImage for selection strategies (BGRA 8888)
    QImage currentImageAsQImage() const;

    // ImageIOController 代理方法 (friend class 触发对应 signal)
    //   setFilePath: 写 m_filePath + emit filePathChanged (替换 onSaveAs 内联的赋值+emit)
    //   emitEditTimeShouldUpdate: 转发 edit time 更新事件 (替换 onSave/onSaveAs 内联的 emit)
    //   emitCloseRequested: 转发 close request (替换 onClose 内联的 emit)
    //   markSaved: 已经在 private: 里 (P0-1.0), friend class 直接调, 不再 inline 包装
    void setFilePath(const QString &path) { m_filePath = path; emit filePathChanged(m_filePath); }
    void emitEditTimeShouldUpdate() { emit editTimeShouldUpdate(m_filePath); }
    void emitCloseRequested() { emit closeRequested(); }

    // SmartObject source file watcher
    //   Tracks all SmartObject layer source files for external modification.
    //   MainWindow slots call rewatchAll() after Convert / Rasterize / Relink
    //   to keep watch state in sync with the LayerStack. Returns nullptr until
    //   loadFile() has run.
    layers::SmartObjectWatcher* smartObjectWatcher() const { return m_smartWatcher.get(); }

    // LayerPanel 实例化 + 注入 LayersDock tab 0
    //   P1.4.2 留了 LayerPanel 没 new 的 bug, 这里补上. LayerPanel::ctor 接受 raw
    //   LayerStack* (parent = this), 装到 LayersDock tab 0 后所有权转移到 LayersDock
    //   (LayersDock::setContentWidget 调用 w->setParent(tab) 后 LayersDock 负责析构).
    //   这里 unique_ptr 仅在调用 setContentWidget 前有效, 调用 .release() 后置空,
    //   避免重复 delete.
    layers::LayerPanel* layerPanel() const { return m_layerPanel; }

    // selected layer index from LayerPanel's QTreeWidget
    //   current row (or -1 if none selected). Used by MainWindow layer menu
    //   actions (Duplicate / Delete / MoveUp / MoveDown / Merge Down) to know
    //   which layer the action targets.
    int selectedLayerIndex() const;

    // layer menu operation enum + helper
    //   Encapsulates the layerStack + undoStack + invalidateCurrentCache
    //   + renderToView sequence that MainWindow menu actions need.
    //   Returns true if operation was applied, false if no selection / no stack.
    enum class LayerOp { NewBitmap, Duplicate, Remove, MoveUp, MoveDown,
                         MergeDown, FlattenVisible };
    bool applyLayerOp(LayerOp op);

    // EngineContext 公开访问 (供 ImageAdjustmentPanel 等组件用)
    //   raw pointer - lifetime 由 ImageWindow 持有 unique_ptr<m_engine> 保证
    //   ImageWindow 总是比组件晚析构 (QObject parent-child 关系), 不会悬空
    vistella::tp::EngineContext* engineContext() { return m_engine.get(); }
    std::atomic<uint64_t>& currentTaskId() { return m_currentTaskId; }
    bool asyncEnabled() const { return m_asyncEnabled; }
    void setAsyncEnabled(bool v) { m_asyncEnabled = v; }

signals:
    void dirtyChanged(bool dirty);
    void filePathChanged(const QString &path);
    void editTimeShouldUpdate(const QString &path);
    void closeRequested();   // 自身要求关闭 (例如工具栏"关闭"按钮或 Ctrl+W)
    // Quick Mask UI 同步 — AppBar 在 MainWindow 顶层, ImageWindow
    //   不能直接改它. emit 信号, MainWindow 在 bindLeftDockWidgets() 里 connect 到 AppBar.
    void quickMaskUiSync(bool on);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    // 图像变换 6 槽 (MainWindow 6 menu action 调用)
    void onFreeTransform();
    void onImageFlipH();
    void onImageFlipV();
    void onImageRotate90CW();
    void onImageRotate90CCW();
    void onImageRotate180();

    // onFitWindow 提升到 public slots (MainWindow 图像 > 全部显示
    //   "Reveal All" 菜单触发, 跟 PS 行为一致 — fit 缩放)
    void onFitWindow();

    // P0-6.6: 应用 QTransform 矩阵到当前 image, 生成 TransformCommand 推 undoStack
    //   (PS 风格通用入口, 4 mode + 翻转 + 旋转都走这个)
    void applyImageTransform(const QTransform& t, const QString& text);

    // non-destructive SmartObject transform
    //   Called by MainWindow (主菜单) and LayerPanel (右键) to set a SmartObject
    //   layer's QTransform via LayerStack::setSmartObjectTransform and push a
    //   LayerCommand::makeSetSmartObjectTransform undo entry. Identity transform
    //   clears hasTransform (matches PS semantics).
    void applySmartObjectTransform(int idx, const QTransform& t);

    // AppBar 3 个 slot (Quick Mask / Screen Mode / Capture)
    //   Quick Mask 切换 MaskBrush 工具; Screen Mode 切窗口状态; Capture 走 QScreen 截图保存 PNG.
    void onQuickMaskToggled(bool on);
    void onScreenModeSelected(int mode);
    void onCaptureRequested(int kind);  // tools::AppBar::CaptureKind
    // Internal helper used by onScreenModeSelected.
    void applyScreenMode(int mode);


    // dock routing slots for RightPanelDock connectors
    //   Color fg/bg sync to Brush paint color, selection save/load to cv::Mat,
    //   Quick Mask three-way sync (AppBar <-> ChannelPathPanel <-> ToolMediator).
    void onForegroundColorChanged(const QColor& c);
    void onSaveSelectionRequested();
    void onLoadSelectionRequested();
    void onChannelPathQuickMaskToggled(bool on);
    void syncQuickMaskUi(bool on);


    //   Originally private; promoted to public slot so MainWindow::aTextColor
    //   connect can invoke it without friend declaration.
    void onTextColorClicked();

public slots:
    // 提升到 public slots — MainWindow::onZoomIn/Out 调
    //   activeImageWindow()->onZoomIn/Out 真实缩放, 不再只 show statusBar 文字.
    void onZoomIn();
    void onZoomOut();

private slots:
    // 工具栏 actions
    // onSave / onSaveAs / onOpen / onClose 搬到 ImageIOController
    //   这 4 个 slot 不再连接任何 signal (buildActions 里就只接了 zoom),
    //   仅作 saveAsPublic / savePublic 转发目标, 转发也搬到 m_io->onSaveAs / m_io->onSave
    void onResetZoom();

    // 撤销/重做
    void onUndo();
    void onRedo();

    // 文字功能: 双击图片空白/已有文字直接进入编辑, 无需按钮
    // 马赛克模式切换 (按钮) - 进马赛克时禁止双击进入文字编辑, 退马赛克时恢复
    void onMosaicModeToggled(bool on);
    // 文字 font / size 改变 -> 应用到选中/正在编辑的 item
    void onTextFontChanged();
    void onTextSizeChanged(int v);

    // 马赛克笔刷大小变化 -> 刷新圆圈光标
    void onMosaicSizeChanged(int v);

private:
    void buildActions();
        void renderToView();
    void refreshAll();

    // 标记一个"已保存"的栈位置, 用于 isDirty 判断
    void markSaved();
    void onStackChanged();

    // 给当前正在编辑的 item 应用字体/字号/颜色 (从左面板)
    void applyTextStyleToCurrent();

    Ui::ImageWindow     *ui;

    // ---- P0-1: 5 component slots. nullptr in P0-1.1; P0-1.2 instantiates
    // and migrates methods from this class. Each component holds a
    // QPointer<ImageWindow> m_host back to this object.
    //
    // smart pointer policy
    //   - QWidget/QObject added to Qt tree (parent-managed): raw pointer
    //   - QObject without Qt parent, single owner: unique_ptr
    //   - Multi-owner: shared_ptr + weak_ptr
    //   - Pure C++ class: unique_ptr or shared_ptr per usage
    //   This rule applies project-wide (image/audio/video/visual modules).
    //   Qt's parent-child ownership is the canonical lifetime owner for
    //   widgets; std::unique_ptr on top of that creates double ownership
    //   and forces the awkward release() + saved-raw-pointer dance.

    // ImageCanvas is the central widget — Qt owns it via setCentralWidget.
    ImageCanvas                            *m_canvas = nullptr;          // P0-1.2
    // MosaicTool / TextOverlayController / ImageIOController: not added to
    // Qt tree (no parent) and only referenced by ImageWindow. unique_ptr OK.
    std::unique_ptr<MosaicTool>             m_mosaicTool;                // P0-1.2
    std::unique_ptr<TextOverlayController>  m_textCtrl;                  // P0-1.2
    std::unique_ptr<ImageIOController>      m_io;                        // P0-1.2
    // PS 风格色彩调整 UI. Parent = m_rightDock's tabWidget
    //   (set via RightPanelDock::setAdjustmentPanel). Qt owns lifetime.
    AdjustmentPanel                        *m_adjustmentPanel = nullptr;  // P0-3.2
    std::unique_ptr<mediators::DialogMediator> m_dialogMed;                 

    // 右侧 panel. Parent = m_rightDockContainer
    //   (set via setWidget()). Qt owns lifetime.
    docks::RightPanelDock                  *m_rightDock = nullptr;
    QDockWidget                            *m_rightDockContainer = nullptr;

    // LeftSidePanel + LeftDockContainer + 4 子 widget + BrushOptionsPanel
    //   全部搬出 ImageWindow, 移到 MainWindow. ImageWindow 现在只持画布 + 业务数据 + 右侧 dock
    //   (P7.6.x v3 commit 1 范围: 只搬 left 侧; 右侧 dock 在 commit 5+ 处理). m_leftSidePanel
    //   / m_leftDockContainer / m_leftToolBar / m_imageOptionBar / m_modeSelectorBar / m_appBar
    //   / m_brushOpt 成员删除; MainWindow 接管.
    //
    //   ImageWindow 现在不需要创建 dock, 但它仍然创建 rightDock / layerPanel / adjustmentPanel
    //   (右侧 dock 留到下一 commit 再搬, 因为它跟 ImageWindow 的 layer/channel/path state 有
    //   大量 signal connect, 搬动需要重新规划 setHost / setContentWidget / setAdjustmentPanel
    //   等 API).
    // Quick Mask mode: when on, MaskBrush paint mode is "switch into" until toggled off;
    //   stores the previously active tool so onQuickMaskToggled(false) restores it.
    bool                                   m_quickMaskActive = false;
    mediators::ToolId                      m_quickMaskSavedToolId = mediators::ToolId::None;
    // saved selection (cv::Mat 1-channel mask 0/255). Persists
    //   across sessions of the same ImageWindow; null until user clicks
    //   "保存选区..." in the ChannelPathPanel.
    std::shared_ptr<cv::Mat>               m_pSavedSelection;

    // MosaicTool + TextOverlayController 专属属性面板
    //   之前 imagewindow.ui 硬编码 groupMosaic + groupText (空 group)
    //   现在升级成独立 OptionPanel widget, 装到左侧 dock 下方 (splitDockWidget
    //   跟在 ImageOptionBar 后面). 用户可拖动调整顺序/位置 (NoDockWidgetFeatures
    //   隐藏 close/float 按钮, 跟左侧其他 dock 一致).
    // MosaicOptionPanel + TextOptionPanel no longer owned
    //   by ImageWindow. They are owned by ImageOptionBar (created lazily inside
    //   ImageOptionBar::ensureMosaicPage / ensureTextPage) and follow the active
    //   tool like every other option panel. The previous Q4.2.2.1 leftPanel
    //   gridLayout_4 hosting kept them always-on; the new routing only shows
    //   them when the matching tool is active (Brush for Mosaic, Text for Text).

    // Tool state context — owns current ToolState, receives eventFilter forwards
    std::unique_ptr<tools::ToolContext>     m_ctx;

    // ToolMediator owned per-ImageWindow (1 个 per image)
    //   Parent = this, so destruction follows ImageWindow. Mediator signal
    //   is wired to m_ctx by m_ctx->attach(this, m_toolMed.get()) below.
    std::unique_ptr<mediators::ToolMediator> m_toolMed;

    // Selection state (mask + bbox + mode). Owned by ImageWindow
    std::unique_ptr<selection::SelectionModel> m_selection;

    cv::Mat   m_original;
    cv::Mat   m_current;
    QString   m_filePath;
    bool      m_ctrlDown = false;

    //  图层系统
    //   m_layerStack 管理多图层 (Bitmap 完整, 其他 4 种 LayerKind 暂用 Bitmap 实现)
    //   m_current = m_layerStack->render() 缓存, 标记 dirty 时重算
    std::unique_ptr<layers::LayerStack> m_layerStack;
    bool    m_currentDirty = true;
    void    invalidateCurrentCache() { m_currentDirty = true; }
    void    rebuildCurrentCache();
    //  LayerPanel 切图层时通知
    //   当前不直接改渲染 (切 layer 不改像素), 但触发重绘让激活层指示器更新
    //   也给 MainWindow 一个挂钩点 (e.g. 切到 Adjustment 时自动展开属性面板)
    void onLayerSelectionChanged(int index);

    // SmartObject file watcher
    //   lifetime tied to ImageWindow; created in loadFile() after m_layerStack.
    //   receives layerAdded/Removed/Changed signals to keep watch state in sync
    //   with the stack; emits sourceFileChanged(idx, path) -> statusBar hint.
    std::unique_ptr<layers::SmartObjectWatcher> m_smartWatcher;

    // LayerPanel — reparented into LayersDock via
    //   setContentWidget. Qt owns lifetime; raw pointer survives.
    layers::LayerPanel                     *m_layerPanel = nullptr;

    QUndoStack *m_undoStack = nullptr;
    int         m_savedIndex = 0;   // 上次保存时的栈 index, 用于 isDirty

    // ===== ThreadPool v2 异步化 =====
    //   m_engine 在 ctor 实例化, Profile::ImageEditor 限 2 线程 (避免系统调度问题)
    //   m_currentTaskId 原子递增, 给每个 async applyCurrentParams 任务一个 id
    //     旧 task 完成时主线程回调发现 id != current, 直接 return, 不刷显示
    //     这就是"取消 in-flight task"的标准做法 (P0-2 一次性到位设计原则)
    std::unique_ptr<vistella::tp::EngineContext>  m_engine;
    std::atomic<uint64_t>                        m_currentTaskId{0};
    bool                                         m_asyncEnabled = true;

    // ===== 交互式马赛克 (状态迁到 MosaicTool) =====
    //   涂抹模式开关 / 涂抹开始时的 snapshot / 4 种类型 enum 都搬到 m_mosaicTool
    //   这里只保留 m_brushCursor 控件 (P0-1.2 这一轮**不动** brush cursor 控件位置)
    // raw pointer with explicit reset in ~ImageWindow
    //   before delete ui. Original raw pointer caused SEGFAULT in MosaicTool::setSize
    //   because ImageCanvas->scene() destructor deletes the ellipse item but
    //   m_brushCursor field doesn't reset — subsequent access (especially
    //   bc->scene() dereference) was a use-after-free.
    //
    //   Qt 6.11's QPointer<T> doesn't work for multi-level inheritance like
    //   QObject->QGraphicsItem->QGraphicsEllipseItem (static_cast<T*>(QObject*)
    //   fails across the QGraphicsItem boundary). So we explicitly reset
    //   m_brushCursor = nullptr at the START of ~ImageWindow, before delete ui
    //   destroys m_canvas (and its scene). See imagewindow.cpp ~ImageWindow
    //   for the reset.
    QGraphicsEllipseItem *m_brushCursor = nullptr;

    friend class ImageEditCommand;
    friend class TextItemCommand;
    // 组件需要直接访问 m_undoStack
    friend class TextOverlayController;
    friend class MosaicTool;
    // 组件需要 m_original / m_current / m_inUndoRedo / ui
    friend class ImageCanvas;
    // ImageIOController 需要调 markSaved (private)
    friend class ImageIOController;
};

#endif // IMAGEWINDOW_H
