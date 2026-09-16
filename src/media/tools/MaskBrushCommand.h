#pragma once

#include <QPointer>
#include <QString>
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
class MaskBrushCommand : public QUndoCommand {
public:
    MaskBrushCommand(ImageWindow* host, int layerIndex,
                    cv::Mat before, cv::Mat after,
                    QUndoCommand* parent = nullptr);

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