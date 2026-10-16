// SPDX-License-Identifier: MIT
//
// CompositorGraph impl - P8.1 (2026-10-16)

#include "CompositorGraph.h"

#include <QQueue>
#include <QStringList>
#include <algorithm>
#include <stdexcept>

namespace compositor {

void CompositorGraph::addNode(CompositorNode* node)
{
    if (!node) return;
    m_nodes.insert(node->id(), node);
    // Pre-populate edge lists so empty lookups are well-defined.
    m_edgesOut.insert(node->id(), {});
    m_edgesIn .insert(node->id(), {});
}

void CompositorGraph::addEdge(CompositorNode::NodeId from, CompositorNode::NodeId to)
{
    if (from == to) return;
    if (!m_nodes.contains(from) || !m_nodes.contains(to)) return;
    auto& out = m_edgesOut[from];
    auto& in  = m_edgesIn[to];
    if (!out.contains(to)) out.append(to);
    if (!in.contains(from)) in.append(from);
}

CompositorNode* CompositorGraph::node(CompositorNode::NodeId id) const
{
    return m_nodes.value(id, nullptr);
}

QList<CompositorNode::NodeId> CompositorGraph::inputsOf(CompositorNode::NodeId id) const
{
    return m_edgesIn.value(id);
}

QList<CompositorNode::NodeId> CompositorGraph::outputsOf(CompositorNode::NodeId id) const
{
    return m_edgesOut.value(id);
}

QList<CompositorNode::NodeId> CompositorGraph::topologicalOrder() const
{
    // Kahn's algorithm: pick nodes with in-degree 0, decrement neighbors.
    QHash<CompositorNode::NodeId, int> inDeg;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        inDeg.insert(it.key(), m_edgesIn.value(it.key()).size());
    }
    QQueue<CompositorNode::NodeId> q;
    for (auto it = inDeg.cbegin(); it != inDeg.cend(); ++it) {
        if (it.value() == 0) q.enqueue(it.key());
    }
    QList<CompositorNode::NodeId> order;
    order.reserve(m_nodes.size());
    while (!q.isEmpty()) {
        const auto u = q.dequeue();
        order.append(u);
        for (const auto v : m_edgesOut.value(u)) {
            int& d = inDeg[v];
            if (--d == 0) q.enqueue(v);
        }
    }
    if (order.size() != m_nodes.size()) {
        throw std::logic_error("CompositorGraph: cycle detected");
    }
    return order;
}

bool CompositorGraph::hasCycle() const
{
    try {
        (void)topologicalOrder();
        return false;
    } catch (const std::logic_error&) {
        return true;
    }
}

QSet<CompositorNode::NodeId> CompositorGraph::transitiveDownstream(CompositorNode::NodeId root) const
{
    QSet<CompositorNode::NodeId> visited;
    QQueue<CompositorNode::NodeId> q;
    for (const auto v : m_edgesOut.value(root)) q.enqueue(v);
    while (!q.isEmpty()) {
        const auto u = q.dequeue();
        if (visited.contains(u)) continue;
        visited.insert(u);
        for (const auto v : m_edgesOut.value(u)) {
            if (!visited.contains(v)) q.enqueue(v);
        }
    }
    return visited;
}

int CompositorGraph::markDirty(CompositorNode::NodeId id)
{
    if (!m_nodes.contains(id)) return 0;
    CompositorNode* n = m_nodes.value(id);
    if (!n) return 0;
    n->markDirty();
    m_dirty.insert(id);

    // Coarse cascade: every transitive downstream also bumps version.
    // Their cache slots will go stale (version no longer matches the
    // TileKey we wrote last time). invalidateNode() in the cache is not
    // strictly needed (version mismatch = miss) but keeps memory tight.
    const QSet<CompositorNode::NodeId> downstream = transitiveDownstream(id);
    int nBumped = 1;
    for (const auto& d : downstream) {
        CompositorNode* dn = m_nodes.value(d);
        if (dn) {
            dn->markDirty();
            m_dirty.insert(d);
            ++nBumped;
        }
    }
    return nBumped;
}

void CompositorGraph::clearDirty()
{
    m_dirty.clear();
}

QString CompositorGraph::dotString() const
{
    QStringList lines;
    lines << QStringLiteral("digraph Compositor {");
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        const auto n = it.value();
        lines << QStringLiteral("  n%1 [label=\"%2 v%3\"];")
                 .arg(qulonglong(n->id()))
                 .arg(n->name())
                 .arg(qulonglong(n->version()));
    }
    for (auto it = m_edgesOut.cbegin(); it != m_edgesOut.cend(); ++it) {
        for (const auto v : it.value()) {
            lines << QStringLiteral("  n%1 -> n%2;")
                     .arg(qulonglong(it.key()))
                     .arg(qulonglong(v));
        }
    }
    lines << QStringLiteral("}");
    return lines.join('\n');
}

} // namespace compositor
