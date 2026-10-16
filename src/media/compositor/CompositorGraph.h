// SPDX-License-Identifier: MIT
//
// CompositorGraph - P8.1 (2026-10-16) DAG of CompositorNode
//   addNode / addEdge build the graph. topologicalOrder() returns the
//   evaluation order (Kahn's algorithm) and throws on cycle. markDirty()
//   bumps a node's version + cascades to all transitive downstream nodes
//   so their cached tiles go stale on the next render.

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "CompositorNode.h"

namespace compositor {

class CompositorGraph {
public:
    CompositorGraph() = default;
    ~CompositorGraph() = default;

    // Non-owning: nodes are owned by whoever created them (the
    // ImageWindow / LayerStack owner). The graph keeps raw pointers.
    void addNode(CompositorNode* node);
    // Edge semantics: from -> to (from is an input of to).
    void addEdge(CompositorNode::NodeId from, CompositorNode::NodeId to);

    // Kahn's algorithm. Returns nodes in evaluation order
    // (sources first). Throws std::logic_error on cycle.
    QList<CompositorNode::NodeId> topologicalOrder() const;

    bool hasCycle() const;
    int  nodeCount() const { return m_nodes.size(); }
    int  edgeCount() const { return m_edgesOut.size(); }

    // Lookup
    CompositorNode* node(CompositorNode::NodeId id) const;
    QList<CompositorNode::NodeId> inputsOf(CompositorNode::NodeId id) const;
    QList<CompositorNode::NodeId> outputsOf(CompositorNode::NodeId id) const;

    // Dirty propagation (coarse): bump the named node's version, then
    // bump every transitive downstream node too. Next render recomputes
    // them. P8.1 strategy: full cascade; P8.2 may refine with bbox.
    int markDirty(CompositorNode::NodeId id);
    bool isDirty(CompositorNode::NodeId id) const { return m_dirty.contains(id); }
    int  dirtyCount() const { return m_dirty.size(); }
    void clearDirty();

    // Diagnostic
    QString dotString() const;   // Graphviz "digraph" (for debug dump)

private:
    QHash<CompositorNode::NodeId, CompositorNode*> m_nodes;
    QHash<CompositorNode::NodeId, QList<CompositorNode::NodeId>> m_edgesOut;  // from -> to
    QHash<CompositorNode::NodeId, QList<CompositorNode::NodeId>> m_edgesIn;   // to -> from

    // Set of nodeIds that are dirty in the current cycle. Cleared by
    // clearDirty() after a successful render so the next markDirty can
    // distinguish "dirty now" from "dirty before last render".
    QSet<CompositorNode::NodeId> m_dirty;

    // BFS over downstream edges from `root`, collecting all transitive
    // downstream nodeIds (excluding `root` itself).
    QSet<CompositorNode::NodeId> transitiveDownstream(CompositorNode::NodeId root) const;
};

} // namespace compositor
