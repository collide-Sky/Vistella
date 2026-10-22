// SPDX-License-Identifier: MIT
//
// SolidCanvasNode - P8.1.1.y (2026-10-22)
//
//   Source node (no inputs) producing a constant-colour tile.
//
//   Exists for the "no bitmap base layer" case. The legacy
//   LayerStack::render() seeds the canvas with the first Bitmap-with-
//   image layer, and when there is none it falls back to
//       canvas = Mat(canvasSize(), CV_8UC3, Scalar(255, 255, 255))
//   — a white canvas that every visible layer then blends onto. Without a
//   node like this the DAG has nothing to hang its first layer off, and
//   the output would start from black instead of white.
//
//   The graph therefore always has an explicit root: either the base
//   layer's own bitmap node, or this one. That removes the
//   "first visible layer becomes the canvas" convention the adapter used
//   to rely on, which silently skipped that layer's opacity.

#pragma once

#include "../CompositorNode.h"
#include "../RenderGraph.h"

#include <QString>

#include <opencv2/core.hpp>

namespace compositor {

class SolidCanvasNode : public CompositorNode {
public:
    SolidCanvasNode(cv::Size size, cv::Scalar color, QString name = {});

    QList<NodeId> inputNodes() const override { return {}; }

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override;

    cv::Size canvasSize() const { return m_size; }
    cv::Scalar color() const { return m_color; }

private:
    cv::Size   m_size;
    cv::Scalar m_color;
    QString    m_name;
};

} // namespace compositor