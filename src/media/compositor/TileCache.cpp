// SPDX-License-Identifier: MIT
//
// TileCache impl

#include "TileCache.h"

namespace compositor {

void TileCache::put(const TileKey& k, const cv::Mat& tile)
{
    if (!k.isValid()) return;   // 0-version keys are sentinel; ignore
    QMutexLocker lock(&m_lock);
    m_tiles.insert(k, tile.clone());   // clone so caller can free its scratch
}

bool TileCache::tryGet(const TileKey& k, cv::Mat& out) const
{
    if (!k.isValid()) return false;
    QMutexLocker lock(&m_lock);
    auto it = m_tiles.constFind(k);
    if (it == m_tiles.constEnd()) return false;
    out = it.value();   // shallow copy; cv::Mat is refcounted
    return true;
}

bool TileCache::contains(const TileKey& k) const
{
    if (!k.isValid()) return false;
    QMutexLocker lock(&m_lock);
    return m_tiles.contains(k);
}

int TileCache::invalidateNode(CompositorNode::NodeId id)
{
    QMutexLocker lock(&m_lock);
    int n = 0;
    // QHash::removeIf (Qt 6.1+) lets us drop by predicate.
    // For broader compat, iterate keys + remove.
    QList<TileKey> toRemove;
    for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
        if (it.key().nodeId == id) toRemove.append(it.key());
    }
    for (const auto& k : toRemove) {
        m_tiles.remove(k);
        ++n;
    }
    return n;
}

void TileCache::clear()
{
    QMutexLocker lock(&m_lock);
    m_tiles.clear();
}

int TileCache::size() const
{
    QMutexLocker lock(&m_lock);
    return m_tiles.size();
}

QSet<CompositorNode::NodeId> TileCache::cachedNodes() const
{
    QMutexLocker lock(&m_lock);
    QSet<CompositorNode::NodeId> s;
    for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
        s.insert(it.key().nodeId);
    }
    return s;
}

} // namespace compositor
