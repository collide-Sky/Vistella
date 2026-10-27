// SPDX-License-Identifier: MIT
//
// CompositorNode - base class for render graph nodes
//   A node declares its upstream inputs (inputNodes) + a per-tile compute
//   method. Subclasses (LayerBitmapNode, LayerBlendNode, etc.) implement
//   compute() to produce one tile of output.
//
//   version() returns a per-node "fingerprint" that the TileCache uses as
//   part of TileKey. markDirty() bumps it; CompositorGraph::markDirty()
//   cascades to all downstream nodes so the next render recomputes their
//   tiles too (coarse propagation; per-tile bounding box tracking is a
//   P8.2+ refinement).

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <opencv2/core.hpp>   // cv::Mat in compute() return type

namespace compositor {

class RenderContext;   // forward decl; full definition in RenderGraph.h

class CompositorNode {
public:
    using NodeId = quint64;
    using PortId = int;

    virtual ~CompositorNode() = default;

    // Stable id within a CompositorGraph. Default: returns reinterpret_cast
    // of `this`. Override only if the node can be moved between graphs.
    virtual NodeId id() const {
        return reinterpret_cast<NodeId>(this);
    }

    // Upstream node ids (input ports). Empty for source nodes (e.g.
    // LayerBitmapNode, which reads from a cv::Mat directly). Order
    // matters: port index 0 = first input.
    virtual QList<NodeId> inputNodes() const { return {}; }

    // Per-tile compute. The TileKey carries (this->id(), tileX, tileY, this->version()).
    // The TileGrid gives image-space geometry. RenderContext exposes the
    // upstream tile cache + per-graph state.
    //
    // Contract: returned cv::Mat must match tileRect(tx, ty) size exactly.
    // CV_8UC3 BGR by default (matches Layer::image); subclasses may return
    // CV_8UC4 BGRA (with alpha) for blend nodes.
    virtual cv::Mat compute(int tileX, int tileY,
                            const class TileGrid& grid,
                            class RenderContext& ctx) = 0;

    // Fingerprint. Bump (via markDirty()) whenever the node's parameters
    // or inputs change in a way that invalidates the cached output.
    // Default impl: a private counter starting at 1.
    quint64 version() const { return m_version; }
    void    markDirty()     { m_version = (m_version + 1) | 1ULL; }   // never 0 (sentinel)

    // Display name for debugging / test diagnostics.
    virtual QString name() const = 0;

protected:
    CompositorNode() : m_version(1) {}   // 1 = first valid; 0 is sentinel

private:
    quint64 m_version;
};

} // namespace compositor
