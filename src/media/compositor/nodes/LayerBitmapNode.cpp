// SPDX-License-Identifier: MIT
//
// LayerBitmapNode impl

#include "LayerBitmapNode.h"

#include "../../imageworker/layers/Layer.h"   // layers::Layer (full def for .image)

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

    // 通道归一化: 合成画布固定是 CV_8UC3, 节点必须输出同样的格式.
    //   图层图可能是 CV_8UC1 (灰度) / CV_8UC3 / CV_8UC4 (带 alpha) / 16bit,
    //   不在这里转的话 RenderGraph 会因为类型不匹配把整块 tile 丢掉 ->
    //   画布全黑.
    cv::Mat src;
    switch (m_layer->image.type()) {
    case CV_8UC1:
        cv::cvtColor(m_layer->image, src, cv::COLOR_GRAY2BGR);
        break;
    case CV_8UC3:
        src = m_layer->image;
        break;
    case CV_8UC4:
        // 画布是 3 通道, alpha 在这里丢掉 (跟 legacy 路径一致)
        cv::cvtColor(m_layer->image, src, cv::COLOR_BGRA2BGR);
        break;
    default:
        // 其它位深 (16bit 等): 先降到 8bit 再转 BGR
        cv::Mat tmp;
        m_layer->image.convertTo(tmp, CV_8U);
        if (tmp.channels() == 1) {
            cv::cvtColor(tmp, src, cv::COLOR_GRAY2BGR);
        } else if (tmp.channels() == 4) {
            cv::cvtColor(tmp, src, cv::COLOR_BGRA2BGR);
        } else {
            src = tmp;   // 3 通道: 直接用
        }
        break;
    }
    if (src.empty()) return cv::Mat();

    // Bounds check: if the layer's image is smaller than the grid image
    // (e.g. layer hasn't been resized to base yet), treat missing regions
    // as transparent/black.
    if (r.x >= src.cols || r.y >= src.rows) {
        return cv::Mat::zeros(r.height, r.width, CV_8UC3);
    }

    // Build the tile by copying from the layer's image.
    cv::Mat tile(r.height, r.width, CV_8UC3);
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
