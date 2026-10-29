// =============================================================
// LayerCommand 实现
//  详见 LayerCommand.h
// SetAdjustmentLut 升级 image-based undo/redo
// =============================================================

#include "LayerCommand.h"

// 实现 image-based undo/redo 需要调 ImageWindow::replaceCurrentImage
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
    // Add: 调用方已经把这层加进栈了, 它就在栈顶
    if (op == Add && m_stack) {
        m_index = m_stack->count() - 1;
        // 记住新层的稳定 id: 撤销时按 id 删, 下标漂移也删对层 (LYR-06).
        if (auto added = m_stack->at(m_index)) m_targetId = added->stableId;
    }
}

LayerCommand* LayerCommand::makeRemove(LayerStack *stack, int index)
{
    if (!stack) return nullptr;
    auto l = stack->at(index);
    if (!l) return nullptr;
    auto *cmd = new LayerCommand(stack, Remove, *l);   // 保存完整 layer (含 cv::Mat clone)
    cmd->m_index = index;                              // 记住原位, 撤销时放回原位
    cmd->m_targetId = l->stableId;                     // 稳定身份: 重做时按 id 找层
    return cmd;
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
    captureAfter();
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
    captureAfter();
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, int intVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_intVal(intVal)
{
    setText(QStringLiteral("Set Blend"));
    captureAfter();
}

LayerCommand::LayerCommand(LayerStack *stack, Op op, int index, const QString &strVal, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(op), m_index(index), m_strVal(strVal)
{
    setText(QStringLiteral("Rename Layer"));
    captureAfter();
}

LayerCommand::LayerCommand(LayerStack *stack, int mergeIndex, QUndoCommand *parent)
    : QUndoCommand(parent), m_stack(stack), m_op(Merge), m_index(mergeIndex)
{
    setText(QStringLiteral("Merge Down"));
    if (!m_stack) return;
    // 保存 merge target (下层) before/after
    auto target = m_stack->at(mergeIndex - 1);
    if (target) m_targetId = target->stableId;   // 下层 (合并目标) 的 id
    if (target) m_mergeBefore = *target;   // 下层 before
    auto src = m_stack->at(mergeIndex);
    if (src) m_secondId = src->stableId;          // 上层 (被合并掉) 的 id
    if (src) {
        m_mergeUpper = *src;               // 上层: 撤销时插回原来的位置
        // 计算 mergeAfter: target 跟 src 按 src.opacity blend
        //   两层的尺寸/类型对不齐时 addWeighted 会抛 cv::Exception,
        //   先判一次, 对不齐就退化成"以下层为准", 至少不崩.
        cv::Mat merged;
        const float a = src->opacity;
        if (!m_mergeBefore.image.empty() && !src->image.empty()
            && m_mergeBefore.image.size() == src->image.size()
            && m_mergeBefore.image.type() == src->image.type()) {
            cv::addWeighted(m_mergeBefore.image, 1.0f - a, src->image, a, 0.0, merged);
        } else {
            merged = m_mergeBefore.image.clone();
        }
        m_mergeAfter = m_mergeBefore;
        m_mergeAfter.image = merged;
    }
}

// simplified Group/Ungroup constructors
//   (link flag based, lines 94-114 in the original file) removed. The new
//   factories below integrate with P1.5.2 LayerKind::Group + m_groups.
//
//   Pattern matches existing SmartFilter factories: factory performs the
//   initial state change itself; redo() is a no-op; undo() reverses via
//   flattenGroup / reinsertGroup. Caller does not need to call mergeIntoGroup
//   or flattenGroup separately.
//
//   makeMergeIntoGroup: factory applies mergeIntoGroup, captures groupIdx
//     and the indices used. undo() flattens the resulting Group.
//
//   makeFlattenGroup: factory applies flattenGroup, captures groupIdx +
//     children snapshot + atIndex. undo() restores via reinsertGroup.

LayerCommand* LayerCommand::makeMergeIntoGroup(LayerStack *stack,
                                                QList<int> indices)
{
    auto *cmd = new LayerCommand(stack, Group, -1);
    cmd->setText(QStringLiteral("Group Layers"));
    cmd->m_groupIndices = indices;
    cmd->m_groupChildCount = indices.size();
    // Apply merge now so the caller doesn't have to.
    cmd->m_index = stack ? stack->mergeIntoGroup(indices) : -1;
    return cmd;
}

LayerCommand* LayerCommand::makeFlattenGroup(LayerStack *stack, int groupIdx)
{
    auto *cmd = new LayerCommand(stack, Ungroup, -1);
    cmd->setText(QStringLiteral("Ungroup"));
    cmd->m_index = groupIdx;
    cmd->m_flattenAtIndex = groupIdx;
    // Apply flatten now + capture children snapshot for undo.
    if (stack && groupIdx >= 0 && groupIdx < stack->count()) {
        const LayerId gid = stack->idOf(groupIdx);
        cmd->m_flattenChildren = stack->groupChildrenOf(gid);  // deep copy
        cmd->m_groupChildCount = static_cast<int>(cmd->m_flattenChildren.size());
        stack->flattenGroup(groupIdx);
    }
    return cmd;
}

// Move factory pattern. Previously callers did
//   push(new LayerCommand(stack, sel, +1)) + m_layerStack->moveUp(sel)
//   which double-moved (push's redo() called moveUp, then caller called
//   moveUp again). The factories below apply the move themselves; redo()
//   is a no-op; undo() reverses. Matches Group/Ungroup factories above.
//
//   m_index holds the pre-move index (where the layer sat before
//   moveUp/moveDown moved it). undo() reverses using m_intVal sign.
//
//   Caveat: redo() is a no-op, so after undo the user cannot redo to
//   restore the move. Same Phase 3/4/5 simplification tradeoff as
//   SetText / Opacity (caller-side setXxx after push is the redo path).
LayerCommand* LayerCommand::makeMoveUp(LayerStack *stack, int index)
{
    auto *cmd = new LayerCommand(stack, index, +1);
    cmd->setText(QStringLiteral("Move Layer Up"));
    // Apply move now so the caller doesn't have to.
    if (stack) stack->moveUp(index);
    return cmd;
}

LayerCommand* LayerCommand::makeMoveDown(LayerStack *stack, int index)
{
    auto *cmd = new LayerCommand(stack, index, -1);
    cmd->setText(QStringLiteral("Move Layer Down"));
    // Apply move now so the caller doesn't have to.
    if (stack) stack->moveDown(index);
    return cmd;
}

//  3 参 (stack, op, index) 专用构造器
//   避免跟 (stack, int, int) Move 构造器歧义
LayerCommand::LayerCommand(LayerStack *stack, Op op, int index)
    : QUndoCommand(), m_stack(stack), m_op(op), m_index(index)
{
    // 留给 factory 设置 m_strVal / m_oldMat / m_oldColor
    // 新值从图层上读 (调用方已经先改过图层了)
    captureAfter();
}

// =====================================================================
//   per-kind 编辑 factory
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

//  智能对象 link↔embed 切换 factory
LayerCommand* LayerCommand::makeToggleSmartObjectEmbed(LayerStack *stack, int index, bool oldEmbedded)
{
    auto *cmd = new LayerCommand(stack, ToggleSmartObjectEmbed, index);
    cmd->m_boolVal = oldEmbedded;
    cmd->setText(QStringLiteral("Toggle Smart Object Embed"));
    return cmd;
}

// =====================================================================
//  Convert / Rasterize / Transform factories
// =====================================================================

// ConvertToSmartObject: caller must invoke convertToSmartObject() BEFORE pushing
//   this command; on push we capture the BEFORE state in m_layer for undo.
//   redo() re-applies the conversion from m_layer (Bitmap image) by calling
//   convertToSmartObject with the captured before-image encoded to cache.
//   This matches the "factory only stores; apply happens around push" pattern
//   used by other LayerCommand factories.
LayerCommand* LayerCommand::makeConvertToSmartObject(LayerStack *stack, int index,
                                                      const Layer &beforeLayer)
{
    auto *cmd = new LayerCommand(stack, ConvertToSmartObject, index);
    cmd->m_layer = beforeLayer;  // deep copy: cv::Mat + QString + payload
    cmd->setText(QStringLiteral("Convert to Smart Object"));
    return cmd;
}

// RasterizeSmartObject: similar pattern. beforeLayer captures the SmartObject
//   state (sourceFilePath / sourceEmbedded / transform / hasTransform) for undo.
LayerCommand* LayerCommand::makeRasterizeSmartObject(LayerStack *stack, int index,
                                                       const Layer &beforeLayer)
{
    auto *cmd = new LayerCommand(stack, RasterizeSmartObject, index);
    cmd->m_layer = beforeLayer;
    cmd->setText(QStringLiteral("Rasterize Smart Object"));
    return cmd;
}

// SetSmartObjectTransform: stores old + new QTransform and has flag pairs.
LayerCommand* LayerCommand::makeSetSmartObjectTransform(LayerStack *stack, int index,
                                                         const QTransform &oldT,
                                                         bool oldHas,
                                                         const QTransform &newT,
                                                         bool newHas)
{
    auto *cmd = new LayerCommand(stack, SetSmartObjectTransform, index);
    cmd->m_oldTransform = oldT;
    cmd->m_oldHasTransform = oldHas;
    cmd->m_newTransform = newT;
    cmd->m_newHasTransform = newHas;
    cmd->setText(QStringLiteral("Set Smart Object Transform"));
    return cmd;
}

// =====================================================================
//  SmartFilter chain undo factories
// =====================================================================
//
// AppendSmartFilter: caller has already called appendSmartFilter() before
//   pushing this command; filterIdx is the new layer's stack index. We
//   store m_strVal = filterType so undo can verify which type to remove.
//   redo is a no-op (matches other LayerCommand factories — push happens
//   after the apply).
LayerCommand* LayerCommand::makeAppendSmartFilter(LayerStack *stack, int filterIdx,
                                                  const QString &filterType,
                                                  double oldStrength)
{
    auto *cmd = new LayerCommand(stack, AppendSmartFilter, filterIdx);
    cmd->m_strVal = filterType;
    cmd->m_floatVal = oldStrength;
    cmd->setText(QStringLiteral("Append Smart Filter"));
    return cmd;
}

// RemoveSmartFilter: caller has already called removeSmartFilter() before
//   pushing. m_layer holds the full removed layer (cv::Mat clone +
//   filterType / filterStrength / parentSmartIndex / filterSlotIndex)
//   so undo can restore by addLayer() back at the original slot.
LayerCommand* LayerCommand::makeRemoveSmartFilter(LayerStack *stack, int filterIdx,
                                                  const Layer &oldLayer)
{
    auto *cmd = new LayerCommand(stack, RemoveSmartFilter, filterIdx);
    cmd->m_layer = oldLayer;
    cmd->setText(QStringLiteral("Remove Smart Filter"));
    return cmd;
}

// MoveSmartFilter: m_intVal holds the new slot (post-move); m_oldSlot
//   holds the slot before move. redo no-op (caller has already moved);
//   undo moves the filter back to m_oldSlot.
LayerCommand* LayerCommand::makeMoveSmartFilter(LayerStack *stack, int filterIdx,
                                                 int oldSlot)
{
    auto *cmd = new LayerCommand(stack, MoveSmartFilter, filterIdx);
    cmd->m_intVal = -1;  // post-move slot is unknown; undo only needs old slot
    cmd->m_oldSlot = oldSlot;
    cmd->setText(QStringLiteral("Reorder Smart Filter"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetSmartFilterEnabled(LayerStack *stack, int filterIdx,
                                                      bool oldEnabled)
{
    auto *cmd = new LayerCommand(stack, SetSmartFilterEnabled, filterIdx);
    cmd->m_boolVal = oldEnabled;
    cmd->setText(QStringLiteral("Toggle Smart Filter"));
    return cmd;
}

LayerCommand* LayerCommand::makeSetSmartFilterStrength(LayerStack *stack, int filterIdx,
                                                       double oldStrength)
{
    auto *cmd = new LayerCommand(stack, SetSmartFilterStrength, filterIdx);
    cmd->m_floatVal = oldStrength;
    cmd->setText(QStringLiteral("Set Smart Filter Strength"));
    return cmd;
}

//  工具编辑: 替换某图层整层像素.
//    before/after 都是调用方给的快照; 调用方已经把 after 应用到图层上,
//    所以 push 触发的首个 redo 不用再应用一次.
LayerCommand* LayerCommand::makeSetLayerImage(LayerStack *stack, int index,
                                               const cv::Mat &before,
                                               const cv::Mat &after,
                                               const QString &text)
{
    auto *cmd = new LayerCommand(stack, SetLayerImage, index);
    cmd->m_oldMat = before.empty() ? cv::Mat() : before.clone();
    cmd->m_newMat = after.empty() ? cv::Mat() : after.clone();
    cmd->setText(text.isEmpty() ? QStringLiteral("Edit Layer Pixels") : text);
    return cmd;
}

// =====================================================================
//  undo / redo
// =====================================================================
//
//  统一模型:
//    undo() → applyState(false) 应用"旧值"
//    redo() → applyState(true)  应用"新值"
//  两者走同一套 LayerStack 公开 API, 末尾统一 notifyChanged():
//    发 layerChanged (图层面板刷新) + 作废渲染缓存 + 重绘画布.
//
//  首个 redo 的特殊性:
//    QUndoStack::push 会立刻调一次 redo(). 大部分 Op 的"向前"动作是调用方
//    自己做的 (先 setOpacity 再 push), 所以首个 redo 必须是 no-op, 否则会
//    重复应用一次. Remove / Merge 是例外 —— 它们的向前动作由 command 自己
//    执行, 首个 redo 必须真的做.
//
//  状态变更都落在 LayerStack 的 API 上, 由 LayerStack 负责发信号;
//  少数直接改 Layer 字段的路径 (类型转换还原 / 蒙版还原) 由 notifyChanged()
//  统一补发一次 layerChanged.

void LayerCommand::undo()
{
    if (!m_stack) return;
    applyState(false);
    notifyChanged();
}

void LayerCommand::redo()
{
    if (!m_stack) return;
    const bool first = m_firstRedo;
    m_firstRedo = false;
    // 调用方已经应用过新值的 Op: 首个 redo 不能再应用一次
    if (first && !selfApplied()) return;
    applyState(true);
    notifyChanged();
}

//  这个 Op 的"向前"动作是不是由 command 自己执行.
//    Remove / Merge: 是 —— 调用方只 push, 动作留给 command.
//    其余: 否 —— 调用方已经先改过图层了.
bool LayerCommand::selfApplied() const
{
    return m_op == Remove || m_op == Merge;
}

//  构造时从图层上读出"新值".
//    调用方的约定是"先改图层, 再 push command", 所以构造时图层上已经是
//    新值, 这里读出来存着, redo 时原样写回去.
void LayerCommand::captureAfter()
{
    if (!m_stack || m_index < 0) return;
    auto l = m_stack->at(m_index);
    if (!l) return;
    switch (m_op) {
    case Opacity:      m_floatNew = l->opacity; break;
    case Visible:      m_boolNew  = l->visible; break;
    case Locked:       m_boolNew  = l->locked; break;
    case Linked:       m_boolNew  = l->isLinked; break;
    case Blend:        m_intNew   = static_cast<int>(l->blend); break;
    case Rename:       m_strNew   = l->name; break;
    case SetText:      m_strNew   = l->text; break;
    case SetTextFont:
        m_strNew   = l->fontFamily;
        m_intNew   = l->fontSize;
        m_newColor = l->textColor;
        break;
    case SetSmartObject:
        m_strNew  = l->sourceFilePath;
        m_boolNew = l->sourceEmbedded;
        break;
    case SetAdjustmentType: m_strNew = l->adjustmentType; break;
    case SetAdjustmentLut:
        // 新值 = 调整图层当前的 LUT (调用方先写新值, 再 push command)
        m_newMat = l->adjustmentLut.clone();
        break;
    case ToggleSmartObjectEmbed: m_boolNew = l->sourceEmbedded; break;
    case ConvertToSmartObject:
    case RasterizeSmartObject:
        m_afterLayer = *l;
        break;
    case SetSmartObjectTransform:
        m_newTransform    = l->transform;
        m_newHasTransform = l->hasTransform;
        break;
    case AppendSmartFilter:
        // 重新挂回链上时要用完整的 filter 图层
        m_layer   = *l;
        m_intNew  = l->filterSlotIndex;
        break;
    case RemoveSmartFilter:
        // 结构操作: 新状态 = "filter 已经不在栈里", 没有可读的新值
        break;
    case MoveSmartFilter:
        m_intNew = l->filterSlotIndex;
        break;
    case SetSmartFilterEnabled:
        m_boolNew = l->opacity > 0.0f;
        break;
    case SetSmartFilterStrength:
        m_doubleNew = l->filterStrength;
        break;
    case SetLayerImage:
        // 新旧像素由 factory 显式传入, 不从图层上读
        break;
    default:
        break;
    }
}

//  按 Op 应用"旧值"(after=false) 或"新值"(after=true).
//    值的来源:
//      - 旧值: 调用方 push 时传进来的 (m_floatVal / m_boolVal / m_intVal /
//              m_strVal / m_oldColor / m_oldMat / m_oldTransform ...)
//      - 新值: 构造时 captureAfter() 从图层上读出来的 (m_floatNew / ...)
void LayerCommand::applyState(bool after)
{
    if (!m_stack) return;
    switch (m_op) {
    case Add: {
        // 调用方已经把图层加在栈顶; 撤销 = 删掉它, 重做 = 放回原位
        if (after) {
            m_stack->insertLayer(m_index, m_layer);
        } else {
            // 按 id 删 (下标可能已经漂移, 按旧下标会删错层) (LYR-06)
            const int idx = m_stack->indexById(m_targetId);
            if (idx >= 0) m_stack->removeLayer(idx);
        }
        break;
    }
    case Remove: {
        // 向前 = 删掉这层 (由 command 自己执行), 撤销 = 放回原来的位置
        if (after) {
            // 按 id 删 (下标可能已经漂移, 按旧下标会删错层) (LYR-06)
            const int idx = m_stack->indexById(m_targetId);
            if (idx >= 0) {
                m_index = idx;
                m_stack->removeLayer(idx);
            }
        } else {
            // 快照里带着原 stableId, 放回去还是同一个身份
            m_stack->insertLayer(m_index, m_layer);
        }
        break;
    }
    case Move: {
        // m_index = 移动前的下标, m_intVal = 方向 (+1 上 / -1 下)
        if (after) {
            if (m_intVal > 0)      m_stack->moveUp(m_index);
            else if (m_intVal < 0) m_stack->moveDown(m_index);
        } else {
            if (m_intVal > 0)      m_stack->moveDown(m_index + 1);
            else if (m_intVal < 0) m_stack->moveUp(m_index - 1);
        }
        break;
    }
    case Opacity:
        m_stack->setOpacity(m_index, after ? m_floatNew : m_floatVal);
        break;
    case Visible:
        m_stack->setVisible(m_index, after ? m_boolNew : m_boolVal);
        break;
    case Locked:
        m_stack->setLocked(m_index, after ? m_boolNew : m_boolVal);
        break;
    case Linked:
        m_stack->setLinked(m_index, after ? m_boolNew : m_boolVal);
        break;
    case Blend:
        m_stack->setBlend(m_index,
                          static_cast<Layer::BlendMode>(after ? m_intNew : m_intVal));
        break;
    case Rename:
        m_stack->rename(m_index, after ? m_strNew : m_strVal);
        break;
    case Merge: {
        // 向前: 下层写合并结果, 上层删掉
        // 撤销: 下层还原, 上层插回原来的位置
        // 上下层都按 id 定位 (LYR-06): 下标会随增删移动漂移
        const int targetIdx = m_stack->indexById(m_targetId);
        if (targetIdx >= 0) {
            if (auto t = m_stack->at(targetIdx)) {
                t->image = after ? m_mergeAfter.image.clone()
                                 : m_mergeBefore.image.clone();
                // 下层是被直接改写的, 补发一次信号
                m_stack->notifyLayerChanged(targetIdx);
            }
        }
        if (after) {
            const int upperIdx = m_stack->indexById(m_secondId);
            if (upperIdx >= 0) m_stack->removeLayer(upperIdx);
        } else {
            // 快照带着原 stableId, 插回去还是同一个身份
            m_stack->insertLayer(m_index, m_mergeUpper);
        }
        break;
    }
    case Group: {
        // 向前: 把连续的子层重新打包成一个 Group
        // 撤销: 解散这个 Group, 子层回到栈里
        if (after) {
            QList<int> idx;
            for (int i = 0; i < m_groupChildCount; ++i) idx.append(m_index + i);
            const int g = m_stack->mergeIntoGroup(idx);
            if (g >= 0) m_index = g;
        } else {
            m_stack->flattenGroup(m_index);
        }
        break;
    }
    case Ungroup: {
        // 向前: 解散 Group (子层摊平回栈里)
        // 撤销: 把子层收回去, 重新立一个 Group
        if (after) {
            m_stack->flattenGroup(m_index);
        } else if (!m_flattenChildren.empty() && m_flattenAtIndex >= 0
                   && m_flattenAtIndex <= m_stack->count()) {
            // 按 id 摘子层 (不是按位置): 位置会漂移, 按位置摘会摘错层 (LYR-18)
            for (const auto &child : m_flattenChildren) {
                if (!child) continue;
                const int idx = m_stack->indexById(child->stableId);
                if (idx >= 0) m_stack->removeLayer(idx);
            }
            // 传副本而不是 move: 命令要留着这份快照, 否则 undo→redo→undo
            // 第二遍就还原不回来
            m_stack->reinsertGroup(m_flattenAtIndex, m_flattenChildren);
        }
        break;
    }
    case SetText:
        m_stack->setText(m_index, after ? m_strNew : m_strVal);
        break;
    case SetTextFont:
        m_stack->setTextFont(m_index,
                             after ? m_strNew : m_strVal,
                             after ? m_intNew : m_intVal,
                             after ? m_newColor : m_oldColor);
        break;
    case SetSmartObject:
        m_stack->setSmartObjectSource(m_index,
                                      after ? m_strNew : m_strVal,
                                      after ? m_boolNew : m_boolVal);
        break;
    case SetAdjustmentType:
        m_stack->setAdjustmentType(m_index, after ? m_strNew : m_strVal);
        break;
    case SetAdjustmentLut: {
        // 调整图层存的是 LUT, 不是像素 —— 底图像素永远不被改写
        auto l = m_stack->at(m_index);
        if (l) {
            const cv::Mat &lut = after ? m_newMat : m_oldMat;
            l->adjustmentLut = lut.empty() ? cv::Mat() : lut.clone();
            m_stack->notifyLayerChanged(m_index);
        }
        break;
    }
    case ToggleSmartObjectEmbed: {
        auto l = m_stack->at(m_index);
        if (l && l->kind == Layer::SmartObject) {
            l->sourceEmbedded = after ? m_boolNew : m_boolVal;
            m_stack->notifyLayerChanged(m_index);
        }
        break;
    }
    case ConvertToSmartObject:
    case RasterizeSmartObject: {
        // 这两个是"整层状态切换", 直接把快照写回图层
        const Layer &snap = after ? m_afterLayer : m_layer;
        auto l = m_stack->at(m_index);
        if (l) {
            l->kind = snap.kind;
            l->name = snap.name;
            l->image = snap.image.clone();
            l->sourceFilePath = snap.sourceFilePath;
            l->sourceEmbedded = snap.sourceEmbedded;
            l->transform = snap.transform;
            l->hasTransform = snap.hasTransform;
            m_stack->notifyLayerChanged(m_index);
        }
        break;
    }
    case SetSmartObjectTransform: {
        auto l = m_stack->at(m_index);
        if (l) {
            l->transform    = after ? m_newTransform : m_oldTransform;
            l->hasTransform = after ? m_newHasTransform : m_oldHasTransform;
            m_stack->notifyLayerChanged(m_index);
        }
        break;
    }
    case AppendSmartFilter: {
        // 向前: 把 filter 子层挂回父 SmartObject 的链上
        // 撤销: 从链上摘掉
        if (after) {
            const int idx = m_stack->reinsertSmartFilter(m_layer, m_layer.filterSlotIndex);
            if (idx >= 0) m_index = idx;
        } else {
            auto l = m_stack->at(m_index);
            if (l && l->kind == Layer::SmartFilter && l->parentSmartId != 0) {
                m_stack->removeSmartFilter(l->parentSmartId, l->stableId);
            }
        }
        break;
    }
    case RemoveSmartFilter: {
        // 向前: 把 filter 子层摘掉
        // 撤销: 挂回链上原来的槽位
        if (after) {
            auto l = m_stack->at(m_index);
            if (l && l->kind == Layer::SmartFilter && l->parentSmartId != 0) {
                m_stack->removeSmartFilter(l->parentSmartId, l->stableId);
            }
        } else {
            const int idx = m_stack->reinsertSmartFilter(m_layer, m_layer.filterSlotIndex);
            if (idx >= 0) m_index = idx;
        }
        break;
    }
    case MoveSmartFilter: {
        auto l = m_stack->at(m_index);
        if (l && l->kind == Layer::SmartFilter && l->parentSmartId != 0) {
            m_stack->moveSmartFilter(l->parentSmartId, l->stableId,
                                      after ? m_intNew : m_oldSlot);
        }
        break;
    }
    case SetSmartFilterEnabled: {
        auto l = m_stack->at(m_index);
        if (l && l->kind == Layer::SmartFilter && l->parentSmartId != 0) {
            m_stack->setSmartFilterEnabled(l->parentSmartId, l->stableId,
                                            after ? m_boolNew : m_boolVal);
        }
        break;
    }
    case SetSmartFilterStrength: {
        auto l = m_stack->at(m_index);
        if (l && l->kind == Layer::SmartFilter && l->parentSmartId != 0) {
            m_stack->setSmartFilterStrength(l->parentSmartId, l->stableId,
                                             after ? m_doubleNew
                                                   : static_cast<double>(m_floatVal));
        }
        break;
    }
    case SetLayerImage: {
        // 工具编辑: 整层像素替换.
        //   clone 而不是共享: 工具会在图层上就地涂抹, 共享会连带改掉
        //   命令里存的快照.
        auto l = m_stack->at(m_index);
        if (l) {
            const cv::Mat &img = after ? m_newMat : m_oldMat;
            if (!img.empty()) {
                l->image = img.clone();
                m_stack->notifyLayerChanged(m_index);
            }
        }
        break;
    }
    }
}

//  undo / redo 末尾的统一出口.
//    状态变更本身已经发过 layerChanged (setter 路径自带, 直接改字段的路径
//    在 applyState 里补发), 这里负责把画布刷新出来 —— 撤销/重做之后用户
//    必须立刻看到结果, 否则画面停在旧帧.
void LayerCommand::notifyChanged()
{
    if (!m_stack) return;
    // LayerStack 的 parent 就是 ImageWindow (ImageWindow 构造时这么挂的);
    // 单测里 LayerStack 没有 parent, 这里自然跳过.
    if (auto *host = qobject_cast<ImageWindow *>(m_stack->parent())) {
        host->refreshCanvasNow();
    }
}

} // namespace layers
