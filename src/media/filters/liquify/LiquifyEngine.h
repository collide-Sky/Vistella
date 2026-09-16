#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>

#include "LiquifyBackingStore.h"
#include "LiquifyMesh.h"

namespace filters::liquify {

// Renderer for a LiquifyBackingStore.
//
// renderFull() walks every pixel of the snapshot through the mesh's inverse
// warp. Use it once at session start, or when the mesh is rebuilt.
//
// renderDirty() only re-walks pixels inside store.dirtyRect() (plus a small
// padding for the inverse-warp footprint). Use it after each tool stamp
// during interactive editing.
//
// Both honour the freeze mask: pixels with mask value > 127 are sampled
// directly from the snapshot, skipping warp entirely.
//
// samplePixel() is a single-pixel query useful for picking / preview UI.
class LiquifyEngine {
public:
    LiquifyEngine() = default;

    void setStore(LiquifyBackingStore* store) { m_store = store; }
    LiquifyBackingStore* store() { return m_store; }
    const LiquifyBackingStore* store() const { return m_store; }

    // Full-image render. Returns an image the same size and format as the
    // store's snapshot.
    QImage renderFull() const;

    // Incremental render. Returns the warped region covering
    // store.dirtyRect() grown by the current maximum displacement magnitude
    // (so inverse-warp sampling does not read pixels from outside the
    // rendered region). outDirtyRect holds the rect within the snapshot
    // coordinates where this image should be composited.
    //
    // Returns a null image if there is no dirty region.
    QImage renderDirty(QRect& outDirtyRect) const;

    // Sample a single output pixel at (x, y) in image coordinates.
    QColor samplePixel(qreal x, qreal y) const;

    // Apply one tool stamp: forwards to LiquifyMesh::applyTool(), then
    // grows the store's dirty rect to cover the brush footprint plus the
    // max-displacement padding.
    void applyToolStamp(const QPointF& center, qreal radius, qreal pressure,
                        const QPointF& direction, LiquifyToolMode mode);

    // Bilinear sample of an image at (x, y). x/y can be fractional.
    // Coordinates outside [0, w-1] / [0, h-1] are clamped to the edge so
    // the caller can safely sample slightly out of bounds.
    static QColor bilinearSample(const QImage& img, qreal x, qreal y);

private:
    // Maximum displacement magnitude across all mesh vertices.
    qreal maxDisplacementMagnitude() const;

    LiquifyBackingStore* m_store = nullptr;
};

}  // namespace filters::liquify
