#include "LayerMaskCommand.h"

#include "LayerStack.h"
#include "logger.h"

namespace layers {

namespace {

// Replace the mask at `index` with `m` (full struct copy including data).
// Helper used by undo() / redo() to swap LayerMask values without touching
// other layers.
void replaceMaskAt(LayerStack* stack, int index, const LayerMask& m) {
    auto l = stack->at(index);
    if (!l) return;
    l->mask = m;
    emit stack->layerChanged(index);
}

}  // namespace

LayerMaskCommand::LayerMaskCommand(LayerStack* stack, int layerIndex,
                                   const LayerMask& before,
                                   const LayerMask& after,
                                   const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent),
      m_stack(stack),
      m_layerIndex(layerIndex),
      m_before(before),
      m_after(after) {
    setText(text);
}

void LayerMaskCommand::undo() {
    if (!m_stack) return;
    replaceMaskAt(m_stack, m_layerIndex, m_before);
    LOG_INFO("[LayerMaskCommand] undo: layer={}", m_layerIndex);
}

void LayerMaskCommand::redo() {
    if (!m_stack) return;
    if (m_firstRedo) {
        // push() will call redo() once; the new state was set by the caller
        // before pushing, so we just suppress the first-redo flag.
        m_firstRedo = false;
        return;
    }
    replaceMaskAt(m_stack, m_layerIndex, m_after);
    LOG_INFO("[LayerMaskCommand] redo: layer={}", m_layerIndex);
}

}  // namespace layers
