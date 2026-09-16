#include "LiquifyBackingStore.h"

#include <algorithm>
#include <cmath>

namespace filters::liquify {

void LiquifyBackingStore::setSnapshot(const QImage& snapshot) {
    m_snapshot = snapshot;
    m_freezeMask = QImage(snapshot.size(), QImage::Format_Grayscale8);
    m_freezeMask.fill(0);
    rebuildMesh(5);
    clearDirty();
}

void LiquifyBackingStore::rebuildMesh(int meshSize) {
    if (m_snapshot.isNull()) return;
    if (meshSize < 2) meshSize = 2;
    if (meshSize > 32) meshSize = 32;
    m_mesh.build(meshSize, meshSize, QRectF(m_snapshot.rect()));
}

bool LiquifyBackingStore::isFrozen(int x, int y) const {
    if (m_freezeMask.isNull()) return false;
    if (x < 0 || x >= m_freezeMask.width()) return false;
    if (y < 0 || y >= m_freezeMask.height()) return false;
    // For Format_Grayscale8, pixelColor() reports the gray value through
    // the red channel; using qGray() on the raw pixel() int would mis-decode.
    return m_freezeMask.pixelColor(x, y).red() > 127;
}

void LiquifyBackingStore::markDirty(const QRect& r) {
    if (r.isEmpty()) return;
    const QRect clamped = m_snapshot.isNull()
                              ? r
                              : r.intersected(QRect(QPoint(0, 0), m_snapshot.size()));
    if (clamped.isEmpty()) return;
    if (m_dirtyRect.isEmpty()) {
        m_dirtyRect = clamped;
    } else {
        m_dirtyRect = m_dirtyRect.united(clamped);
    }
}

void LiquifyBackingStore::resetMesh() {
    m_mesh.reset();
    clearDirty();
}

}  // namespace filters::liquify
