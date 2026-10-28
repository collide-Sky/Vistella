#ifndef LAYERCOMMAND_H
#define LAYERCOMMAND_H

// =============================================================
// LayerCommand — 图层操作 QUndoCommand
//
// 覆盖的图层操作 (每个 Op 存 before/after 完整状态):
//   Add      — 新建 layer, undo 删除
//   Remove   — 删除 layer, undo 恢复 (含 cv::Mat clone)
//   Move     — 移动 layer (moveUp / moveDown), undo 反向
//   Opacity  — 改 opacity, undo 还原
//   Visible  — 改 visible, undo 还原
//   Locked   — 改 locked
//   Linked   — 改 isLinked
//   Blend    — 改 blend mode
//   Rename   — 改 name
//   Merge    — 合并图层 (mergeDown / flatten), undo 拆分
//   Group    — 编组, undo 解散
//   Ungroup  — 解散, undo 恢复
//
// 设计原则:
//   - 每个 command 存两份状态: before (旧值) + after (新值)
//   - 结构性操作 (Add / Remove / Merge) 额外记住目标图层的 stableId,
//     撤销 / 重做按 id 定位而不是裸下标 (LYR-06)
//   - 不用 parent (每个 command 独立, 易于调试)
//
// 调用约定 (重要):
//   调用方**先**把新值应用到图层, **再**push command.
//   这样 command 构造时图层上已经是新值, captureAfter() 读得到, redo 才有
//   东西可写回去. 例:
//       m_layerStack->setOpacity(index, 0.5f);
//       m_undoStack->push(new LayerCommand(stack, Opacity, index, oldOpacity));
//   Remove / Merge 是例外: 它们的"向前"动作由 command 自己执行.
//
// 用法:
//     QUndoStack *stack = m_w->m_undoStack;
//     stack->push(new LayerCommand(m_w->m_layerStack.get(),
//                                  LayerCommand::Add, newLayer));
// =============================================================

#include "Layer.h"
#include "LayerStack.h"

#include <QUndoCommand>
#include <QString>
#include <QColor>
#include <QTransform>
#include <QPointer>

namespace layers {

class LayerCommand : public QUndoCommand
{
public:
    enum Op {
        Add, Remove, Move, Opacity, Visible, Locked, Linked,
        Blend, Rename, Merge, Group, Ungroup,
        //  per-kind 编辑 Op
        SetText,            // 文字内容 (m_strVal = old)
        SetTextFont,        // 字体三件套 (m_strVal=old family, m_intVal=old size, m_oldColor=old color)
        SetSmartObject,     // 智能对象源 (m_strVal=old path, m_boolVal=old embed)
        SetAdjustmentType,  // 调整类型 (m_strVal=old type)
        SetAdjustmentLut,   // 调整 LUT (m_oldMat=old lut)
        //  蒙版 Op
        AddMask,            // 加蒙版 (m_oldMat=old mask, m_boolVal=wasEnabled)
        ClearMask,          // 删蒙版 (m_oldMat=old mask, m_boolVal=wasEnabled)
        EnableMask,         // 开关 (m_boolVal=old enabled)
        //  智能对象扩展 Op
        ToggleSmartObjectEmbed,  // link↔embed (m_boolVal=old embedded)
        // 智能对象 Convert / Rasterize / Transform
        ConvertToSmartObject,    // Bitmap->SmartObject (in-place kind change)
        RasterizeSmartObject,   // SmartObject->Bitmap (in-place kind change)
        SetSmartObjectTransform,// affine transform (m_oldTransform/m_newTransform)
        // SmartFilter sub-layer Ops
        AppendSmartFilter,      // add SmartFilter sub-layer (m_strVal=filterType, m_intVal=filterIdx)
        RemoveSmartFilter,      // remove SmartFilter sub-layer (m_layer=old full Layer)
        MoveSmartFilter,        // reorder (m_intVal=newSlot)
        SetSmartFilterEnabled,  // enable/disable (m_boolVal=old enabled)
        SetSmartFilterStrength, // strength (m_floatVal=old strength)
        // 工具编辑: 替换某图层整层像素
        //   m_oldMat = 编辑前的像素, m_newMat = 编辑后的像素
        SetLayerImage,
    };

    // Add: 给一个 layer, push 时 add, undo 时 remove
    LayerCommand(LayerStack *stack, Op op, const Layer &layer, QUndoCommand *parent = nullptr);
    // Remove: 同 Add, push 时 remove, undo 时 add
    static LayerCommand* makeRemove(LayerStack *stack, int index);
    // Move: index 是被移动的 layer, direction = +1 (上) / -1 (下)
    //   callers should use the makeMoveUp /
    //   makeMoveDown factories below (factory pattern, same as Group/Ungroup).
    //   The 3-arg (stack, index, direction) constructor is kept for source
    //   compatibility but its redo() is now a no-op — if you push it directly
    //   without calling moveUp/moveDown yourself, nothing moves.
    LayerCommand(LayerStack *stack, int index, int direction, QUndoCommand *parent = nullptr);
    // makeMoveUp: factory applies LayerStack::moveUp(index) itself. Caller
    //   does NOT need to call moveUp separately (matches makeMergeIntoGroup /
    //   makeFlattenGroup). redo() is a no-op; undo() reverses the move.
    static LayerCommand* makeMoveUp(LayerStack *stack, int index);
    // makeMoveDown: factory applies LayerStack::moveDown(index) itself.
    static LayerCommand* makeMoveDown(LayerStack *stack, int index);
    // Opacity / Visible / Locked / Linked / Blend / Rename: index + value
    LayerCommand(LayerStack *stack, Op op, int index, float floatVal, QUndoCommand *parent = nullptr);  // opacity
    LayerCommand(LayerStack *stack, Op op, int index, bool boolVal, QUndoCommand *parent = nullptr);    // visible / locked / linked
    LayerCommand(LayerStack *stack, Op op, int index, int intVal, QUndoCommand *parent = nullptr);       // blend
    LayerCommand(LayerStack *stack, Op op, int index, const QString &strVal, QUndoCommand *parent = nullptr);  // rename
    // Merge: mergeDown index, undo 拆分 (Phase 1 简化: 用 m_beforeMerge / m_afterMerge 存两个 layer)
    LayerCommand(LayerStack *stack, int mergeIndex, QUndoCommand *parent = nullptr);
    // simplified Group/Ungroup (link flag)
    //   is REMOVED. Use makeMergeIntoGroup / makeFlattenGroup factories below
    //   which integrate with P1.5.2 LayerKind::Group + m_groups storage.
    //
    //   Pattern matches existing SmartFilter factories: factory performs
    //   the initial state change itself. redo() is a no-op; undo()
    //   reverses via flattenGroup / reinsertGroup.
    //
    // makeMergeIntoGroup: packs the given adjacent indices into a new
    //   Group container via LayerStack::mergeIntoGroup. Caller passes only
    //   the indices; no need to call mergeIntoGroup separately.
    static LayerCommand* makeMergeIntoGroup(LayerStack *stack,
                                            QList<int> indices);
    // makeFlattenGroup: dissolves the Group at groupIdx via
    //   LayerStack::flattenGroup (factory captures a children snapshot
    //   internally for undo).
    static LayerCommand* makeFlattenGroup(LayerStack *stack, int groupIdx);
    //  per-kind 编辑专用
    //   3 参 (stack, op, index) 避免跟 (stack, index, direction) 歧义
    explicit LayerCommand(LayerStack *stack, Op op, int index);
    //  per-kind 编辑 factory
    //   用 static make 避免构造函数签名冲突 (rename/text 都是 QString)
    static LayerCommand* makeSetText(LayerStack *stack, int index, const QString &oldText);
    static LayerCommand* makeSetTextFont(LayerStack *stack, int index,
                                          const QString &oldFamily, int oldSize,
                                          const QColor &oldColor);
    static LayerCommand* makeSetSmartObject(LayerStack *stack, int index,
                                             const QString &oldPath, bool oldEmbed);
    static LayerCommand* makeSetAdjustmentType(LayerStack *stack, int index, const QString &oldType);
    static LayerCommand* makeSetAdjustmentLut(LayerStack *stack, int index, const cv::Mat &oldLut);
    //  蒙版 factory
    //   oldMask (空 Mat = 原无蒙版), wasEnabled (原 enabled 状态)
    static LayerCommand* makeAddMask(LayerStack *stack, int index,
                                      const cv::Mat &oldMask, bool wasEnabled);
    static LayerCommand* makeClearMask(LayerStack *stack, int index,
                                        const cv::Mat &oldMask, bool wasEnabled);
    static LayerCommand* makeEnableMask(LayerStack *stack, int index, bool oldEnabled);
    //  智能对象扩展 factory
    static LayerCommand* makeToggleSmartObjectEmbed(LayerStack *stack, int index, bool oldEmbedded);
    // Convert / Rasterize / Transform factories
    //   ConvertToSmartObject: stores full before layer (cv::Mat clone) for undo.
    //   RasterizeSmartObject: stores full before layer (cv::Mat clone + sourceFilePath).
    //   SetSmartObjectTransform: old/new QTransform + hasTransform flag.
    static LayerCommand* makeConvertToSmartObject(LayerStack *stack, int index,
                                                  const Layer &beforeLayer);
    static LayerCommand* makeRasterizeSmartObject(LayerStack *stack, int index,
                                                   const Layer &beforeLayer);
    static LayerCommand* makeSetSmartObjectTransform(LayerStack *stack, int index,
                                                     const QTransform &oldT,
                                                     bool oldHas,
                                                     const QTransform &newT,
                                                     bool newHas);
    // SmartFilter chain undo factories
    //   AppendSmartFilter: caller appends the filter first (UI pushes the
    //     LayerCommand after appendSmartFilter returns the new idx); on
    //     undo we drop the filter sub-layer and any chain bookkeeping.
    //   RemoveSmartFilter: caller removes the filter first via
    //     removeSmartFilter; factory stores the old layer + parent chain
    //     state (m_strVal = parent SmartObject index as text).
    //   MoveSmartFilter: m_intVal = new slot; redo applies moveSmartFilter,
    //     undo reverses by moving to the original slot.
    //   SetSmartFilterEnabled: m_boolVal = old enabled.
    //   SetSmartFilterStrength: m_floatVal = old strength.
    static LayerCommand* makeAppendSmartFilter(LayerStack *stack, int filterIdx,
                                               const QString &filterType,
                                               double oldStrength);
    static LayerCommand* makeRemoveSmartFilter(LayerStack *stack, int filterIdx,
                                               const Layer &oldLayer);
    static LayerCommand* makeMoveSmartFilter(LayerStack *stack, int filterIdx,
                                             int oldSlot);
    static LayerCommand* makeSetSmartFilterEnabled(LayerStack *stack, int filterIdx,
                                                   bool oldEnabled);
    static LayerCommand* makeSetSmartFilterStrength(LayerStack *stack, int filterIdx,
                                                    double oldStrength);
    // 工具编辑: 替换某图层整层像素 (画笔/修复/红眼 等)
    //   before: 编辑前的图层像素, after: 编辑后的图层像素
    //   调用方自己把 after 应用到图层上, 再 push 这条命令;
    //   undo 写回 before, redo 写回 after.
    static LayerCommand* makeSetLayerImage(LayerStack *stack, int index,
                                           const cv::Mat &before,
                                           const cv::Mat &after,
                                           const QString &text);

    void undo() override;
    void redo() override;

public:
    // 允许 mainwindow 在 push 前修改 m_layer (e.g. duplicate 改名)
    Layer& layerRef() { return m_layer; }

private:
    // ---- 统一的应用 / 刷新入口 ----
    //   applyState(after): 按 Op 应用"旧值"(after=false, undo) 或
    //     "新值"(after=true, redo). 所有状态变更都走 LayerStack 的公开
    //     API, 由 LayerStack 负责发 layerChanged.
    //   captureAfter(): 构造时从当前图层读出"新值". 调用方的约定是
    //     "先改图层, 再 push command", 所以构造时图层上已经是新值.
    //   notifyChanged(): undo/redo 末尾统一出口, 负责把画布刷新出来.
    //     (发 layerChanged 的事由 setter / applyState 里的补发负责.)
    //   selfApplied(): 这个 Op 的"向前"动作是不是由 command 自己执行.
    //     true  → 首个 redo 必须真的执行 (Remove / Merge).
    //     false → 调用方已经应用过了, 首个 redo 必须是 no-op, 否则会重复
    //             应用一次 (比如追加两次智能滤镜).
    void applyState(bool after);
    void captureAfter();
    void notifyChanged();
    bool selfApplied() const;

    LayerStack *m_stack;
    Op          m_op;
    int         m_index = -1;
    // 目标图层的稳定 id (LYR-06).
    //   撤销 / 重做按 id 定位, 不用裸下标 —— 下标会随增删移动漂移,
    //   按旧下标删会删掉另一个图层.
    LayerId     m_targetId = 0;
    // Merge: 被合并掉的那一层 (上层) 的 id.
    LayerId     m_secondId = 0;
    int         m_index2 = -1;       // second index (move / group)
    int         m_intVal = 0;        // direction / blend
    float       m_floatVal = 0;      // opacity (old)
    bool        m_boolVal = false;   // visible / locked / linked
    QString     m_strVal;            // rename (old name) / SetText / SetSmartObject path / SetAdjustmentType
    QColor      m_oldColor;          // Phase 3: SetTextFont old color
    cv::Mat     m_oldMat;            // Phase 3: SetAdjustmentLut old LUT
                                    // P0-3.3: 同时作 "before image 快照" (跟 host 一起用)
    cv::Mat     m_newMat;            // P0-3.3: "after image 快照" (仅 host 模式用)
                                    // 也用于 AddMask 的新蒙版 / SetAdjustmentLut 的新 LUT

    // ---- after 状态 (新值) ----
    //   构造时由 captureAfter() 从图层上读出, redo 时原样写回.
    //   没有这一组字段, redo 就只能写成 no-op (撤销之后重做等于什么都不做).
    float       m_floatNew = 0.0f;
    bool        m_boolNew = false;
    int         m_intNew = 0;
    QString     m_strNew;
    QColor      m_newColor;
    double      m_doubleNew = 0.0;
    int         m_groupChildCount = 0;    // 编组/解组: 子图层数量
    bool        m_firstRedo = true;       // QUndoStack::push 触发的首个 redo

    // 状态保存 (Phase 1 简化: Add/Remove/Merge 用 before/after 完整 layer)
    Layer       m_layer;             // Add: new layer; Remove: deleted layer
    Layer       m_mergeBefore;       // Merge: 下层 (merge target) before
    Layer       m_mergeAfter;        // Merge: 下层 (merge target) after
    Layer       m_mergeUpper;        // Merge: 上层 (被合并掉的那层), 撤销时插回
    Layer       m_afterLayer;        // Convert/Rasterize: 转换后的完整快照
    QList<bool> m_groupBefore;       // Group: 区间内 link 状态
    QList<bool> m_ungroupBefore;     // Ungroup: 全部 link 状态
    // Convert / Rasterize smart object before-state
    //   We reuse m_layer for the "before" snapshot (containing cv::Mat + sourceFilePath
    //   + transform) so we can restore the original payload on undo. This is
    //   lighter than introducing a separate "ConvertBefore/after" + "RasterizeBefore".
    // Smart object transform undo state
    QTransform  m_oldTransform;      // SetSmartObjectTransform: before
    QTransform  m_newTransform;      // SetSmartObjectTransform: after
    bool        m_oldHasTransform = false;  // SetSmartObjectTransform: before has flag
    bool        m_newHasTransform = false;  // SetSmartObjectTransform: after has flag
    // SmartFilter undo state
    //   AppendSmartFilter / RemoveSmartFilter reuse m_layer (full clone).
    //   MoveSmartFilter stores old/new slot in m_intVal (current) / m_index2 (old).
    int         m_oldSlot = -1;      // MoveSmartFilter: slot before move
    // Group/Ungroup undo state. The Group /
    //   Ungroup Op enum values are kept (for forward compatibility with
    //   any UI strings already constructed), but the storage below drives
    //   P1.5.2 LayerKind::Group redo/undo rather than the P0-1 link flag.
    QList<int>             m_groupIndices;    // makeMergeIntoGroup: indices passed to mergeIntoGroup
    std::vector<LayerPtr>  m_flattenChildren; // makeFlattenGroup:  captured children snapshot
    int                    m_flattenAtIndex = -1;  // makeFlattenGroup: index that held the Group pre-flatten
};

} // namespace layers

#endif // LAYERCOMMAND_H
