// SPDX-License-Identifier: MIT
//
// LayerBitmapNode impl

#include "LayerBitmapNode.h"

#include "../../imageworker/layers/Layer.h"   // layers::Layer (full def for .image)
#include "../../imageworker/layers/PixelFormat.h"   // layers::toBgra

#include <opencv2/imgproc.hpp>

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

    // Channel normalisation: the compositing canvas is always CV_8UC4
    // BGRA with straight alpha (pixel contract).
    //   The layer image may be CV_8UC1 (grayscale) / CV_8UC3 / CV_8UC4
    //   (with alpha) / 16bit. Without normalisation RenderGraph drops the
    //   whole tile on a type mismatch -> a black canvas. The alpha must
    //   NOT be dropped here: without it there is no transparency at all
    //   (LYR-01).
    const cv::Mat src = layers::toBgra(m_layer->image);
    if (src.empty()) return cv::Mat();

    // Bounds check: if the layer's image is smaller than the grid image
    // (e.g. layer hasn't been resized to base yet), treat missing regions
    // as transparent/black.
    if (r.x >= src.cols || r.y >= src.rows) {
        return cv::Mat::zeros(r.height, r.width, CV_8UC4);
    }

    // Build the tile by copying from the layer's image.
    cv::Mat tile(r.height, r.width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    const int copyW = std::min(r.width,  src.cols - r.x);
    const int copyH = std::min(r.height, src.rows - r.y);
    if (copyW <= 0 || copyH <= 0) {
        tile.setTo(cv::Scalar(0, 0, 0));
        return tile;
    }
    cv::Mat srcROI = src(cv::Rect(r.x, r.y, copyW, copyH));
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
