// SPDX-License-Identifier: MIT
//
// SelectionModel - P0-4.1 (2026-09-10)
//
// Image selection state holder.
//   - 1-channel QImage mask (Format_Alpha8: 0=out, 255=in)
//   - tight bounding box (QRect, invalid when empty)
//   - SelectionMode (Replace/Add/Subtract/Intersect) for setMask compositing
//   - Changed signal for marching ants + PropertiesDock + ImageProcessor
//
// Design (Strategy pattern - P0-4.2):
//   SelectionModel owns the canonical mask.
//   SelectionStrategy implementations (Rect/Lasso/MagicWand/ColorRange) compute
//   a NEW mask from a gesture, then call setMask(newMask, mode) on SelectionModel.
//   SelectionModel applies mode compositing and recomputes bbox.
//
// Storage: Format_Alpha8 (1 byte/pixel) — fits mask, fast memcopy, easy to render.
//   Image size = mask size (1:1 pixel mapping). setSize() resizes.
//
// Usage:
//   SelectionModel m;                          // owned by ImageWindow
//   m.setSize(image.size());
//   RectSelectStrategy s;
//   QImage newMask = s.computeMask(...);       // 1-channel
//   m.setMask(newMask, SelectionModel::Replace);
//
#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QSize>
#include <Qt>

namespace selection {

class SelectionModel : public QObject
{
    Q_OBJECT
public:
    enum class Mode {
        Replace   = 0,   // default, overwrites
        Add       = 1,   // union (Shift)
        Subtract  = 2,   // difference (Alt)
        Intersect = 3,   // intersection (Shift+Alt)
    };
    Q_ENUM(Mode)

    explicit SelectionModel(QObject* parent = nullptr);
    ~SelectionModel() override;

    // ===== Mask access =====
    QImage mask() const { return m_mask; }
    bool   isEmpty() const { return m_bbox.isEmpty(); }
    QRect  boundingRect() const { return m_bbox; }
    QSize  size() const { return m_maskSize; }

    // ===== Mutators (push to undo stack from caller, NOT here) =====
    void setSize(const QSize& size);
    void clear();
    void selectAll();
    void invert();
    // Compose newMask into current mask per mode
    void setMask(const QImage& newMask, Mode mode = Mode::Replace);

    // ===== Queries =====
    bool contains(const QPoint& p) const;
    int  pixelCount() const;     // 0..maskSize.width()*height(), 0 if empty

    // ===== Modifier key -> mode (PS standard) =====
    static Mode modifierKeyToMode(Qt::KeyboardModifiers mods);

signals:
    void changed();                   // mask or bbox changed
    void sizeChanged(const QSize& size);

private:
    void recomputeBbox();             // scan mask, update m_bbox
    void ensureMaskAllocated();       // allocate m_mask per m_maskSize

    QImage m_mask;                    // Format_Alpha8, 0=out, 255=in
    QSize  m_maskSize;                // image size (or default 0x0)
    QRect  m_bbox;                    // tight bounding box (invalid if empty)
};

} // namespace selection
