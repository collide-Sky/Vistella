// SPDX-License-Identifier: MIT
//
// GroupNode - P8.1.1 Phase B (2026-10-20) composite a LayerGroup.
//
//   Semantics mirror the legacy LayerStack::render() group branch
//   (LayerStack.cpp ~line 1424):
//
//     1. Start a fresh groupMat = the current canvas (base port).
//     2. For each visible child in order: rasterize, apply the
//        child's mask (if enabled), then applyBlend(child.blend,
//        groupMat, childMat, child.opacity).
//     3. Composite groupMat back over the canvas using the GROUP's
//        opacity with Normal blend. The group's own blendMode is
//        intentionally ignored (Photoshop does the same).
//
//   Inputs: port 0 = base (the incoming canvas), port 1 = the
//   first child's node (subsequent children come from the vector
//   built by LayerStackAdapter, not from graph edges). Actually —
//   see below.
//
//   Design choice: rather than N graph ports for N children (which
//   would need a dynamic inputNodes() and a dynamic addEdge per
//   child), GroupNode receives a *child subgraph root*: the adapter
//   builds a chain of BlendNodes for the children starting from the
//   base port, and GroupNode is effectively just "apply my opacity
//   over my input". That collapses to a LayerBlendNode with Normal
//   blend + group opacity. The adapter emits that directly.
//
//   So GroupNode exists for the case where a group needs to be a
//   *first-class* node in a larger graph (e.g. so its subtree can
//   be cached independently and dirty-tracked as a unit). It takes
//   one input port (the already-composited group content) plus the
//   group's own opacity.

#pragma once

#include "../CompositorNode.h"
#include "../../imageworker/layers/Layer.h"

#include <opencv2/core.hpp>

namespace compositor {

class GroupNode : public CompositorNode {
public:
    // content: the node that produces the fully-composited group
    //          content (children already blended together).
    // opacity: the Group layer's opacity, applied when compositing
    //          content over the base.
    // base:    the incoming canvas. If 0, content is returned
    //          directly (root group).
    GroupNode(CompositorNode::NodeId base,
              CompositorNode::NodeId content,
              float opacity = 1.0f,
              QString name = QStringLiteral("Group"));

    QList<NodeId> inputNodes() const override {
        if (m_base) return { m_base, m_content };
        return { m_content };
    }

    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& ctx) override;

    QString name() const override { return m_name; }

    float opacity() const { return m_opacity; }

private:
    NodeId  m_base;      // 0 = root group (no incoming canvas)
    NodeId  m_content;   // composited group content
    float   m_opacity;
    QString m_name;
};

} // namespace compositor