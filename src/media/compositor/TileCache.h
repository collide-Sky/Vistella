// SPDX-License-Identifier: MIT
//
// TileCache - QHash<TileKey, cv::Mat> cache
//   Synchronous, single-threaded. P8.4 will wrap with a mutex if parallel
//   executor is added. invalidateNode() drops all entries for a given
//   nodeId (used during dirty propagation + manual flush).
//
//   Lookup is exact-match on (nodeId, tx, ty, version). A miss returns
//   false and the caller is expected to recompute + put() the result.

#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <opencv2/core.hpp>

#include "TileGrid.h"        // TileKey
#include "CompositorNode.h"  // CompositorNode::NodeId

namespace compositor {

class TileCache {
public:
    void put(const TileKey& k, const cv::Mat& tile);
    bool tryGet(const TileKey& k, cv::Mat& out) const;
    bool contains(const TileKey& k) const;

    // Drop all entries for a node. Used by CompositorGraph::markDirty
    // cascade so the next render recomputes the dirty node's tiles.
    // Note: this drops slots regardless of version. The graph's dirty
    // set is what signals "version bumped" downstream.
    int  invalidateNode(CompositorNode::NodeId id);

    // Drop everything.
    void clear();

    int  size() const;
    bool empty() const { return size() == 0; }

    // For tests / diagnostics: distinct nodeIds currently in cache.
    QSet<CompositorNode::NodeId> cachedNodes() const;

private:
    QHash<TileKey, cv::Mat> m_tiles;
    // thread-safe cache. tryGet / put / invalidate
    // are guarded by a QMutex so the parallel executor (P8.4) can fan
    // tiles across worker threads without races. Const methods take
    // a QMutexLocker via the `mutable` lock; non-const take it the
    // same way.
    mutable QMutex          m_lock;
};

} // namespace compositor
