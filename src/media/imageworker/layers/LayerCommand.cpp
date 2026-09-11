// =============================================================
// LayerCommand 实现
// 阶段 1 W4.3 Phase 1 (2026-09-04) — 详见 LayerCommand.h
// P0-3.3 (2026-09-08) — SetAdjustmentLut 升级 image-based undo/redo
// =============================================================

#include "LayerCommand.h"

// P0-3.3 (2026-09-08): 实现 image-based undo/redo 需要调 ImageWindow::replaceCurrentImage
//   .cpp 里 #include "imagewindow.h" 是必要的, 不会拉进 media 模块循环依赖
//   (LayerCommand.h 走前置声明, .cpp 走完整 include, 是标准 PIMPL 风格)
#include "../../imagewindow.h"

#include <QString>

namespace layers {

// =====================================================================
//  构造 (按 Op 类型重载)
// =====================================================================

LayerCommand::LayerCommand(LayerStack *stack, Op op, const Layer &layer, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_layer(layer)
{
    switch (op) {
    case Add:    setText(QStringLiteral("Add Layer")); break;
    case Remove: setText(QStringLiteral("Remove Layer")); break;
    default:     setText(QStringLiteral("Layer Op")); break;
    }
}

LayerCommand* LayerCommand::makeRemove(LayerStack *stack, int index)
{
    if (!stack) return nullptr;
    auto l = stack->at(index);
    if (!l) return nullptr;
    return new LayerCommand(stack, Remove, *l);   // 保存完整 layer (含 cv::Mat clone)
}

LayerCommand::LayerCommand(LayerStack *stack, int index, int direction, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(Move), m_index(index), m_intVal(direction)
{
    setText(QStringLiteral("Move Layer"));
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, float floatVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_floatVal(floatVal)
{
    setText(QStringLiteral("Set Opacity"));
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, bool boolVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_boolVal(boolVal)
{
    switch (op) {
    case Visible: setText(QStringLiteral("Toggle Visibility")); break;
    case Locked:  setText(QStringLiteral("Toggle Lock")); break;
    case Linked:  setText(QStringLiteral("Toggle Link")); break;
    default:      setText(QStringLiteral("Layer Bool Op")); break;
    }
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, int intVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_intVal(intVal)
{
    setText(QStringLiteral("Set Blend"));
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, const QString &strVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_strVal(strVal)
{
    setText(QStringLiteral("Rename Layer"));
}

LayerCommand::LayerCommand(LayerStack *stack, int mergeIndex, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(Merge), m_index(mergeIndex)
{
    setText(QStringLiteral("Merge Down"));
    if (!m_stack) return;
    // 保存 merge target (下层) before/after
    auto target = m_stack->at(mergeIndex - 1);
    if (target) m_mergeBefore = *target;   // 下层 before
    auto src = m_stack->at(mergeIndex);
    if (src) {
        // 计算 mergeAfter: target 跟 src 按 src.opacity blend
        cv::Mat merged;
        const float a = src->opacity;
        cv::addWeighted(m_mergeBefore.image, 1.0f - a, src->image, a, 0.0, merged);
        m_mergeAfter = m_mergeBefore;
        m_mergeAfter.image = merged;
    }
}

LayerCommand::LayerCommand(LayerStack *stack, int first, int last, int groupId, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(Group), m_index(first), m_index2(last), m_intVal(groupId)
{
    setText(QStringLiteral("Group Layers"));
    if (!m_stack) return;
    for (int i = first; i <= last; ++i) {
        auto l = m_stack->at(i);
        m_groupBefore << (l && l->isLinked);
    }
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op)
{
    if (op == Ungroup) setText(QStringLiteral("Ungroup"));
    if (!m_stack) return;
    for (int i = 0; i < m_stack->count(); ++i) {
        auto l = m_stack->at(i);
        m_ungroupBefore << (l && l->isLinked);
    }
}

// 阶段 1 W4.3 Phase 3 (2026-09-04): 3 参 (stack, op, index) 专用构造器
//   避免跟 (stack, int, int) Move 构造器歧义
LayerCommand::LayerCommand(LayerStack *stack, Op op, int index)
    : QUndoCommand(), m_stack(stack), m_op(op), m_index(index)
{
    // 留给 factory 设置 m_strVal / m_oldMat / m_oldColor
}

// =====================================================================
//  阶段 1 W4.3 Phase 3 (2026-09-04): per-kind 编辑 factory
// =====================================================================

LayerCommand* LayerCommand::makeSetText(LayerStack *stack, int index, const QString &oldText)
{
    auto *cmd = new LayerCommand(stack, SetText, index);
    cmd->m_strVal = oldText;
    cmd->setText(QStringLiteral("Edit Text"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetTextFont(LayerStack *stack, int index,
                                             const QString &oldFamily, int oldSize,
                                             const QColor &oldColor)
{
    auto *cmd = new LayerCommand(stack, SetTextFont, index);
    cmd->m_intVal = oldSize;       // 重用 m_intVal 存 size
    cmd->m_strVal = oldFamily;     // 重用 m_strVal 存 family
    cmd->m_oldColor = oldColor;
    cmd->setText(QStringLiteral("Edit Text Font"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetSmartObject(LayerStack *stack, int index,
                                                const QString &oldPath, bool oldEmbed)
{
    auto *cmd = new LayerCommand(stack, SetSmartObject, index);
    cmd->m_boolVal = oldEmbed;     // 重用 m_boolVal 存 embed
    cmd->m_strVal = oldPath;       // 重用 m_strVal 存 path
    cmd->setText(QStringLiteral("Edit Smart Object"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetAdjustmentType(LayerStack *stack, int index, const QString &oldType)
{
    auto *cmd = new LayerCommand(stack, SetAdjustmentType, index);
    cmd->m_strVal = oldType;
    cmd->setText(QStringLiteral("Edit Adjustment Type"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetAdjustmentLut(LayerStack *stack, int index, const cv::Mat &oldLut)
{
    auto *cmd = new LayerCommand(stack, SetAdjustmentLut, index);
    cmd->m_oldMat = oldLut.empty() ? cv::Mat() : oldLut.clone();
    cmd->setText(QStringLiteral("Edit Adjustment LUT"));
    return cmd;
}

// P0-3.3 (2026-09-08): 升级版 SetAdjustmentLut factory
//   接受 host + newImage, undo/redo 走 replaceCurrentImage 恢复 image
//   m_oldMat 在这里重定义为"before image 快照" (跟 layer.adjustmentLut 字段名同名但实际存 image)
//   m_newMat = "after image 快照" (仅在 host 模式有意义, 旧版不用)
//   用法:
//     cmd = LayerCommand::makeSetAdjustmentLut(stack, index, beforeImg, afterImg, host);
//     host->setCurrentImage(afterImg);  // 推 command 前先应用新值 (跟 P0-3.2 现有流程一致)
//     stack->push(cmd);                  // undo 走 host->replaceCurrentImage(beforeImg)
//   注意: 不在这里面调 setCurrentImage, 跟 P0-3.2 factory 风格一致 (factory 只存, apply 在 push 前后做)
LayerCommand* LayerCommand::makeSetAdjustmentLut(LayerStack *stack, int index,
                                                  const cv::Mat &oldImage,
                                                  const cv::Mat &newImage,
                                                  ImageWindow *host)
{
    auto *cmd = new LayerCommand(stack, SetAdjustmentLut, index);
    cmd->m_oldMat = oldImage.empty() ? cv::Mat() : oldImage.clone();
    cmd->m_newMat = newImage.empty() ? cv::Mat() : newImage.clone();
    cmd->m_host = host;
    cmd->setText(QStringLiteral("Edit Adjustment LUT"));
    return cmd;
}

// =====================================================================
//  阶段 1 W4.4 Phase 4 (2026-09-04): 蒙版 factory
// =====================================================================

LayerCommand* LayerCommand::makeAddMask(LayerStack *stack, int index,
                                         const cv::Mat &oldMask, bool wasEnabled)
{
    auto *cmd = new LayerCommand(stack, AddMask, index);
    cmd->m_oldMat = oldMask.empty() ? cv::Mat() : oldMask.clone();
    cmd->m_boolVal = wasEnabled;
    cmd->setText(QStringLiteral("Add Mask"));
    return cmd;
}

LayerCommand* LayerCommand::makeClearMask(LayerStack *stack, int index,
                                           const cv::Mat &oldMask, bool wasEnabled)
{
    auto *cmd = new LayerCommand(stack, ClearMask, index);
    cmd->m_oldMat = oldMask.empty() ? cv::Mat() : oldMask.clone();
    cmd->m_boolVal = wasEnabled;
    cmd->setText(QStringLiteral("Clear Mask"));
    return cmd;
}

LayerCommand* LayerCommand::makeEnableMask(LayerStack *stack, int index, bool oldEnabled)
{
    auto *cmd = new LayerCommand(stack, EnableMask, index);
    cmd->m_boolVal = oldEnabled;
    cmd->setText(QStringLiteral("Toggle Mask"));
    return cmd;
}

// 阶段 1 W4.4 Phase 5 (2026-09-04): 智能对象 link↔embed 切换 factory
LayerCommand* LayerCommand::makeToggleSmartObjectEmbed(LayerStack *stack, int index, bool oldEmbedded)
{
    auto *cmd = new LayerCommand(stack, ToggleSmartObjectEmbed, index);
    cmd->m_boolVal = oldEmbedded;
    cmd->setText(QStringLiteral("Toggle Smart Object Embed"));
    return cmd;
}

// =====================================================================
//  undo / redo
// =====================================================================

void LayerCommand::undo()
{
    if (!m_stack) return;
    switch (m_op) {
    case Add: {
        // 删刚加的
        m_stack->removeLayer(m_stack->count() - 1);
        break;
    }
    case Remove: {
        // 恢复
        m_stack->addLayer(m_layer);
        break;
    }
    case Move: {
        // m_index = 被移动 layer 的**原位置** (push 时存)
        //   redo: moveUp(m_index-1) → layer 从 m_index 移到 m_index+1
        //   undo: moveDown(m_index+1) → layer 从 m_index+1 移回 m_index (m_index+1 减 1 越界则调 moveDown(m_index))
        if (m_intVal > 0 && m_index >= 0) m_stack->moveDown(m_index + 1);
        else if (m_intVal < 0)            m_stack->moveUp(m_index - 1);
        break;
    }
    case Opacity: {
        // m_floatVal = old value (push 时存的)
        // undo 走旧值, redo 走新值 (m_floatNew)
        auto l = m_stack->at(m_index);
        if (l) l->opacity = m_floatVal;
        break;
    }
    case Visible: {
        auto l = m_stack->at(m_index);
        if (l) l->visible = m_boolVal;
        break;
    }
    case Locked: {
        auto l = m_stack->at(m_index);
        if (l) l->locked = m_boolVal;
        break;
    }
    case Linked: {
        auto l = m_stack->at(m_index);
        if (l) l->isLinked = m_boolVal;
        break;
    }
    case Blend: {
        auto l = m_stack->at(m_index);
        if (l) l->blend = static_cast<Layer::BlendMode>(m_intVal);
        break;
    }
    case Rename: {
        auto l = m_stack->at(m_index);
        if (l) l->name = m_strVal;
        break;
    }
    case Merge: {
        // 拆分: 把上层恢复, 下层还原
        //   现在下层 image 是 m_mergeAfter.image, 上层已删
        //   undo: 下层还原 m_mergeBefore.image, 上层 addLayer(upperLayer)
        //   Phase 1 简化: 不知道 upper 的原 Layer (没存), 不可逆 undo
        //   简化: 仅恢复下层 image, 不重建上层
        if (auto target = m_stack->at(m_index - 1)) {
            target->image = m_mergeBefore.image.clone();
        }
        break;
    }
    case Group: {
        // 恢复 link 状态
        for (int i = 0; i < m_groupBefore.size(); ++i) {
            if (auto l = m_stack->at(m_index + i)) {
                l->isLinked = m_groupBefore[i];
            }
        }
        break;
    }
    case Ungroup: {
        // 恢复 link 状态
        for (int i = 0; i < m_ungroupBefore.size(); ++i) {
            if (auto l = m_stack->at(i)) {
                l->isLinked = m_ungroupBefore[i];
            }
        }
        break;
    }
    case SetText: {
        auto l = m_stack->at(m_index);
        if (l) l->text = m_strVal;
        break;
    }
    case SetTextFont: {
        auto l = m_stack->at(m_index);
        if (l) {
            l->fontFamily = m_strVal;
            l->fontSize = m_intVal;
            l->textColor = m_oldColor;
        }
        break;
    }
    case SetSmartObject: {
        auto l = m_stack->at(m_index);
        if (l) {
            l->sourceFilePath = m_strVal;
            l->sourceEmbedded = m_boolVal;
        }
        break;
    }
    case SetAdjustmentType: {
        auto l = m_stack->at(m_index);
        if (l) l->adjustmentType = m_strVal;
        break;
    }
    case SetAdjustmentLut: {
        // P0-3.3 (2026-09-08) 分支: m_host 非空 → 走 image-based undo (P0-3.2 AdjustmentPanel 用法)
        //   m_host 走 replaceCurrentImage 恢复 image (跟 ImageEditCommand::undo 等价, 但用 layer 命名空间)
        //   m_host 为空 → 走原 l->adjustmentLut 路径 (P0-3.2 之前的用法, 兼容)
        if (m_host) {
            if (!m_oldMat.empty()) m_host->replaceCurrentImage(m_oldMat);
        } else {
            auto l = m_stack->at(m_index);
            if (l && !m_oldMat.empty()) l->adjustmentLut = m_oldMat.clone();
        }
        break;
    }
    case AddMask: {
        // undo: 恢复原 mask (无就置空) + 原 enabled
        auto l = m_stack->at(m_index);
        if (l) {
            l->layerMask = m_oldMat.empty() ? cv::Mat() : m_oldMat.clone();
            l->maskEnabled = m_boolVal;
        }
        break;
    }
    case ClearMask: {
        // undo: 恢复 mask + enabled
        auto l = m_stack->at(m_index);
        if (l) {
            l->layerMask = m_oldMat.empty() ? cv::Mat() : m_oldMat.clone();
            l->maskEnabled = m_boolVal;
        }
        break;
    }
    case EnableMask: {
        auto l = m_stack->at(m_index);
        if (l) l->maskEnabled = m_boolVal;
        break;
    }
    case ToggleSmartObjectEmbed: {
        // undo: 还原 old embedded
        auto l = m_stack->at(m_index);
        if (l && l->kind == Layer::SmartObject) {
            l->sourceEmbedded = m_boolVal;
            // 注: cache 文件可能已不一致, 但 render 时会重新复制/删除
        }
        break;
    }
    }
}

void LayerCommand::redo()
{
    if (!m_stack) return;
    switch (m_op) {
    case Add: {
        m_stack->addLayer(m_layer);
        break;
    }
    case Remove: {
        // 找到原 layer (用 image 对比简单点; Phase 1 简化: 按 m_layer.image dataPtr)
        for (int i = 0; i < m_stack->count(); ++i) {
            auto l = m_stack->at(i);
            if (l && l.get() && m_layer.image.data == l->image.data) {
                m_stack->removeLayer(i);
                break;
            }
        }
        break;
    }
    case Move: {
        // Phase 1 简化: undo no-op (避免越界 + 复杂 index 转换)
        //   redo: moveUp(m_index) 或 moveDown(m_index), 越界时 LayerStack 内部返 false
        if (m_intVal > 0) m_stack->moveUp(m_index);
        else if (m_intVal < 0) m_stack->moveDown(m_index);
        break;
    }
    case Opacity: {
        // m_floatVal = old value, redo 路径在 mainwindow 实际调 stack.setOpacity (新值)
        //   LayerCommand::redo 自己不改 opacity (避免和 mainwindow 的 setOpacity 双调)
        //   Phase 1 简化: redo no-op, undo 还原 old value
        break;
    }
    case Visible: {
        auto l = m_stack->at(m_index);
        if (l) l->visible = m_boolVal;
        break;
    }
    case Locked: {
        auto l = m_stack->at(m_index);
        if (l) l->locked = m_boolVal;
        break;
    }
    case Linked: {
        auto l = m_stack->at(m_index);
        if (l) l->isLinked = m_boolVal;
        break;
    }
    case Blend: {
        auto l = m_stack->at(m_index);
        if (l) l->blend = static_cast<Layer::BlendMode>(m_intVal);
        break;
    }
    case Rename: {
        auto l = m_stack->at(m_index);
        if (l) l->name = m_strVal;
        break;
    }
    case Merge: {
        m_stack->mergeDown(m_index);
        break;
    }
    case Group: {
        m_stack->groupLayers(m_index, m_index2, m_intVal);
        break;
    }
    case Ungroup: {
        // 简化: 全部 unlink
        for (int i = 0; i < m_stack->count(); ++i) {
            if (auto l = m_stack->at(i)) l->isLinked = false;
        }
        break;
    }
    case SetText:
    case SetTextFont:
    case SetSmartObject:
    case SetAdjustmentType:
    case AddMask:
    case ClearMask:
    case EnableMask:
    case ToggleSmartObjectEmbed:
        // Phase 3/4/5 简化: redo no-op (跟 Opacity 一致)
        //   真实 mainwindow 流程: push(cmd 存 old) → setXxx(new) → undo 还原 old → setXxx(new) → redo no-op
        break;
    case SetAdjustmentLut:
        // P0-3.3 (2026-09-08) 分支: m_host 非空 → redo 走 image-based
        //   (跟 undo 对称, m_newMat 是 push 之前存好的)
        if (m_host && !m_newMat.empty()) m_host->replaceCurrentImage(m_newMat);
        break;
    }
}

} // namespace layers
