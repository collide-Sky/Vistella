#include "LiquifyCommand.h"

#include "../../imagewindow.h"
#include "logger.h"

namespace filters::liquify {

LiquifyCommand::LiquifyCommand(ImageWindow* host, const cv::Mat& before,
                               const cv::Mat& after, QUndoCommand* parent)
    : QUndoCommand(parent),
      m_host(host),
      m_before(before.clone()),
      m_after(after.clone()) {
    setText(QObject::tr("Liquify"));
}

void LiquifyCommand::undo() {
    if (!m_host) return;
    if (!m_before.empty()) {
        m_host->replaceCurrentImage(m_before);
        LOG_INFO("[LiquifyCommand] undo: restored before-image");
    }
}

void LiquifyCommand::redo() {
    if (!m_host) return;
    if (m_firstRedo) {
        // push() triggers redo() once; after-image already prepared.
        m_firstRedo = false;
    }
    if (!m_after.empty()) {
        m_host->replaceCurrentImage(m_after);
        LOG_INFO("[LiquifyCommand] redo: applied liquify result");
    }
}

}  // namespace filters::liquify
