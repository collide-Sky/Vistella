// SPDX-License-Identifier: MIT
//
// RenderGraph impl - P8.1 (2026-10-16)

#include "RenderGraph.h"

#include <opencv2/core.hpp>
#include <QHash>
#include <stdexcept>

namespace compositor {

cv::Mat RenderGraph::execute()
{
    // Idempotent: cache persists across calls. markDirty() (in
    // CompositorGraph) bumps versions which causes next execute to miss
    // exactly the stale tiles. Tests rely on this for "second pass all
    // hits" verification.
    m_lastHits = 0;
    m_lastMisses = 0;
    m_lastTiles  = 0;

    if (!m_graph || !m_grid) return cv::Mat();
    CompositorNode* out = m_graph->node(m_output);
    if (!out) return cv::Mat();

    RenderContext ctx(m_graph, &m_cache, this);

    // Build the full canvas by stitching output node's tiles.
    cv::Mat canvas(m_grid->imageHeight(), m_grid->imageWidth(), CV_8UC3,
                   cv::Scalar(0, 0, 0));

    const auto tiles = m_grid->allTiles();
    for (const auto& t : tiles) {
        const int tx = t.first;
        const int ty = t.second;
        cv::Mat tile = requestNodeTile(m_output, tx, ty, ctx);
        if (tile.empty()) continue;
        const cv::Rect r = m_grid->tileRect(tx, ty);
        // Place tile into canvas (handles partial edge tiles naturally).
        cv::Mat dst = canvas(r);
        if (tile.size() == dst.size() && tile.type() == dst.type()) {
            tile.copyTo(dst);
        }
        // Mismatched size / type = silently skip (caller should ensure
        // compute() returns the right size).
    }
    m_lastHits  = ctx.hits;
    m_lastMisses = ctx.misses;
    m_lastTiles = tiles.size();
    m_graph->clearDirty();
    return canvas;
}

cv::Mat RenderGraph::executeAndReuse(RenderContext& ctx)
{
    if (!m_graph || !m_grid) return cv::Mat();
    CompositorNode* out = m_graph->node(m_output);
    if (!out) return cv::Mat();

    cv::Mat canvas(m_grid->imageHeight(), m_grid->imageWidth(), CV_8UC3,
                   cv::Scalar(0, 0, 0));
    const auto tiles = m_grid->allTiles();
    for (const auto& t : tiles) {
        const int tx = t.first;
        const int ty = t.second;
        cv::Mat tile = requestNodeTile(m_output, tx, ty, ctx);
        if (tile.empty()) continue;
        const cv::Rect r = m_grid->tileRect(tx, ty);
        cv::Mat dst = canvas(r);
        if (tile.size() == dst.size() && tile.type() == dst.type()) {
            tile.copyTo(dst);
        }
    }
    return canvas;
}

// (see header for the second-pass execution path)

cv::Mat RenderGraph::requestNodeTile(CompositorNode::NodeId n,
                                     int tileX, int tileY,
                                     RenderContext& ctx)
{
    CompositorNode* node = ctx.graph->node(n);
    if (!node) return cv::Mat();

    const TileKey k{ n, tileX, tileY, node->version() };

    cv::Mat cached;
    if (ctx.cache->tryGet(k, cached)) {
        ctx.hits++;
        return cached;
    }
    ctx.misses++;

    // Materialize upstream tiles first (recursive). A node with multiple
    // inputs is responsible for pulling them in its compute(); we don't
    // pre-resolve here because the input order is semantic to the node.
    //
    // Just call node->compute(); it has full access to ctx.cache and can
    // call ctx.requestNodeTile() on its upstream nodes if needed.
    cv::Mat tile = node->compute(tileX, tileY, *m_grid, ctx);
    if (!tile.empty()) {
        ctx.cache->put(k, tile);
    }
    return tile;
}

// RenderContext forwards to the owning RenderGraph so the cache +
// tile-grid + hits/misses accounting are all consistent. Implemented
// out-of-line so RenderContext stays a header-only forward-decl for
// CompositorNode.h.
cv::Mat RenderContext::requestNodeTile(CompositorNode::NodeId n, int tileX, int tileY)
{
    if (!owner) return cv::Mat();
    return owner->requestNodeTile(n, tileX, tileY, *this);
}

} // namespace compositor
