// SPDX-License-Identifier: MIT
//
// RenderGraph impl - P8.1 (2026-10-16) + P8.4 (2026-10-16)

#include "RenderGraph.h"

#include "engine_context.h"   // P8.4: vistella::tp::EngineContext
#include "executor.hpp"        // P8.4: vistella::tp::IExecutor + Priority

#include <atomic>
#include <opencv2/core.hpp>
#include <QHash>
#include <QThread>
#include <QVector>
#include <stdexcept>

namespace compositor {

cv::Mat RenderGraph::execute()
{
    // No engine -> always sequential (preserves P8.1 caller-thread
    // behavior).
    return execute(nullptr);
}

cv::Mat RenderGraph::execute(::vistella::tp::EngineContext* engine)
{
    if (m_strategy == CompositeStrategy::Parallel && engine) {
        return executeParallel(engine);
    }
    return executeSequential();
}

cv::Mat RenderGraph::executeSequential()
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

cv::Mat RenderGraph::executeParallel(::vistella::tp::EngineContext* engine)
{
    // P8.4 MVP: parallel fan-out. P8.4.x may swap in a std::future-based
    // barrier; for now we busy-wait on a std::atomic<int> counter.
    //
    // NOTE: TileCache is mutex-protected (P8.4) so concurrent tryGet/put
    // are race-free. CompositorNode::compute() is expected to be
    // thread-safe (no shared mutable state in the per-call path).
    //
    // The local RenderContext is captured by reference into each task
    // lambda; the busy-wait below guarantees the function's stack frame
    // (and therefore localCtx) outlives every submitted worker.
    if (!engine) return executeSequential();
    if (!m_graph || !m_grid) return cv::Mat();
    CompositorNode* out = m_graph->node(m_output);
    if (!out) return cv::Mat();

    m_lastHits = 0;
    m_lastMisses = 0;
    m_lastTiles  = 0;

    cv::Mat canvas(m_grid->imageHeight(), m_grid->imageWidth(), CV_8UC3,
                   cv::Scalar(0, 0, 0));

    const auto tiles = m_grid->allTiles();
    const int nTiles = static_cast<int>(tiles.size());
    if (nTiles == 0) {
        m_graph->clearDirty();
        return canvas;
    }

    // Pre-compute (tx, ty) per task so workers do not touch QPair.
    QVector<int> txs;
    txs.reserve(nTiles);
    QVector<int> tys;
    tys.reserve(nTiles);
    for (const auto& t : tiles) {
        txs.append(t.first);
        tys.append(t.second);
    }

    // Brace-init to avoid MSVC most-vexing-parse on
    //   QVector<cv::Mat> slots(nTiles);
    // because cv::Mat has an implicit ctor from int, so MSVC may bind
    // the (nTiles) as a function parameter list, declaring a parameter
    // named nTiles that collides with the local int. Brace-init cannot
    // be misparsed as a declaration.
    //
    // Note: the name 'slots' cannot be used here because Qt's
    // qtmetamacros.h #defines `slots` to Q_SLOTS = __declspec(qt_slot).
    // After preprocessing, `slots` expands to that, breaking both
    // declarations and member-access syntax. Use `tileSlots` instead.
    QVector<cv::Mat> tileSlots;
    tileSlots.resize(nTiles);
    QVector<char> ready(nTiles, 0);

    RenderContext localCtx(m_graph, &m_cache, this);
    std::atomic<int> doneCount{0};

    for (int i = 0; i < nTiles; ++i) {
        const int tx = txs[i];
        const int ty = tys[i];
        engine->background().submit_fn(
            [this, &localCtx, tx, ty, i, &tileSlots, &ready, &doneCount]
            (vistella::tp::TaskContext& /*tc*/) mutable {
                cv::Mat tile = requestNodeTile(m_output, tx, ty, localCtx);
                if (!tile.empty()) {
                    tileSlots[i] = std::move(tile);
                    ready[i]  = 1;
                }
                doneCount.fetch_add(1, std::memory_order_relaxed);
            },
            "P8.4 render tile",
            ::vistella::tp::Priority::Normal);
    }

    while (doneCount.load(std::memory_order_relaxed) < nTiles) {
        QThread::msleep(1);
    }

    for (int i = 0; i < nTiles; ++i) {
        if (!ready[i]) continue;
        const int tx = txs[i];
        const int ty = tys[i];
        const cv::Rect r = m_grid->tileRect(tx, ty);
        cv::Mat dst = canvas(r);
        if (tileSlots[i].size() == dst.size() && tileSlots[i].type() == dst.type()) {
            tileSlots[i].copyTo(dst);
        }
    }
    m_lastHits   = 0;
    m_lastMisses = 0;
    m_lastTiles  = nTiles;
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
