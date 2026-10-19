// SPDX-License-Identifier: MIT
//
// LayerStackAdapter - P8.1.1 (2026-10-19) LayerStack -> CompositorGraph
// adapter.
//
//   Builds a CompositorGraph that renders a layers::LayerStack
//   bottom-up via a chain of LayerBitmapNode + LayerBlendNode
//   (and LayerMaskNode for layers that have a mask).
//
//   MVP scope:
//     - Bitmap layers only (the most common; Vector/Text/Smart/
//       Adjustment/SmartFilter fall through to a base + passthrough
//       in P8.1.1.x).
//     - Group layers are flattened (children inlined at the group's
//       position; the group itself becomes a passthrough).
//     - blend modes Normal/Multiply/Screen/Darken/Lighten/Difference.
//       Other modes fall through to Normal at the LayerBlendNode.
//     - Per-layer opacity is honored (passed to LayerBlendNode).
//     - visible=false layers are skipped (passthrough).
//     - masked layers get a LayerMaskNode inserted (mask source is
//       the layer's own layerMask cv::Mat; LayerBitmapNode can be
//       reused for it via a thin MaskBitmapNode that returns a
//       single-channel image).
//
//   Returns a LayerStackAdapter::Result that contains the output
//   node id and a list of owned nodes (so the caller keeps them
//   alive for the lifetime of the graph).

#pragma once

#include "../CompositorGraph.h"
#include "../CompositorNode.h"

#include "LayerBitmapNode.h"
#include "LayerBlendNode.h"
#include "LayerMaskNode.h"

#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace layers { class LayerStack; }

namespace compositor {

class LayerStackAdapter {
public:
    struct Result {
        CompositorGraph         graph;
        CompositorNode::NodeId  outputNode = 0;

        // The adapter owns the nodes (they live in stable storage so
        // the graph's NodeId (== this) remains valid). Callers
        // access them via the vector; do not delete.
        std::vector<std::unique_ptr<LayerBitmapNode>> bitmaps;
        std::vector<std::unique_ptr<LayerBlendNode>>  blends;
        std::vector<std::unique_ptr<LayerMaskNode>>   masks;
    };

    // Build a DAG that renders `stack` bottom-up. The stack is not
    // modified; the caller continues to own it.
    static Result build(layers::LayerStack& stack);
};

} // namespace compositor