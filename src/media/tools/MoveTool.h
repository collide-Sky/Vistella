// SPDX-License-Identifier: MIT
//
// MoveTool
//
// State pattern concrete class - Move tool (V)
//
// M2-4: the tool used to be an empty shell -- it showed a status bar
//   message and never moved anything. The drag now really moves one
//   of three things, in Photoshop's priority order:
//
//   1. A text item under the cursor (the item is dragged in the scene)
//   2. The selection (the selection mask is dragged with it)
//   3. The active layer's pixels (the layer content is dragged within
//      the canvas)
//
// The pixels of a layer are dragged with the SAME undo contract the
// brush uses: the pre-drag pixels are snapshotted on press, the
// layer buffer is written live during the drag, and on release the
// before/after pair is pushed as one undo command. Anything else
// would lose the move from the undo history.
//
// The tool needs the mouse for itself, so it declares
// wantsExclusiveMouse(): the canvas runs with ScrollHandDrag always
// on and would otherwise scroll the view instead of moving the
// content.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QRect>

#include <opencv2/core.hpp>

class GraphicsTextItem;
class QImage;

namespace tools {

class MoveTool : public ToolState
{
public:
    MoveTool() = default;
    ~MoveTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host)  override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    // M2-4: the move tool drags content, so the canvas must not pan.
    bool wantsExclusiveMouse() const override { return true; }

    mediators::ToolId id()        const override { return mediators::ToolId::Move; }
    QString pageTitle() const override { return QStringLiteral("Move"); }
    QCursor cursor()   const override { return QCursor(Qt::SizeAllCursor); }

    // ===== test accessors =====
    //   The move behaviour is pixel-level, so the tests need to see
    //   what the tool did. These are read-only views of the internal
    //   drag state.
    enum class DragTarget {
        None,       // no drag in progress
        TextItem,   // a text item is being dragged
        Selection,  // the selection mask is being dragged
        LayerPixels // the active layer's pixels are being dragged
    };

    DragTarget dragTarget() const { return m_target; }
    bool        isDragging()    const { return m_target != DragTarget::None; }
    // The last applied offset, in image pixels.
    QPointF     lastDelta()     const { return m_lastDelta; }

private:
    // Which kind of thing the current drag moves.
    DragTarget m_target = DragTarget::None;

    // Text item drag
    GraphicsTextItem* m_dragItem = nullptr;
    QPointF m_pressScenePos;      // scene position at press
    QPointF m_pressItemPos;        // item position at press

    // Selection drag
    QRect   m_selBBoxAtPress;      // selection bounding box at press
    QImage  m_selMaskAtPress;      // selection mask as it was at press

    // Layer pixel drag
    int     m_layerIndex = -1;
    cv::Mat m_backup;              // pre-drag pixels of the active layer
    QPointF m_lastDelta;           // last applied offset

    // Helpers
    GraphicsTextItem* textItemAt(ImageWindow* host, const QPointF& scenePos) const;
    void beginTextDrag(ImageWindow* host, GraphicsTextItem* item, const QPointF& scenePos);
    void beginSelectionDrag(ImageWindow* host, const QPointF& scenePos);
    void beginLayerDrag(ImageWindow* host, const QPointF& scenePos);
    void applyTextDrag(const QPointF& scenePos);
    void applySelectionDrag(ImageWindow* host, const QPointF& scenePos);
    void applyLayerDrag(ImageWindow* host, const QPointF& scenePos);
    void finishDrag(ImageWindow* host);
    void cancelDrag(ImageWindow* host);
};

} // namespace tools
