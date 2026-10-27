// SPDX-License-Identifier: MIT
//
// LayerRasterNode impl

#include "LayerRasterNode.h"

#include "../TileGrid.h"

#include <algorithm>

namespace compositor {

LayerRasterNode::LayerRasterNode(const layers::LayerStack* stack,
                                 layers::LayerPtr layer,
                                 int index,
                                 QString name)
    : m_stack(stack)
    , m_layer(std::move(layer))
    , m_index(index)
    , m_name(std::move(name))
{
}

QString LayerRasterNode::name() const
{
    return m_name;
}

const cv::Mat& LayerRasterNode::raster()
{
    if (m_rasterValid) return m_raster;

    m_raster = cv::Mat();
    if (m_stack && m_layer) {
        m_raster = m_stack->rasterizeLayer(m_layer);
        // The SmartFilter chain rides along with the raster rather than
        // living downstream: blur kernels cross tile seams, so it has
        // to see the full image. Group children (index < 0) never get
        // the chain in the legacy render() either, and this reproduces
        // that exactly.
        if (!m_raster.empty()
            && m_index >= 0
            && m_layer->kind == layers::Layer::SmartObject) {
            m_raster = m_stack->applySmartFilterChain(m_raster, m_index);
        }
    }
    m_rasterValid = true;
    return m_raster;
}

cv::Mat LayerRasterNode::compute(int tileX, int tileY,
                                 const TileGrid& grid,
                                 RenderContext& /*ctx*/)
{
    if (!m_stack || !m_layer) return cv::Mat();

    const cv::Mat& full = raster();
    if (full.empty()) return cv::Mat();

    const cv::Rect r = grid.tileRect(tileX, tileY);
    if (r.width <= 0 || r.height <= 0) return cv::Mat();

    // rasterize() sizes Text / Vector / SmartObject output from
    // LayerStack::canvasSize(), which reads index 0's image — not the
    // base bitmap the grid was built from. Those normally agree (the
    // base layer is index 0), but not always: a stack whose index 0 is
    // a text layer rasterizes to canvasSize()'s 800x600 fallback while
    // the grid tiles the base bitmap's dimensions. Bounds-check rather
    // than assume, and pad whatever the source does not cover with the
    // same black LayerBitmapNode uses for out-of-range tiles.
    cv::Mat tile(r.height, r.width, full.type(), cv::Scalar(0, 0, 0));
    if (r.x >= full.cols || r.y >= full.rows) return tile;

    const int copyW = std::min(r.width, full.cols - r.x);
    const int copyH = std::min(r.height, full.rows - r.y);
    if (copyW <= 0 || copyH <= 0) return tile;

    full(cv::Rect(r.x, r.y, copyW, copyH))
        .copyTo(tile(cv::Rect(0, 0, copyW, copyH)));
    return tile;
}

void LayerRasterNode::markLayerEdited()
{
    invalidateRaster();
    markDirty();
}

} // namespace compositor