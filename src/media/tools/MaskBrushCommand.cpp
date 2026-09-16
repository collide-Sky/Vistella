#include "MaskBrushCommand.h"

#include "imagewindow.h"
#include "imageworker/layers/LayerStack.h"
#include "logger.h"

namespace tools {

MaskBrushCommand::MaskBrushCommand(ImageWindow* host, int layerIndex,
                                   cv::Mat before, cv::Mat after,
                                   QUndoCommand* parent)
    : QUndoCommand(parent),
      m_host(host),
      m_layerIndex(layerIndex),
      m_before(std::move(before)),
      m_after(std::move(after)) {
    setText(QObject::tr("Mask Brush Stroke"));
}

namespace {

void applyToLayer(ImageWindow* host, int idx, const cv::Mat& mask) {
    if (!host) return;
    auto* stack = host->layerStack();
    if (!stack) return;
    auto l = stack->at(idx);
    if (!l) return;
    if (l->mask.kind != layers::LayerMask::Pixel) {
        l->mask.kind = layers::LayerMask::Pixel;
    }
    l->mask.pixel = mask.clone();
    l->mask.enabled = true;
    emit stack->layerChanged(idx);
}

}  // namespace

void MaskBrushCommand::undo() {
    if (!m_host) return;
    applyToLayer(m_host, m_layerIndex, m_before);
    LOG_INFO("[MaskBrushCommand] undo: layer={}", m_layerIndex);
}

void MaskBrushCommand::redo() {
    if (!m_host) return;
    if (m_firstRedo) {
        // push() will call redo() once; the new state was set by the brush
        // tool before pushing. just clear the flag.
        m_firstRedo = false;
        return;
    }
    applyToLayer(m_host, m_layerIndex, m_after);
    LOG_INFO("[MaskBrushCommand] redo: layer={}", m_layerIndex);
}

}  // namespace tools