// SPDX-License-Identifier: MIT
//
// LayerMaskCommand
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
#include "../../UndoStack.h"
#include <QUndoCommand>

#include "LayerMask.h"

namespace layers {

class LayerStack;

class LayerMaskCommand : public QUndoCommand,
                     public vistella::IUndoMemoryFootprint,
                     public vistella::IUndoFirstRedo {
    // M1-7: see LayerCommand::consumeFirstRedo().
    void consumeFirstRedo() override { m_firstRedo = false; }

public:
    LayerMaskCommand(LayerStack* stack, int layerIndex,
                     const LayerMask& before, const LayerMask& after,
                     const QString& text, QUndoCommand* parent = nullptr);

    // M1-7: bytes held by this command (the two mask Mats).
    size_t memoryFootprint() const override {
        return matBytes(m_before.pixel) + matBytes(m_after.pixel);
    }

    static size_t matBytes(const cv::Mat& m) {
        if (m.empty()) return 0;
        return static_cast<size_t>(m.total()) * static_cast<size_t>(m.elemSize());
    }
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
