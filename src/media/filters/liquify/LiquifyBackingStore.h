#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include "LiquifyMesh.h"

namespace filters::liquify {

// Snapshot + freeze mask + mesh + dirty region.
//
// Single source of truth for an active Liquify session. Snapshot is immutable
// after setSnapshot(); freeze mask is mutable via paintFreezeMask(); mesh
// holds the deformation field. dirtyRect tracks which image region needs
// re-rendering after a tool stamp.
class LiquifyBackingStore {
public:
    LiquifyBackingStore() = default;

    // Replace the source image. Resets the mesh to cover the new image
    // dimensions with mesh_size=5 (PS default) and clears the dirty region.
    // Freeze mask is rebuilt to an all-zero (unfrozen) image.
    void setSnapshot(const QImage& snapshot);

    const QImage& snapshot() const { return m_snapshot; }
    QSize size() const { return m_snapshot.size(); }
    QRect bounds() const { return m_snapshot.rect(); }
    bool isEmpty() const { return m_snapshot.isNull(); }

    // Build / rebuild the mesh for the current snapshot.
    // meshSize in [2, 32] (PS uses 1-10; we allow more for fine work).
    void rebuildMesh(int meshSize);

    LiquifyMesh& mesh() { return m_mesh; }
    const LiquifyMesh& mesh() const { return m_mesh; }

    // Freeze mask is Format_Grayscale8. Value 0 = not frozen, 255 = frozen.
    // It is allocated lazily on first set; callers can also write into it
    // directly via freezeMask().
    QImage& freezeMask() { return m_freezeMask; }
    const QImage& freezeMask() const { return m_freezeMask; }
    bool isFrozen(int x, int y) const;

    // Dirty region management.
    // markDirty() unions r into the accumulated dirty rect (clamped to image).
    void markDirty(const QRect& r);
    QRect dirtyRect() const { return m_dirtyRect; }
    bool hasDirty() const { return !m_dirtyRect.isEmpty(); }
    void clearDirty() { m_dirtyRect = QRect(); }

    // Reset mesh displacements + dirty region; keep snapshot and freeze mask.
    void resetMesh();

private:
    QImage m_snapshot;
    QImage m_freezeMask;     // Format_Grayscale8, same dims as snapshot
    LiquifyMesh m_mesh;
    QRect m_dirtyRect;
};

}  // namespace filters::liquify
