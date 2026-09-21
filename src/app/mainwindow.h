#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QToolButton>
#include <QStringList>
#include <QFrame>
#include <QPushButton>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QAction;
class QActionGroup;
class QMenu;
class QPoint;
class DocWindow;
class InfoTreeDock;
class ImageWindow;
class HomePage;
namespace filter { enum class FilterKind : int; }
namespace layers { class LayerStack; }
namespace mediators { class WorkspaceMediator; }
class QMouseEvent;
QT_END_NAMESPACE

// 简单窗口模式 (2026-09-09 重建, 9/4+ 状态机工作丢失后)
// 1 个 mode 记录 Normal/Maximized, 1 个 size (m_normalSize) 记 normal 尺寸
// 不加 sanity check / fullscreenish 检测 / 复杂 fallback
enum class WindowMode : int { Normal, Maximized };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // 启动时由 main.cpp 决定恢复哪些文件 (弹窗问过用户了)
    void restoreSessionFiles(const QStringList &files, const QString &activeFile);

    // 窗口模式 (2026-09-09 简单状态机: 1 mode + 1 size)
    void setMode(WindowMode m);
    void loadWindowState();
    // Apply current m_mode through setMode(). External code (e.g. main.cpp splash timer)
    //   must NEVER call showMaximized()/showNormal() directly. Always go through
    //   this so the Normal branch's centered setGeometry runs and button text syncs.
    // P1.4.6 fix (2026-09-17): unify all 3 showMaximized/showNormal entry points.
    void applyWindowMode() { setMode(m_mode); }
    WindowMode mode() const { return m_mode; }

    // 工作区 Mediator (F-F 2026-09-09: mainwindow 临时持有, F-G 改成 imagewindow 共享)
    mediators::WorkspaceMediator* workspaceMediator() const { return m_workspaceMed.get(); }

    // ===== 标题栏扩展点 (后续加功能直接用这些容器) =====
    //   left  : logo + app name + version (极少改)
    //   mid   : 菜单条 + 后续 VS 风格全局工具 (主要扩展点)
    //   right : 模式切换 + 主题 + 用户 + 通知... + 窗控 (次要扩展点)
    QWidget* titleBarLeft()   { return m_titleLeft; }
    QWidget* titleBarMid()    { return m_titleMid; }
    QWidget* titleBarRight()  { return m_titleRight; }

protected:
    void changeEvent(QEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    // 无边框 + 自绘标题栏: 标题栏区域按下拖动 / 双击最大化
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private slots:
    // 文件
    void onNewFile();
    void onOpenFile();
    void onSaveFile();
    void onSaveAsFile();
    // P0-8.2 (2026-09-15): 多格式导出 (调 ImageIOController::onExport)
    void onExportFile();
    void onCloseCurrent();
    void onCloseAll();
    void onCloseOthers();
    void onCloseSameType();

    // 编辑
    void onUndo();
    void onRedo();
    void onCut();
    void onCopy();
    void onPaste();
    void onSelectAll();
    void onFind();
    void onReplace();

    // P0-4.7 (2026-09-10): 选择菜单 3 action (image module selection ops)
    void onImageSelectAll();
    void onImageDeselect();
    void onImageInverse();

    // 视图
    void onToggleTheme();
    void onResetLayout();
    void onZoomIn();
    void onZoomOut();
    void onSettings();

    // P0-6.5 (2026-09-14): 图像变换 6 槽
    void onFreeTransform();
    void onImageFlipH();
    void onImageFlipV();
    void onImageRotate90CW();
    void onImageRotate90CCW();
    void onImageRotate180();

    // 阶段 0 第 10 步: 主题画廊 + 登录对话框入口
    void onShowThemeGallery();
    void onShowLogin();

    // P0-5 (2026-09-10): 滤镜菜单 4 action 弹 FilterDialog
    void onFilterMenuTriggered(filter::FilterKind kind);
    // P0 leftover 5 (2026-09-21): Curves / Levels / B&W / ChannelMixer
    //   standalone dialog trigger (routes through active ImageWindow's
    //   DialogMediator).
    void onAdjustDialogTriggered(const QString& dialogId);
    // P1.2.5+6+8 (2026-09-16): 启动 Liquify 交互式液化对话框
    void onLiquifyTriggered();
    // P1.3.4/5/6/7 (2026-09-17): 图层蒙版入口
    void onSwitchToMaskBrush();          // 切到 MaskBrushTool
    void onAddPixelMaskFromSelection(); // 选区 -> 像素蒙版 (全 255)
    void onRefineMaskEdge();            // Refine Edge 对话框
    void onColorRangeMask();            // Color Range 对话框

    // P1.4.2 (2026-09-17): Smart object main menu actions (idx=-1 = use current selection)
    //   LayerPanel right-click menu emits with idx; main menu triggers pass -1.
    void onSmartObjectConvert(int idx = -1);
    void onSmartObjectRasterize(int idx = -1);
    void onSmartObjectEditSource(int idx = -1);
    void onSmartObjectRelink(int idx = -1);
    // P1.4.4 (2026-09-17): Smart filter main menu entry; shares the flow with
    //   the LayerPanel right-click "Apply Smart Filter..." handler.
    void onApplySmartFilter(int idx = -1);

    // P1.4.5 (2026-09-17): SmartObject non-destructive transform 主菜单 4 入口
    //   (idx=-1 = use current selection). onSmartObjectTransform is a chained
    //   alias for Scale + Rotate (simpler v1 UX, fancy dialog deferred to P1.4.6).
    void onSmartObjectTransform(int idx = -1);
    void onSmartObjectScale(int idx = -1);
    void onSmartObjectRotate(int idx = -1);
    void onSmartObjectResetTransform(int idx = -1);

    // P1.4.2: 4 slot 共用的 image/stack/idx 解析 (idx<0 → stack->selection())
    //   填 msg 给 statusBar 提示; 返 false 表示已发提示, 调用方直接 return
    bool resolveSmartObjectTarget(int idxIn,
                                   ImageWindow **outImg,
                                   layers::LayerStack **outStack,
                                   int *outIdx,
                                   QString *outMsg);

    // Tab
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onTabContextMenu(const QPoint &pos);

    // PS/WPS/VS 风格主页切换 (2026-09-10): HomePage 在 mainStack page 0, 工作空间在 page 1
    //   启动默认 page 0, 新建/打开切到 page 1, 关闭所有 tab 切回 page 0
    void switchToHomePage();
    void switchToWorkspace();
    void onHomeNewRequested();
    void onHomeOpenRequested();
    void onHomeOpenFolderRequested();
    void onHomeOpenPathRequested(const QString &path);
    void onHomeModuleRequested(int moduleKind);

    // Session / theme
    void onThemeChanged();

private:
    void buildActions();
    //void buildMenuCorner();      // (旧)菜单栏右上角圆角框放 建模/多媒体 — 已合并到 buildRightArea
    void buildTitleBar();         // 自绘标题栏 L2 方案: 替代 buildMenuCorner
    void buildLeftArea();         // logo + app name + version
    void buildMidArea();          // 菜单条 + 后续 VS 风格全局工具
    void buildRightArea();        // 模式 + 主题 + 用户 + 窗控
    void onMaximizeRestore();     // 双击标题栏 / 点最大化按钮
    QRect titleBarDragRect() const;  // 计算可拖动区域 (left+mid, 避开按钮)
    //void buildGlobalBar();       // 顶部全局命令栏放主题按钮 (已合并到 buildRightArea)
    void buildInfoTree();
    void buildThemeConnections();
    void retranslateUiTexts();
    void applyTheme();

    // PS/WPS/VS 风格 (2026-09-10): 安装 HomePage 到 mainStack page 0
    void installHomePage();

    void updateWindowTitle();
    QString defaultFileDialogFilter() const;
    QString defaultFileExtension() const;

    // Tab 管理
    int  addDocTab(QWidget *doc);
    int  addImageTab(ImageWindow *img);
    QWidget *widgetAt(int index) const;
    int  indexOfWidget(QWidget *w) const;
    void focusOrCreateWidget(QWidget *doc);

    // Tab 钉住
    struct TabState {
        QWidget *widget = nullptr;
        QToolButton *pinBtn = nullptr;
        QToolButton *closeBtn = nullptr;   // 自己画的 close 按钮 (跟 pin 一起放在 RightSide 容器里)
        bool pinned = false;
    };
    void installTabPinButton(int index);
    void onTabPinToggled(int index, bool pinned);
    void reorderPinnedToLeft();
    int  pinButtonIndex(QObject *btn) const;
    TabState *tabStateAt(int index);

    // 文档类型识别
    enum DocType { TypeUnknown, TypeText, TypeImage };
    static DocType docTypeOf(QWidget *w);
    QString docTypeName(DocType t) const;

    // 修改标记
    void refreshTabTitle(int index);

    // 会话
    void saveSession();
    void restoreSession();

    Ui::MainWindow *ui;

    // 主题按钮 (放在顶部全局命令栏, 后续会加更多全局按钮)
    //QToolButton *m_btnTheme = nullptr;

    // 模式按钮已删除 (3D 模块移除, 2026-09-02)

    // ===== 自绘标题栏 (L2: 无边框 + 3 段容器 + 窗控) =====
    QWidget      *m_titleBar   = nullptr;  // 整条标题栏 (40px 高), setMenuWidget 嵌入
    QFrame       *m_titleLeft  = nullptr;  // 左段: logo + app name + version
    QFrame       *m_titleMid   = nullptr;  // 中段: 菜单 + 后续 VS 风格全局工具
    QFrame       *m_titleRight = nullptr;  // 右段: 模式 corner + 主题 + 用户 + 窗控
    // 窗控按钮 (右侧)
    QPushButton  *m_btnMin     = nullptr;
    QPushButton  *m_btnMax     = nullptr;
    QPushButton  *m_btnClose   = nullptr;
    // 全局命令按钮 (右侧, modeCorner 之后)
    QToolButton  *m_btnTheme   = nullptr;
    QToolButton  *m_btnUser    = nullptr;
    // 拖动状态
    QPoint        m_titleDragStartGlobal;        // 拖动起始 (全局坐标 - 窗口左上)
    QPoint        m_titleDragStartLocal;         // 拖动起始 (窗口本地坐标, 用于还原时定位)
    QPoint        m_titlePressGlobal;            // 按下时的全局坐标 (用来算阈值距离)
    bool          m_titleDragging = false;       // 正在拖动标题栏
    bool          m_titleRestoredOnDrag = false; // 拖动期间是否已从最大化还原过
    bool          m_isMaximized   = false;       // 同步窗控按钮文字

    // 简单窗口模式 (2026-09-09 重建)
    //   m_mode 唯一可信状态, setMode 统一切 m_mode + showMaximized/showNormal
    //   m_normalSize 仅在 Normal 时使用, 默认 1280x800, 启动后从 QSettings 读
    WindowMode    m_mode         = WindowMode::Maximized;
    QSize         m_normalSize   = QSize(1280, 800);

    QAction *m_actNew      = nullptr;
    QAction *m_actOpen     = nullptr;
    QAction *m_actSave     = nullptr;
    QAction *m_actSaveAs   = nullptr;
    // P0-8.2 (2026-09-15): 多格式导出 action (Ctrl+Shift+E)
    QAction *m_actExport   = nullptr;
    QAction *m_actCloseCur = nullptr;
    QAction *m_actCloseAll = nullptr;
    QAction *m_actCloseOthers   = nullptr;
    QAction *m_actCloseSameType = nullptr;

    QAction *m_actUndo     = nullptr;
    QAction *m_actRedo     = nullptr;
    QAction *m_actCut      = nullptr;
    QAction *m_actCopy     = nullptr;
    QAction *m_actPaste    = nullptr;
    QAction *m_actSelAll   = nullptr;
    QAction *m_actFind     = nullptr;
    QAction *m_actReplace  = nullptr;

    QAction *m_actZoomIn   = nullptr;
    QAction *m_actZoomOut  = nullptr;
    QAction *m_actResetLayout = nullptr;
    QAction *m_actToggleTheme = nullptr;

    // P0-6.5 (2026-09-14): 6 图像变换 action
    QAction *m_actFreeTransform  = nullptr;   // 编辑 → 自由变换 (Ctrl+T)
    QAction *m_actImageFlipH     = nullptr;   // 图像 → 水平翻转
    QAction *m_actImageFlipV     = nullptr;   // 图像 → 垂直翻转
    QAction *m_actImageRotate90CW  = nullptr; // 图像 → 旋转 90° 顺时针 (Ctrl+])
    QAction *m_actImageRotate90CCW = nullptr; // 图像 → 旋转 90° 逆时针 (Ctrl+[)
    QAction *m_actImageRotate180  = nullptr; // 图像 → 旋转 180°

    // 模式 action 已删除 (3D 模块移除, 2026-09-02)
    // 后续如果需要多模态切分 (图片/视频/音频), 在这里加回 QActionGroup + actions

    QList<TabState> m_tabStates;

    InfoTreeDock *m_infoDock = nullptr;

    // PS/WPS/VS 风格主页切换 (2026-09-10): mainStack page 0 (HomePage) / page 1 (Workspace)
    //   HomePage 是独立 widget, 装在 mainStack page 0 的 homePageContainer 里
    //   工作空间是 mainStack page 1 的 tabWidget (跟以前一样装 doc tab)
    HomePage     *m_homePage     = nullptr;

    // F-F (2026-09-09) 工作区 Mediator (临时 mainwindow 持有, F-G 改 imagewindow 共享)
    std::unique_ptr<mediators::WorkspaceMediator> m_workspaceMed;

    // 顶部全局命令栏 (主题按钮 + 后续全局设置按钮)
    class QToolBar *m_globalBar = nullptr;
};

#endif // MAINWINDOW_H
