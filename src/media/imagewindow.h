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
#include <QUndoStack>
#include <QGraphicsRectItem>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>

#include "imageworker/layers/LayerStack.h"

// P0-2 (2026-09-08): 接 ThreadPool v2 异步化入口
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

class ImageInfoPanel;
class GraphicsTextItem;
#include "imagewindow/ImageCanvas.h"
// P0-1.4 (2026-09-07): ImageIOController full include (saveAsPublic / savePublic
//   inline at line 145-146 调 m_io->onSaveAs / m_io->onSave, 需要完整定义)
//   跟 ImageCanvas 同等地位, 组件全量 include
#include "imagewindow/ImageIOController.h"
class ImageAdjustmentPanel;
// P0-3.2 (2026-09-08): AdjustmentPanel forward decl (新组件, 5 tab 色彩调整 UI)
//   在 imagewindow.cpp 调 applyCurrentTab / pushUndoCommand, 用 forward decl 即可
class AdjustmentPanel;
class MosaicTool;
class TextOverlayController;
// F-G.3 Fix (2026-09-10): RightPanelDock forward decl (右侧 panel 1 widget 4+1 tab)
namespace docks { class RightPanelDock; }
namespace mediators { class WorkspaceMediator; }
// F-N (2026-09-10): ToolContext forward decl (state machine for tools)
namespace tools { class ToolContext; }
// P0-4 (2026-09-10): SelectionModel forward decl (selection state holder)
namespace selection { class SelectionModel; }
// P0-5 (2026-09-10): FilterKind forward decl (20 滤镜 PS 同款)
namespace filter { enum class FilterKind : int; }
// P0-6.6 (2026-09-14): TransformBox forward decl (8 handle 自由变换)
namespace transform { class TransformBox; }

// =============================================================
// TextItemCommand — 文字 item 的增/删/改/移动 撤销命令
//   主流做法: 文字作为 scene item, 撤销栈只记录 item 操作 (不记录像素)
//   烧图 (flattenText) 不入栈, 烧图后 m_textItems 清空
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
        // 阶段 1 Step A Bug 2 (2026-09-04): 显式加 toggleBlur
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

    // P0-1.2 fix (2026-09-08): filePath() 加防御性检查
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
    // P0-1.4 (2026-09-07): 内部委托给 m_io (ImageIOController), 不再调 onSaveAs/onSave
    //   (这 4 个 IO slot 已搬到 ImageIOController 实现)
    void saveAsPublic() { if (m_io) m_io->onSaveAs(); }
    void savePublic()   { if (m_io) m_io->onSave(); }

    // 公开 undo/redo: 给 MainWindow 的菜单栏 / 快捷键 (Ctrl+Z / Ctrl+Y) 转发
    void undoPublic() { onUndo(); }
    void redoPublic() { onRedo(); }

    // 公开: 给 ImageEditCommand (m_useImage 模式) 用 - 还原 m_current 到指定 Mat
    // m_w 已经在 ImageEditCommand 里检查过 alive, 这里直接用
    void replaceCurrentImage(const cv::Mat &img);

    // 公开: 给 TextItemCommand 用 - 文字 item 增/删/改
    // P0-1.2 (2026-09-07): 内部委托给 m_textCtrl (TextOverlayController)
    void registerTextItem(GraphicsTextItem *item);
    void unregisterTextItem(GraphicsTextItem *item);
    // 公开: 连接文字 item 的 editingFinished + transformFinished signal
    //   给 createTextItem 和 TextItemCommand::Add.redo 都调
    //   (避免 redo 重建 item 后没 connect, 后续编辑/拖动/旋转没撤销栈)
    // P0-1.2 (2026-09-07): 内部委托给 m_textCtrl
    void connectTextItemSignals(GraphicsTextItem *item);
    QList<GraphicsTextItem*> textItems() const;

    // 公开: 创建文字 item (scene 坐标) - 给 eventFilter 双击空白处调用
    // P0-1.2 (2026-09-07): 内部委托给 m_textCtrl
    GraphicsTextItem *createTextItem(const QPointF &scenePos);

    // 公开: 扁平化所有文字 item 到 m_current (烧图) - 给 onSave 调用
    // P0-1.2 (2026-09-07): 内部委托给 m_textCtrl
    void flattenText();

    // P0-6.6 (2026-09-14): TransformCommand::redo/undo 调用
    //   接受变换后 cv::Mat, 同步 m_current + 可选 box 状态 (4 mode 切换时)
    void applyTransformImage(const cv::Mat& img, const transform::TransformBox* newBox = nullptr);

    // P0-1.2 (2026-09-07): 给 TextOverlayController / MosaicTool 用
    //  返回/接收 cv::Mat& (mutable 引用, 涂抹场景下 in-place 修改)
    cv::Mat&       currentImage()       { return m_current; }
    const cv::Mat& currentImage() const { return m_current; }
    cv::Mat&       originalImage()       { return m_original; }
    const cv::Mat& originalImage() const { return m_original; }

    // P0-3.2 (2026-09-08): AdjustmentPanel 公开访问 (供 MainWindow 等)
    class AdjustmentPanel* adjustmentPanel() { return m_adjustmentPanel.get(); }
    // P0-1.2 (2026-09-07): 公开 (MosaicTool 调, 触发 refreshAll)
    //   主流做法: 内部 clone m_current, 同步 base layer, invalidate cache, refreshAll
    void setCurrentImage(const cv::Mat &img);

    // P0-1.2 (2026-09-07): 给 TextOverlayController 用
    //   sceneRef: 组件 addItem/removeItem 用
    //   statusBarRef: 组件 statusBar()->showMessage 用
    // P0-1.3 (2026-09-07): 转发到 m_canvas
    QGraphicsScene& sceneRef() { return *m_canvas->scene(); }
    QStatusBar*     statusBarRef() { return statusBar(); }

    // P0-1.2 (2026-09-07): 给 MosaicTool 用
    //   textOverlay(): 锁/解锁文字 item 双击编辑 (setMosaicMode)
    //   brushCursor(): brush cursor 控件 (P0-1.2 这一刻**不动**控件位置)
    //   renderToViewPublic(): 涂抹 in-place 修改后触发重绘
    //   infoPanel(): push undo 后更新 info panel
    class TextOverlayController* textOverlay() { return m_textCtrl.get(); }
    QGraphicsEllipseItem* brushCursor() { return m_brushCursor; }
    ImageInfoPanel*       infoPanel()   { return m_infoPanel; }
    void renderToViewPublic() { renderToView(); }

    // 阶段 1 W4.3 Phase 1 (2026-09-04): 给 LayerPanel 调 (MainWindow 通过这些访问 layerStack + undoStack)
    layers::LayerStack* layerStack() const { return m_layerStack.get(); }

    // F-G.3 (2026-09-09): 注入 WorkspaceMediator (mainwindow 创建 imagewindow 后调一次)
    void attachWorkspaceMed(mediators::WorkspaceMediator* wsMed);
    QUndoStack*        undoStack() const { return m_undoStack; }

    // P0-5 (2026-09-10): 滤镜应用 (主菜单 4 action 接真)
    //   调 FilterFactory + strategy + push FilterCommand
    void applyFilter(filter::FilterKind kind);

    // F-N (2026-09-10): ToolContext accessor (eventFilter forwards to current tool via this)
    //   m_ctx is owned by ImageWindow (constructed in ctor, no need to inject from outside)
    tools::ToolContext* toolContext() const { return m_ctx.get(); }

    // P0-7.1 (2026-09-14): ImageCanvas accessor (Text 工具 hitTest scenePos 用)
    //   m_canvas 暴露给 tools::Text 走 QGraphicsScene::itemAt 找 GraphicsTextItem
    ImageCanvas* imageCanvas() const { return m_canvas.get(); }

    // P0-4 (2026-09-10): Selection model accessor (for selection tools + main menu actions)
    selection::SelectionModel* selectionModel() const { return m_selection.get(); }
    // P0-4: Convert m_current (cv::Mat) to QImage for selection strategies (BGRA 8888)
    QImage currentImageAsQImage() const;

    // P0-1.4 (2026-09-07): ImageIOController 代理方法 (friend class 触发对应 signal)
    //   setFilePath: 写 m_filePath + emit filePathChanged (替换 onSaveAs 内联的赋值+emit)
    //   emitEditTimeShouldUpdate: 转发 edit time 更新事件 (替换 onSave/onSaveAs 内联的 emit)
    //   emitCloseRequested: 转发 close request (替换 onClose 内联的 emit)
    //   markSaved: 已经在 private: 里 (P0-1.0), friend class 直接调, 不再 inline 包装
    void setFilePath(const QString &path) { m_filePath = path; emit filePathChanged(m_filePath); }
    void emitEditTimeShouldUpdate() { emit editTimeShouldUpdate(m_filePath); }
    void emitCloseRequested() { emit closeRequested(); }

    // P0-2 (2026-09-08): EngineContext 公开访问 (供 ImageAdjustmentPanel 等组件用)
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

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    // P0-6.6 (2026-09-14): 图像变换 6 槽 (MainWindow 6 menu action 调用)
    void onFreeTransform();
    void onImageFlipH();
    void onImageFlipV();
    void onImageRotate90CW();
    void onImageRotate90CCW();
    void onImageRotate180();

    // P0-6.6: 应用 QTransform 矩阵到当前 image, 生成 TransformCommand 推 undoStack
    //   (PS 风格通用入口, 4 mode + 翻转 + 旋转都走这个)
    void applyImageTransform(const QTransform& t, const QString& text);

private slots:
    // 工具栏 actions
    // P0-1.4 (2026-09-07): onSave / onSaveAs / onOpen / onClose 搬到 ImageIOController
    //   这 4 个 slot 不再连接任何 signal (buildActions 里就只接了 zoom),
    //   仅作 saveAsPublic / savePublic 转发目标, 转发也搬到 m_io->onSaveAs / m_io->onSave
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();
    void onResetZoom();

    // 撤销/重做
    void onUndo();
    void onRedo();

    // 文字功能: 双击图片空白/已有文字直接进入编辑, 无需按钮
    // 马赛克模式切换 (按钮) - 进马赛克时禁止双击进入文字编辑, 退马赛克时恢复
    void onMosaicModeToggled(bool on);
    // 文字颜色按钮
    void onTextColorClicked();
    // 文字 font / size 改变 -> 应用到选中/正在编辑的 item
    void onTextFontChanged();
    void onTextSizeChanged(int v);

    // 马赛克笔刷大小变化 -> 刷新圆圈光标
    void onMosaicSizeChanged(int v);

private:
    void buildActions();
    void buildInfoPanel();
    void applyPanelTheme();

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
    std::unique_ptr<ImageCanvas>            m_canvas;        // P0-1.2
    std::unique_ptr<ImageAdjustmentPanel>   m_adjustment;    // P0-1.2
    std::unique_ptr<MosaicTool>             m_mosaicTool;    // P0-1.2
    std::unique_ptr<TextOverlayController>  m_textCtrl;      // P0-1.2
    std::unique_ptr<ImageIOController>      m_io;            // P0-1.2
    // P0-3.2 (2026-09-08): PS 风格色彩调整 UI (5 tab Curves/Levels/HSL/B&W/ChannelMixer)
    //   跟 m_adjustment (旧 5 toggle + 10 slider) 共存, 各管各的
    std::unique_ptr<AdjustmentPanel>        m_adjustmentPanel;  // P0-3.2 (F-G.3: parent = m_rightDock->tabWidget(), 析构时 m_rightDock 负责 delete, unique_ptr 不持有所有权)

    // F-G.3 Fix (2026-09-10): 右侧 panel (1 个 widget 装 4 dock + 1 调整 tab)
    //   替代原来 3 个分离 dock (RightPanelStack + adjDock + infoDock) + addDockWidget 抢画布位置
    std::unique_ptr<docks::RightPanelDock> m_rightDock;

    // F-N (2026-09-10): Tool state context — owns current ToolState, receives eventFilter forwards
    std::unique_ptr<tools::ToolContext>     m_ctx;

    // P0-4 (2026-09-10): Selection state (mask + bbox + mode). Owned by ImageWindow.
    std::unique_ptr<selection::SelectionModel> m_selection;

    cv::Mat   m_original;
    cv::Mat   m_current;
    QString   m_filePath;
    bool      m_ctrlDown = false;

    // 阶段 1 W4.3 Phase 1 (2026-09-04): 图层系统
    //   m_layerStack 管理多图层 (Bitmap 完整, 其他 4 种 LayerKind 暂用 Bitmap 实现)
    //   m_current = m_layerStack->render() 缓存, 标记 dirty 时重算
    std::unique_ptr<layers::LayerStack> m_layerStack;
    bool    m_currentDirty = true;
    void    invalidateCurrentCache() { m_currentDirty = true; }
    void    rebuildCurrentCache();
    // 阶段 1 Step A Bug 1 (2026-09-04): LayerPanel 切图层时通知
    //   当前不直接改渲染 (切 layer 不改像素), 但触发重绘让激活层指示器更新
    //   也给 MainWindow 一个挂钩点 (e.g. 切到 Adjustment 时自动展开属性面板)
    void onLayerSelectionChanged(int index);

    QUndoStack *m_undoStack = nullptr;
    int         m_savedIndex = 0;   // 上次保存时的栈 index, 用于 isDirty
    bool        m_inUndoRedo = false;  // 防止 undo/redo 期间又触发 push

    ImageInfoPanel *m_infoPanel = nullptr;

    // ===== P0-2 (2026-09-08): ThreadPool v2 异步化 =====
    //   m_engine 在 ctor 实例化, Profile::ImageEditor 限 2 线程 (避免系统调度问题)
    //   m_currentTaskId 原子递增, 给每个 async applyCurrentParams 任务一个 id
    //     旧 task 完成时主线程回调发现 id != current, 直接 return, 不刷显示
    //     这就是"取消 in-flight task"的标准做法 (P0-2 一次性到位设计原则)
    std::unique_ptr<vistella::tp::EngineContext>  m_engine;
    std::atomic<uint64_t>                        m_currentTaskId{0};
    bool                                         m_asyncEnabled = true;

    // ===== 交互式马赛克 (P0-1.2 2026-09-07: 状态迁到 MosaicTool) =====
    //   涂抹模式开关 / 涂抹开始时的 snapshot / 4 种类型 enum 都搬到 m_mosaicTool
    //   这里只保留 m_brushCursor 控件 (P0-1.2 这一轮**不动** brush cursor 控件位置)
    QGraphicsEllipseItem *m_brushCursor = nullptr;  // 笔刷圆圈光标

    friend class ImageEditCommand;
    friend class TextItemCommand;
    // P0-1.2 (2026-09-07): 组件需要直接访问 m_undoStack / m_infoPanel
    friend class TextOverlayController;
    friend class MosaicTool;
    // P0-1.3 (2026-09-07): 组件需要 m_original / m_current / m_inUndoRedo / ui
    friend class ImageCanvas;
    friend class ImageAdjustmentPanel;
    // P0-1.4 (2026-09-07): ImageIOController 需要调 markSaved() (private)
    friend class ImageIOController;
};

#endif // IMAGEWINDOW_H
