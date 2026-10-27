// SPDX-License-Identifier: MIT
//
// LayerBitmapNode impl

#include "LayerBitmapNode.h"

#include "../../imageworker/layers/Layer.h"   // layers::Layer (full def for .image)

#include <algorithm>   // std::min

namespace compositor {

LayerBitmapNode::LayerBitmapNode(const layers::Layer* layer, QString name)
    : m_layer(layer)
    , m_name(name.isEmpty() && layer ? layer->name
                                     : (name.isEmpty() ? QStringLiteral("LayerBitmap") : name))
{
}

QString LayerBitmapNode::name() const
{
    return m_name;
}

cv::Mat LayerBitmapNode::compute(int tileX, int tileY,
                                 const TileGrid& grid,
                                 RenderContext& /*ctx*/)
{
    if (!m_layer) return cv::Mat();
    if (m_layer->image.empty()) return cv::Mat();

    const cv::Rect r = grid.tileRect(tileX, tileY);
    if (r.width <= 0 || r.height <= 0) return cv::Mat();

    // Bounds check: if the layer's image is smaller than the grid image
    // (e.g. layer hasn't been resized to base yet), treat missing regions
    // as transparent/black.
    if (r.x >= m_layer->image.cols || r.y >= m_layer->image.rows) {
        return cv::Mat::zeros(r.height, r.width, m_layer->image.type());
    }

    // Build the tile by copying from the layer's image.
    cv::Mat tile(r.height, r.width, m_layer->image.type());
    const int copyW = std::min(r.width,  m_layer->image.cols - r.x);
    const int copyH = std::min(r.height, m_layer->image.rows - r.y);
    if (copyW <= 0 || copyH <= 0) {
        tile.setTo(cv::Scalar(0, 0, 0));
        return tile;
    }
    cv::Mat srcROI = m_layer->image(cv::Rect(r.x, r.y, copyW, copyH));
    cv::Mat dstROI = tile(cv::Rect(0, 0, copyW, copyH));
    srcROI.copyTo(dstROI);
    // Pad the remainder (partial edge tiles) with black.
    if (copyW < r.width || copyH < r.height) {
        // Zero-init already happened via cv::Mat ctor; nothing to do.
    }
    return tile;
}

void LayerBitmapNode::markLayerEdited()
{
    markDirty();
}

} // namespace compositor
