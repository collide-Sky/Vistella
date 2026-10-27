// SPDX-License-Identifier: MIT
//
// SolidCanvasNode impl

#include "SolidCanvasNode.h"

#include "../TileGrid.h"

namespace compositor {

SolidCanvasNode::SolidCanvasNode(cv::Size size, cv::Scalar color, QString name)
    : m_size(size)
    , m_color(color)
    , m_name(std::move(name))
{
}

QString SolidCanvasNode::name() const
{
    return m_name;
}

cv::Mat SolidCanvasNode::compute(int tileX, int tileY,
                                 const TileGrid& grid,
                                 RenderContext& /*ctx*/)
{
    const cv::Rect r = grid.tileRect(tileX, tileY);
    if (r.width <= 0 || r.height <= 0) return cv::Mat();

    // The grid is built from this node's size, so tiles normally sit
    // inside it. Bounds-check anyway: a mismatch here would otherwise
    // paint canvas pixels that legacy leaves black.
    const int w = std::min(r.width, m_size.width - r.x);
    const int h = std::min(r.height, m_size.height - r.y);
    if (w <= 0 || h <= 0) return cv::Mat(r.height, r.width, CV_8UC3,
                                          cv::Scalar(0, 0, 0));

    cv::Mat tile(r.height, r.width, CV_8UC3, m_color);
    if (w < r.width || h < r.height) {
        tile(cv::Rect(0, 0, w, h)).setTo(m_color);
    }
    return tile;
}

} // namespace compositor