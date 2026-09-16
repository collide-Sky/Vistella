// SPDX-License-Identifier: MIT
//
// LayerMaskCommand - P1.3.8 (2026-09-16)
//
// QUndoCommand for LayerMask struct mutations on a Layer (P1.3.2+).
// Stores before/after LayerMask snapshots; redo() reapplies the new mask
// to the layer (and clears back to the old one on undo()).
//
// Used by:
//   - addPixelMask / addVectorMask
//   - clearMaskFull
//   - setMaskEnabled / setMaskDensity / setMaskFeather / setMaskInvert
//
#pragma once

#include <QPointer>
#include <QString>
#include <QUndoCommand>

#include "LayerMask.h"

namespace layers {

class LayerStack;

class LayerMaskCommand : public QUndoCommand {
public:
    LayerMaskCommand(LayerStack* stack, int layerIndex,
                     const LayerMask& before, const LayerMask& after,
                     const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    int layerIndex() const { return m_layerIndex; }

private:
    QPointer<LayerStack> m_stack;
    int m_layerIndex = -1;
    LayerMask m_before;
    LayerMask m_after;
    bool m_firstRedo = true;
};

}  // namespace layers
