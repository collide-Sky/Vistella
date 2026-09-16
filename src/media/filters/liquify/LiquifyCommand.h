// SPDX-License-Identifier: MIT
//
// LiquifyCommand - P1.2.8 (2026-09-16)
//
// QUndoCommand for the Liquify filter. Unlike FilterCommand, the after-image
// is pre-computed by the dialog (interactive editing happens there), so the
// constructor just clones both before+after and stores them.
//
//   before-image = the image at dialog open time (clone)
//   after-image  = the image returned by LiquifyDialog::resultImage() (clone)
//   host: ImageWindow for replaceCurrentImage on undo/redo
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
    LiquifyCommand(ImageWindow* host, const cv::Mat& before,
                   const cv::Mat& after, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<ImageWindow> m_host;
    cv::Mat m_before;
    cv::Mat m_after;
    bool m_firstRedo = true;
};

}  // namespace filters::liquify
