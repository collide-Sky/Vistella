// SPDX-License-Identifier: MIT
//
// SelectionModel
//
// Image selection state holder.
//   - 1-channel QImage mask (Format_Alpha8: 0=out, 255=in)
//   - tight bounding box (QRect, invalid when empty)
//   - SelectionMode (Replace/Add/Subtract/Intersect) for setMask compositing
//   - the persistent selection mode (see "selection mode ownership" below)
//   - Changed signal for marching ants + PropertiesDock + ImageProcessor
//
// Selection mode ownership (M2-5):
//   The mode (New / Add / Subtract / Intersect) is a DOCUMENT property in
//   Photoshop: it survives switching from one selection tool to another.
//   It used to live in the tool instance, which is destroyed and recreated
//   on every tool switch, so the mode was silently reset to Replace.
//   SelectionModel lives as long as the document (one per ImageWindow),
//   so it is the single owner of the mode. The selection tools read it
//   from here; they do NOT keep a copy.
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
#include <QPointF>
#include <QRect>
#include <QSize>
#include <Qt>
#include <QTransform>

namespace selection {

class SelectionModel : public QObject
{
    Q_OBJECT
public:
    // One straight piece of the selection outline, in mask pixel
    //   coordinates. The line runs from `a` to `b`.
    struct Segment {
        QPointF a;
        QPointF b;
    };

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

    // PS select > Modify 5 项 + Transform
    //   5 项都生成新 mask (不直接改),触发 changed signal, bbox recompute.
    //   选区变换 (translate/scale/rotate) — QTransform 应用到 mask 像素坐标.
    //   选区羽化 (feather radiusPx): Gaussian blur alpha8 mask + 阈值化保留边缘
    //   选区扩展 (grow radiusPx): dilation 半径 N 像素
    //   选区收缩 (shrink radiusPx): erosion 半径 N 像素
    //   选区平滑 (smooth radiusPx): medianBlur 平滑 mask
    void feather(int radiusPx);
    void grow(int radiusPx);
    void shrink(int radiusPx);
    void smooth(int radiusPx);
    void transform(const QTransform& t);

    // ===== The persistent selection mode =====
    //   New (Replace) / Add / Subtract / Intersect.
    //   One owner per document -- see the header comment.
    //   Modifier keys still override the mode for a single click
    //   (see modifierKeyToMode).
    Mode selectionMode() const { return m_selectionMode; }
    void setSelectionMode(Mode m);

    // ===== Queries =====
    bool contains(const QPoint& p) const;
    int  pixelCount() const;     // 0..maskSize.width()*height(), 0 if empty

    // The mask alpha at a pixel (0..255), 0 when the pixel is outside
    //   the mask. Used by the paint clip: the alpha scales the paint
    //   weight, so a feathered selection clips proportionally.
    int  alphaAt(const QPoint& p) const;

    // The outline of the selection as a list of straight segments, in
    //   mask pixel coordinates.
    //   The shape follows the real selection: every row is scanned for
    //   runs of selected pixels and the boundary of each run becomes a
    //   segment. It is NOT the bounding box -- a round or lasso
    //   selection gets a round or lasso outline.
    //   Used by the marching ants (ImageCanvas).
    QList<Segment> contourSegments() const;

    // ===== Modifier key -> mode (PS standard) =====
    static Mode modifierKeyToMode(Qt::KeyboardModifiers mods);

signals:
    void changed();                   // mask or bbox changed
    void sizeChanged(const QSize& size);
    void selectionModeChanged(Mode mode);   // the persistent mode changed

private:
    void recomputeBbox();             // scan mask, update m_bbox
    void ensureMaskAllocated();       // allocate m_mask per m_maskSize

    QImage m_mask;                    // Format_Alpha8, 0=out, 255=in
    QSize  m_maskSize;                // image size (or default 0x0)
    QRect  m_bbox;                    // tight bounding box (invalid if empty)

    // The persistent selection mode (M2-5). Lives here because this
    //   object outlives every tool instance.
    Mode   m_selectionMode = Mode::Replace;
};

} // namespace selection
