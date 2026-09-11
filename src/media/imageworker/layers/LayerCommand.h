#ifndef LAYERCOMMAND_H
#define LAYERCOMMAND_H

// =============================================================
// LayerCommand — 阶段 1 W4.3 Phase 1 图层操作 QUndoCommand (2026-09-04)
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
//   - 每个 command 内部存 before/after Layer 拷贝 (含 cv::Mat clone)
//   - undo() 反向操作, redo() 正向
//   - 不用 parent (每个 command 独立, 易于调试)
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
#include <QPointer>

// 前向声明 (P0-3.3 引入 host 指针做 image-based undo)
//   不 #include "imagewindow.h" 是为了避免 LayerCommand.h 拉进 media 模块的依赖
//   undo/redo 内部通过 m_host 调 replaceCurrentImage (ImageWindow 公有方法)
//   必须放**全局**命名空间, ImageWindow 在全局, 不在 layers::
class ImageWindow;

namespace layers {

class LayerCommand : public QUndoCommand
{
public:
    enum Op {
        Add, Remove, Move, Opacity, Visible, Locked, Linked,
        Blend, Rename, Merge, Group, Ungroup,
        // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑 Op
        SetText,            // 文字内容 (m_strVal = old)
        SetTextFont,        // 字体三件套 (m_strVal=old family, m_intVal=old size, m_oldColor=old color)
        SetSmartObject,     // 智能对象源 (m_strVal=old path, m_boolVal=old embed)
        SetAdjustmentType,  // 调整类型 (m_strVal=old type)
        SetAdjustmentLut,   // 调整 LUT (m_oldMat=old lut)
        // 阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版 Op
        AddMask,            // 加蒙版 (m_oldMat=old mask, m_boolVal=wasEnabled)
        ClearMask,          // 删蒙版 (m_oldMat=old mask, m_boolVal=wasEnabled)
        EnableMask,         // 开关 (m_boolVal=old enabled)
        // 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展 Op
        ToggleSmartObjectEmbed,  // link↔embed (m_boolVal=old embedded)
    };

    // Add: 给一个 layer, push 时 add, undo 时 remove
    LayerCommand(LayerStack *stack, Op op, const Layer &layer, QUndoCommand *parent = nullptr);
    // Remove: 同 Add, push 时 remove, undo 时 add
    static LayerCommand* makeRemove(LayerStack *stack, int index);
    // Move: index 是被移动的 layer, direction = +1 (上) / -1 (下)
    LayerCommand(LayerStack *stack, int index, int direction, QUndoCommand *parent = nullptr);
    // Opacity / Visible / Locked / Linked / Blend / Rename: index + value
    LayerCommand(LayerStack *stack, Op op, int index, float floatVal, QUndoCommand *parent = nullptr);  // opacity
    LayerCommand(LayerStack *stack, Op op, int index, bool boolVal, QUndoCommand *parent = nullptr);    // visible / locked / linked
    LayerCommand(LayerStack *stack, Op op, int index, int intVal, QUndoCommand *parent = nullptr);       // blend
    LayerCommand(LayerStack *stack, Op op, int index, const QString &strVal, QUndoCommand *parent = nullptr);  // rename
    // Merge: mergeDown index, undo 拆分 (Phase 1 简化: 用 m_beforeMerge / m_afterMerge 存两个 layer)
    LayerCommand(LayerStack *stack, int mergeIndex, QUndoCommand *parent = nullptr);
    // Group: [first, last] 编组 (Phase 1 简化: link 标记)
    LayerCommand(LayerStack *stack, int first, int last, int groupId, QUndoCommand *parent = nullptr);
    // Ungroup: 全 unlink
    LayerCommand(LayerStack *stack, Op op, QUndoCommand *parent = nullptr);
    // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑专用
    //   3 参 (stack, op, index) 避免跟 (stack, index, direction) 歧义
    explicit LayerCommand(LayerStack *stack, Op op, int index);
    // 阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑 factory
    //   用 static make 避免构造函数签名冲突 (rename/text 都是 QString)
    static LayerCommand* makeSetText(LayerStack *stack, int index, const QString &oldText);
    static LayerCommand* makeSetTextFont(LayerStack *stack, int index,
                                          const QString &oldFamily, int oldSize,
                                          const QColor &oldColor);
    static LayerCommand* makeSetSmartObject(LayerStack *stack, int index,
                                             const QString &oldPath, bool oldEmbed);
    static LayerCommand* makeSetAdjustmentType(LayerStack *stack, int index, const QString &oldType);
    static LayerCommand* makeSetAdjustmentLut(LayerStack *stack, int index, const cv::Mat &oldLut);
    // P0-3.3 (2026-09-08): 升级版 SetAdjustmentLut
    //   接受 ImageWindow* host + newImage, undo/redo 走 replaceCurrentImage
    //   旧版 (无 host) 走原 l->adjustmentLut 路径, 兼容已存在的 P0-3.2 代码
    //   m_oldMat 重定义为"before image 快照", m_newMat = "after image 快照"
    //   undo: m_host->replaceCurrentImage(m_oldMat)
    //   redo: m_host->replaceCurrentImage(m_newMat)
    static LayerCommand* makeSetAdjustmentLut(LayerStack *stack, int index,
                                              const cv::Mat &oldImage,
                                              const cv::Mat &newImage,
                                              ImageWindow *host);
    // 阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版 factory
    //   oldMask (空 Mat = 原无蒙版), wasEnabled (原 enabled 状态)
    static LayerCommand* makeAddMask(LayerStack *stack, int index,
                                      const cv::Mat &oldMask, bool wasEnabled);
    static LayerCommand* makeClearMask(LayerStack *stack, int index,
                                        const cv::Mat &oldMask, bool wasEnabled);
    static LayerCommand* makeEnableMask(LayerStack *stack, int index, bool oldEnabled);
    // 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象扩展 factory
    static LayerCommand* makeToggleSmartObjectEmbed(LayerStack *stack, int index, bool oldEmbedded);

    void undo() override;
    void redo() override;

public:
    // 允许 mainwindow 在 push 前修改 m_layer (e.g. duplicate 改名)
    Layer& layerRef() { return m_layer; }

private:
    LayerStack *m_stack;
    Op          m_op;
    int         m_index = -1;
    int         m_index2 = -1;       // second index (move / group)
    int         m_intVal = 0;        // direction / blend
    float       m_floatVal = 0;      // opacity (old)
    bool        m_boolVal = false;   // visible / locked / linked
    QString     m_strVal;            // rename (old name) / SetText / SetSmartObject path / SetAdjustmentType
    QColor      m_oldColor;          // Phase 3: SetTextFont old color
    cv::Mat     m_oldMat;            // Phase 3: SetAdjustmentLut old LUT
                                    // P0-3.3: 同时作 "before image 快照" (跟 host 一起用)
    cv::Mat     m_newMat;            // P0-3.3: "after image 快照" (仅 host 模式用)
    QPointer<ImageWindow> m_host;    // P0-3.3: image-based undo/redo 目标, 跟 m_stack 互补

    // 状态保存 (Phase 1 简化: Add/Remove/Merge 用 before/after 完整 layer)
    Layer       m_layer;             // Add: new layer; Remove: deleted layer
    Layer       m_mergeBefore;       // Merge: 下层 (merge target) before
    Layer       m_mergeAfter;        // Merge: 下层 (merge target) after
    QList<bool> m_groupBefore;       // Group: 区间内 link 状态
    QList<bool> m_ungroupBefore;     // Ungroup: 全部 link 状态
};

} // namespace layers

#endif // LAYERCOMMAND_H
