#include "LiquifyCommand.h"

#include "../../imagewindow.h"
#include "logger.h"

namespace filters::liquify {

LiquifyCommand::LiquifyCommand(ImageWindow* host, int layerIndex,
                               const cv::Mat& before, const cv::Mat& after,
                               QUndoCommand* parent)
    : QUndoCommand(parent),
      m_host(host),
      m_layerIndex(layerIndex),
      m_before(before.clone()),
      m_after(after.clone()) {
    setText(QObject::tr("Liquify"));
}

void LiquifyCommand::applyToLayer(const cv::Mat& pixels) {
    if (!m_host || pixels.empty()) return;
    if (m_layerIndex < 0) return;
    m_host->writeLayerPixels(m_layerIndex, pixels);
}

void LiquifyCommand::undo() {
    applyToLayer(m_before);
    LOG_INFO("[LiquifyCommand] undo: restored before-image");
}

void LiquifyCommand::redo() {
    applyToLayer(m_after);
    LOG_INFO("[LiquifyCommand] redo: applied liquify result");
}

}  // namespace filters::liquify
