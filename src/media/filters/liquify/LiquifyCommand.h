// SPDX-License-Identifier: MIT
//
// LiquifyCommand
//
// QUndoCommand for the Liquify filter. Unlike FilterCommand, the after-image
// is pre-computed by the dialog (interactive editing happens there), so the
// constructor just clones both before+after and stores them.
//
//   before-image = the layer pixels at dialog open time (clone)
//   after-image  = the pixels returned by LiquifyDialog::resultImage() (clone)
//   layer index  = the layer the liquify was applied to
//
//   undo/redo write the pixels back into that layer — the composite cache is
//   never touched, it is a render artefact and would be overwritten anyway.
//
#pragma once

#include <QPointer>
#include <QString>
#include <QUndoCommand>
#include <opencv2/core.hpp>

class ImageWindow;

namespace filters::liquify {

class LiquifyCommand : public QUndoCommand {
public:
    LiquifyCommand(ImageWindow* host, int layerIndex, const cv::Mat& before,
                   const cv::Mat& after, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    int layerIndex() const { return m_layerIndex; }

private:
    void applyToLayer(const cv::Mat& pixels);

    QPointer<ImageWindow> m_host;
    int m_layerIndex = -1;
    cv::Mat m_before;
    cv::Mat m_after;
};

}  // namespace filters::liquify
