#ifndef LAYERPANEL_H
#define LAYERPANEL_H

// =============================================================
// LayerPanel — 阶段 1 W4.3 Phase 1 图层 dock widget (2026-09-04)
//
// PS 风格图层面板 (完整 Phase 1 功能):
//   - 列表 widget: 缩略图 40x40 + 名字 + 可见 ✓/✗ + 锁定 🔒 + 链接 🔗
//   - 工具栏: 新建 / 删除 / 复制 / 上移 / 下移 / 合并下方 / 拼合可见 / 编组 / 解组
//   - 右键菜单: 重命名 / 切换可见 / 切换锁定 / 切换链接 / 合并下方 / 拼合可见
//   - 选中: 单击高亮, 选中后底部显示属性 (不透明度 / 混合模式 / 可见 / 锁定 / 链接)
//
// 设计原则 (成熟方案):
//   - LayerPanel 是纯 QWidget, 不直接修改 LayerStack — 所有操作发 signal 给 MainWindow
//     MainWindow 调 ImageWindow 的 API + push LayerCommand 进撤销栈
//   - 监听 LayerStack 的 signals 实时更新 UI (add/remove/change/count/selection)
//   - 缩略图缓存: cv::Mat → QImage scaled 到 40x40, layer 变化时 invalidate
// =============================================================

#include <QWidget>
#include <QTreeWidget>
#include <QHash>
#include <QPointer>
#include <QImage>

#include <opencv2/core.hpp>

#include "LayerStack.h"  // P0 leftover 2: LayerId used by Group child item ids (LayerStack::m_groups)

QT_BEGIN_NAMESPACE
class QTreeWidget;
class QTreeWidgetItem;
class QToolBar;
class QDoubleSpinBox;
class QToolButton;
class QLabel;
class QSlider;
class QMenu;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QComboBox;
class QStackedWidget;
QT_END_NAMESPACE

namespace layers {

class LayerStack;

class LayerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayerPanel(LayerStack *stack, QWidget *parent = nullptr);
    ~LayerPanel() override;

    // 绑定到新的 LayerStack (e.g. 切换 tab 时)
    void bindStack(LayerStack *stack);

    // 外部通知: 撤销栈 index 变化 (更新按钮 enabled 状态)
    void setUndoStackIndex(int currentIndex, int savedIndex);

signals:
    // 用户操作 (给 MainWindow 调 ImageWindow API + push LayerCommand)
    // 阶段 1 W4.3 Phase 3 (2026-09-04): 改成 addLayerKindRequested 带 LayerKind 参数
    //   LayerKind 0=Bitmap / 1=Vector / 2=Text / 3=SmartObject / 4=Adjustment
    //   MainWindow 根据 kind 走不同初始化 (QInputDialog / QFileDialog / 默认值)
    void addLayerKindRequested(int kind);
    void deleteLayerRequested(int index);
    void duplicateLayerRequested(int index);
    void moveUpRequested(int index);
    void moveDownRequested(int index);
    void mergeDownRequested(int index);
    void flattenVisibleRequested();
    void groupRequested(int first, int last);
    void ungroupRequested();
    void renameRequested(int index, const QString &newName);
    void setVisibleRequested(int index, bool visible);
    void setLockedRequested(int index, bool locked);
    void setLinkedRequested(int index, bool linked);
    void setOpacityRequested(int index, float opacity);
    void setBlendRequested(int index, int blend);
    // Phase 3 (2026-09-04): per-kind 编辑信号
    //   全部传 int index, MainWindow 路由到 LayerStack::setXxx + push LayerCommand
    void setTextRequested(int index, const QString &text);
    void setTextFontRequested(int index, const QString &family, int size, const QColor &color);
    void setSmartObjectSourceRequested(int index, const QString &path, bool embed);
    void setAdjustmentTypeRequested(int index, const QString &type);
    void setAdjustmentLutResetRequested(int index);   // 复位到 identity
    // Phase 4 (2026-09-04): 蒙版信号
    //   maskPath 传 "" = clearMask; 否则 addMask(load + enable)
    void addMaskRequested(int index, const QString &maskPath);
    void clearMaskRequested(int index);
    void toggleMaskRequested(int index, bool enabled);
    // P1.3.3 (2026-09-16): extended mask operations.
    void addPixelMaskFromSelectionRequested(int index);
    void addVectorMaskRequested(int index);
    void setMaskDensityRequested(int index, qreal density);
    void setMaskFeatherRequested(int index, qreal featherPx);
    void setMaskInvertRequested(int index, bool invert);
    // Phase 5 (2026-09-04): 智能对象扩展信号
    void editSmartObjectSourceRequested(int index);   // QDesktopServices 打开源文件
    void refreshSmartObjectRequested(int index);
    void toggleSmartObjectEmbedRequested(int index);
    // P1.4.2 (2026-09-17): 智能对象右键菜单入口
    //   转发到 MainWindow onSmartObjectConvert/Rasterize/Relink (带 idx 参数版)
    void convertToSmartObjectRequested(int index);    // Bitmap -> SmartObject (嵌入)
    void rasterizeSmartObjectRequested(int index);    // SmartObject -> Bitmap
    void relinkSmartObjectRequested(int index);       // 重新链接源文件
    void selectionChangedFromPanel(int index);
    // P0 leftover 2 (2026-09-21): emitted when a Group's child item is
    //   activated in the tree. groupId is the parent Group's LayerId (stable
    //   across m_layers mutations; see LayerStack::m_groups rekey). childIdx
    //   is the index within groupChildrenOf(groupId). Layer operations on
    //   group children are wired up in P0 leftover 3 (LayerCommand Group/Ungroup).
    void selectionChangedFromPanelChild(LayerId groupId, int childIdx);
    // P1.4.4 (2026-09-17): SmartFilter sub-layer entry (SmartObject right-click)
    //   Routed to MainWindow: pops filter picker dialog + calls appendSmartFilter
    //   + pushes undo command.
    void applySmartFilterRequested(int smartIdx);
    // P1.4.5 (2026-09-17): SmartObject non-destructive transform right-click
    //   entries. Routed to ImageWindow::applySmartObjectTransform.
    //   transformSmartObjectRequested: pops scale + rotation QInputDialog.
    //   resetSmartObjectTransformRequested: clears the existing transform.
    void transformSmartObjectRequested(int smartIdx);
    void resetSmartObjectTransformRequested(int smartIdx);

private slots:
    // 内部: 监听 LayerStack signals
    void onLayerAdded(int index);
    void onLayerRemoved(int index);
    void onLayerChanged(int index);
    void onCountChanged();
    void onSelectionChanged(int index);

    // 按钮 / 列表点击
    void onAddClicked();
    void onDeleteClicked();
    void onDuplicateClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onMergeDownClicked();
    void onFlattenClicked();
    void onGroupClicked();
    void onUngroupClicked();
    void onTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onOpacityChanged(double v);
    void onBlendChanged(int idx);

    // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑 slots
    void onTextEditChanged();                        // QLineEdit::editingFinished (无参)
    void onTextFontFamilyChanged(const QString &family);
    void onTextSizeChanged(int size);
    void onSmartObjectBrowseClicked();
    void onSmartObjectEmbedToggled(bool embed);
    void onAdjustmentTypeChanged(const QString &type);
    void onAdjustmentLutResetClicked();

    // 阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版 slots
    void onMaskAddClicked();
    void onMaskClearClicked();
    void onMaskEnableToggled(bool enabled);

    // 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展 slots
    void onSmartObjectEditClicked();
    void onSmartObjectRefreshClicked();
    void onSmartObjectToggleEmbedClicked();

    // 右键菜单
    void onContextMenu(const QPoint &pos);

private:
    // 重建列表 (clear + addItem)
    void rebuildList();
    // 更新某一行 (不重建列表)
    void refreshRow(int index);
    // 缩略图 cv::Mat → QImage 40x40
    QImage makeThumbnail(const cv::Mat &img) const;
    // 选中行同步 layerStack
    void syncSelectionToStack();
    // 构造 blend 下拉 QMenu (11 PS 混合模式 + QActionGroup exclusive)
    QMenu *createBlendMenu();
    // 构造 per-kind 属性面板 (5 个 page)
    void   buildKindProps();
    // 同步 per-kind 属性到当前选中 layer
    void   syncKindProps(int index);
    // 阶段 1 W4.4 Phase 4 (2026-09-04): 同步 mask 控件到当前 layer
    void   syncMaskProps(int index);

    // P0 leftover 2 (2026-09-21): tree helpers. Top-level items hold
    //   layer index in Qt::UserRole. Group child items hold a QString
    //   "child:<groupLayerIdHex>:<childIdx>" so selection routing can
    //   distinguish top-layer vs group-child clicks.
    static QString childItemId(LayerId groupId, int childIdx);
    static bool    isChildItemId(const QString &s);
    static LayerId childItemGroupId(const QString &s);
    static int     childItemChildIdx(const QString &s);
    QTreeWidgetItem *findTopLevelItemByLayerIndex(int layerIndex) const;
    QTreeWidgetItem *findGroupChildItem(LayerId groupId, int childIdx) const;

    QPointer<LayerStack> m_stack;

    // 阶段 1 W4.3 Phase 2 (2026-09-04): blend menu 缓存
    //   持有指针用于在 onSelectionChanged 同步 checked 状态
    QMenu *m_blendMenu = nullptr;

    // 列表 (P0 leftover 2: QListWidget -> QTreeWidget for Group children)
    QTreeWidget *m_tree = nullptr;

    // 工具栏按钮
    QToolButton *m_btnAdd = nullptr;
    QToolButton *m_btnDel = nullptr;
    QToolButton *m_btnDup = nullptr;
    QToolButton *m_btnUp  = nullptr;
    QToolButton *m_btnDown = nullptr;
    QToolButton *m_btnMerge = nullptr;
    QToolButton *m_btnFlatten = nullptr;
    QToolButton *m_btnGroup = nullptr;
    QToolButton *m_btnUngroup = nullptr;

    // 属性面板
    QDoubleSpinBox *m_opacity = nullptr;
    // 阶段 1 W4.3 Phase 2 (2026-09-04): blend 改用 QToolButton+QMenu
    //   根因: QComboBox 在浮出 QDockWidget 里有 Qt z-order bug (popup 漂在标题栏)
    //   修法: QToolButton 显示当前 mode, 点击弹 QMenu, QMenu 跟随 button 位置无 z-order 问题
    QToolButton    *m_blendButton = nullptr;

    // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 属性面板 QStackedWidget
    //   index 0 = empty (Bitmap / 无选中)
    //   index 1 = Text   (text edit + font family combo + size spin)
    //   index 2 = Vector (简单显示: path count label)
    //   index 3 = SmartObject (path label + browse button + embed checkbox)
    //   index 4 = Adjustment (type combo + LUT reset button)
    QStackedWidget *m_kindProps = nullptr;
    // Text
    QLineEdit *m_textEdit = nullptr;
    QComboBox *m_textFontFamily = nullptr;
    QSpinBox  *m_textSize = nullptr;
    // SmartObject
    QLabel    *m_smartPathLabel = nullptr;
    QPushButton *m_smartBrowse = nullptr;
    QToolButton *m_smartEmbed = nullptr;   // checkable
    // 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展按钮
    QPushButton *m_smartEdit = nullptr;        // 打开源到默认 app
    QPushButton *m_smartRefresh = nullptr;     // 重新加载
    QPushButton *m_smartToggleEmbed = nullptr; // link ↔ embed
    // Vector (Phase 3 简化: 只显示 path count)
    QLabel    *m_vectorInfo = nullptr;
    // Adjustment
    QComboBox *m_adjustmentType = nullptr;
    QPushButton *m_adjustmentLutReset = nullptr;
    QLabel    *m_adjustmentInfo = nullptr;

    // 阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版控制 (跨 kind 共享, 不放 QStackedWidget)
    QLabel       *m_maskStatus = nullptr;    // "(无蒙版)" / "(已禁用)" / "(已启用)"
    QPushButton  *m_maskAdd = nullptr;
    QToolButton  *m_maskEnable = nullptr;   // checkable
    QPushButton  *m_maskClear = nullptr;

    // 缩略图缓存 (index → QImage)
    QHash<int, QImage> m_thumbs;

    // 重建锁 (避免 rebuildList 期间触发 selectionChanged)
    bool m_rebuilding = false;
};

} // namespace layers

#endif // LAYERPANEL_H
