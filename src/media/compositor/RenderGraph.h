// SPDX-License-Identifier: MIT
//
// RenderGraph + RenderContext - P8.1 (2026-10-16)
//   RenderContext: per-render state (cache + the calling graph). A node's
//     compute() pulls upstream tile outputs via ctx.cache.tryGet(key).
//   RenderGraph:  main entry. execute() walks the graph in topological
//     order, fetches/computes each tile of the output node, and assembles
//     the full canvas. Tracks hit/miss counts for testing.

#pragma once

#include <QString>

#include "CompositorGraph.h"
#include "TileCache.h"
#include "TileGrid.h"

#include <opencv2/core.hpp>

namespace compositor {

// Per-render state. compute() implementations query upstream tiles via
// ctx.requestNodeTile(id, tx, ty) which uses the cache + topologically
// materializes the upstream tile if needed. RenderContext also exposes
// the raw cache for direct tryGet/put when a node wants to manage its
// own multi-input composition.
class RenderContext {
public:
    // Backref lets ctx.requestNodeTile forward to the owning RenderGraph
    // (which has the TileGrid). Set in RenderGraph::execute().
    RenderContext(CompositorGraph* graph, TileCache* cache, class RenderGraph* owner = nullptr)
        : graph(graph), cache(cache), owner(owner) {}

    CompositorGraph* graph = nullptr;
    TileCache*       cache = nullptr;
    RenderGraph*     owner = nullptr;   // backref for requestNodeTile

    // Materialize an upstream tile through the standard path: cache
    // lookup, miss -> recursive compute -> cache write. Updates hits /
    // misses so RenderGraph can report the stats. Use this from your
    // node's compute() when you need an upstream tile so the cache
    // accounting is consistent.
    cv::Mat requestNodeTile(CompositorNode::NodeId n, int tileX, int tileY);

    // Stats (incremented by RenderGraph for test verification).
    int hits   = 0;
    int misses = 0;
};

class RenderGraph {
public:
    RenderGraph(CompositorGraph* graph,
                CompositorNode::NodeId outputNode,
                TileGrid* grid)
        : m_graph(graph), m_output(outputNode), m_grid(grid) {}

    // Render the full canvas. Returns a cv::Mat of size grid->imageWidth()
    // x grid->imageHeight() (CV_8UC3 BGR by default).
    cv::Mat execute();

    // After execute(), these reflect the most recent render. Used by tests
    // to verify cache hit rate.
    int lastHitCount()   const { return m_lastHits; }
    int lastMissCount()  const { return m_lastMisses; }
    int lastTilesDone()  const { return m_lastTiles; }

    // For tests: re-render with the same graph + cache. Lets us verify
    // hit rate improvement on second pass (idempotent re-render).
    cv::Mat executeAndReuse(RenderContext& ctx);

private:
    CompositorGraph*       m_graph  = nullptr;
    CompositorNode::NodeId m_output = 0;
    TileGrid*              m_grid   = nullptr;
    TileCache              m_cache;

    int m_lastHits   = 0;
    int m_lastMisses = 0;
    int m_lastTiles  = 0;

public:
    // Recursive: compute node n's tile (tx, ty). Uses cache; if miss,
    // calls n->compute() after materializing upstream tiles. Updates ctx.
    // Public so RenderContext::requestNodeTile can call it (with a
    // backref to the owning RenderGraph).
    cv::Mat requestNodeTile(CompositorNode::NodeId n,
                            int tileX, int tileY,
                            RenderContext& ctx);
};

} // namespace compositor
