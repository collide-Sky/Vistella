#pragma once

#include <QPointer>
#include <QString>
#include "../UndoStack.h"
#include <QUndoCommand>
#include <opencv2/core.hpp>

class ImageWindow;

namespace tools {

// QUndoCommand for a single mask brush stroke.
//
// Stores before/after pixel mask snapshots.  The undo/redo swaps the
// snapshot into the layer's mask struct and emits a change signal so
// the canvas repaints.
//
// One stroke == one undo step (not per dab).
class MaskBrushCommand : public QUndoCommand,
                     public vistella::IUndoMemoryFootprint,
                     public vistella::IUndoFirstRedo {
    // M1-7: see LayerCommand::consumeFirstRedo().
    void consumeFirstRedo() override { m_firstRedo = false; }

public:
    MaskBrushCommand(ImageWindow* host, int layerIndex,
                    cv::Mat before, cv::Mat after,
                    QUndoCommand* parent = nullptr);

    // M1-7: bytes held by this command.
    size_t memoryFootprint() const override {
        return matBytes(m_before) + matBytes(m_after);
    }

    static size_t matBytes(const cv::Mat& m) {
        if (m.empty()) return 0;
        return static_cast<size_t>(m.total()) * static_cast<size_t>(m.elemSize());
    }
    void undo() override;
    void redo() override;

    int layerIndex() const { return m_layerIndex; }

private:
    QPointer<ImageWindow> m_host;
    int m_layerIndex = -1;
    cv::Mat m_before;
    cv::Mat m_after;
    bool m_firstRedo = true;
};

}  // namespace tools