// SPDX-License-Identifier: MIT
//
// RenderGraph + RenderContext - P8.1 (2026-10-16) + P8.4 (2026-10-16)
//   RenderContext: per-render state (cache + the calling graph). A node's
//     compute() pulls upstream tile outputs via ctx.cache.tryGet(key).
//   RenderGraph:  main entry. execute() walks the graph in topological
//     order, fetches/computes each tile of the output node, and assembles
//     the full canvas. Tracks hit/miss counts for testing.
//
//   P8.4: add CompositeStrategy (Sequential / Parallel) + execute(engine)
//   overload that fans tiles across vistella::tp::EngineContext::background().
//   TileCache is mutex-protected so concurrent tryGet/put are race-free.

#pragma once

#include <QString>

#include "CompositorGraph.h"
#include "TileCache.h"
#include "TileGrid.h"

#include <opencv2/core.hpp>

// Forward-declare vistella::tp::EngineContext in the global namespace
// so the compositor namespace below doesn't re-wrap it. P8.4 uses
// it as the dispatch target for the Parallel composite strategy.
namespace vistella { namespace tp { class EngineContext; } }

namespace compositor {

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

// P8.4 (2026-10-16): composite strategy. P7.6.x v3 commit 2 already
// defined VCompositeStrategy in the VDraggableTabWidget subsystem for
// the dock-level pipeline; the equivalent here is per-render dispatch.
// Sequential = main thread loops over tiles. Parallel = fan tiles
// across EngineContext::background worker threads.
enum class CompositeStrategy {
    Sequential = 0,
    Parallel   = 1,
};

class RenderGraph {
public:
    RenderGraph(CompositorGraph* graph,
                CompositorNode::NodeId outputNode,
                TileGrid* grid)
        : m_graph(graph), m_output(outputNode), m_grid(grid) {}

    // P8.4 (2026-10-16): composite strategy selection. Default
    // Sequential preserves the P8.1 behavior. Setting Parallel
    // routes the per-tile work to engine->background() when an
    // engine is passed to execute().
    void setCompositeStrategy(CompositeStrategy s) { m_strategy = s; }
    CompositeStrategy compositeStrategy() const { return m_strategy; }

    // Render the full canvas. Returns a cv::Mat of size grid->imageWidth()
    // x grid->imageHeight() (CV_8UC3 BGR by default). The
    // engine-bearing overload honors the CompositeStrategy:
    //   Sequential  -> walk tiles on the caller thread (no engine)
    //   Parallel    -> submit one task per tile to engine->background();
    //                  if engine is null, falls back to Sequential.
    cv::Mat execute();
    cv::Mat execute(::vistella::tp::EngineContext* engine);

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
    CompositeStrategy      m_strategy = CompositeStrategy::Sequential;

    int m_lastHits   = 0;
    int m_lastMisses = 0;
    int m_lastTiles  = 0;

    // Sequential path: walks tiles on the caller thread.
    cv::Mat executeSequential();
    // Parallel path: submits one task per tile to engine->background().
    // engine may be null; falls back to executeSequential() in that case.
    cv::Mat executeParallel(::vistella::tp::EngineContext* engine);

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
