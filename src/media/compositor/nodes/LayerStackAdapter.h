// SPDX-License-Identifier: MIT
//
// LayerStackAdapter - P8.1.1 (2026-10-16) LayerStack -> CompositorGraph
//   adapter. P8.1.1 Phase B (2026-10-20) adds Group support.
//
//   Builds a CompositorGraph that renders a layers::LayerStack
//   bottom-up via a chain of LayerBitmapNode + LayerBlendNode
//   (and LayerMaskNode for layers that have a mask).
//
//   P8.1.1 Phase A scope (committed):
//     - Bitmap layers only.
//     - Blend modes delegated to layers::applyBlend (all 15).
//     - Per-layer opacity honored.
//     - visible=false layers skipped.
//     - Masked layers get a LayerMaskNode inserted.
//
//   P8.1.1 Phase B additions:
//     - LayerKind::Group: children are composited recursively into
//       a GroupNode, which applies the group's opacity with Normal
//       blend. The group's own blendMode is ignored (matches PS and
//       the legacy LayerStack::render() group branch).
//     - LayerKind::Adjustment: LUT applied via a LayerAdjustmentNode
//       (deferred to P8.1.1.x — see the adapter's default branch).
//
//   Returns a Result that owns every node it created so the caller
//   keeps them alive for the lifetime of the graph.

#pragma once

#include "../CompositorGraph.h"
#include "../CompositorNode.h"

#include "LayerBitmapNode.h"
#include "LayerBlendNode.h"
#include "LayerMaskNode.h"
#include "GroupNode.h"

#include <QList>
#include <QString>

#include <opencv2/core.hpp>

#include <memory>
#include <vector>

namespace layers { class LayerStack; }

namespace compositor {

class LayerStackAdapter {
public:
    struct Result {
        CompositorGraph         graph;
        CompositorNode::NodeId  outputNode = 0;

        // Canvas geometry the graph was built for. The adapter owns
        // base-layer discovery (it has to pick the compositing root
        // anyway), so it publishes the resulting geometry here instead
        // of letting callers re-scan the stack with a second, drifting
        // copy of the same rule.
        //
        // Rules, matching the legacy LayerStack::render():
        //   - a base bitmap layer exists -> its image size wins
        //     (canvas is that layer's image.clone());
        //   - otherwise -> LayerStack::canvasSize() (white fallback).
        cv::Size canvasSize;

        // Every node the adapter created. The graph stores raw
        // pointers, so the Result must outlive the graph.
        std::vector<std::unique_ptr<LayerBitmapNode>> bitmaps;
        std::vector<std::unique_ptr<LayerBlendNode>>  blends;
        std::vector<std::unique_ptr<LayerMaskNode>>   masks;
        std::vector<std::unique_ptr<GroupNode>>       groups;

        // Mask source nodes (adapter-private CompositorNode subclass
        // that yields a Layer's layerMask as a single-channel tile).
        // Type-erased because the class is file-local.
        struct MaskSourceHolder {
            std::unique_ptr<CompositorNode> node;
        };
        std::vector<MaskSourceHolder> maskSources;
    };

    // Build a DAG that renders `stack` bottom-up. The stack is not
    // modified; the caller continues to own it.
    static Result build(layers::LayerStack& stack);
};

} // namespace compositor