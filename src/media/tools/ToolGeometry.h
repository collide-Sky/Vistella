// SPDX-License-Identifier: MIT
//
// ToolGeometry - the shared coordinate helpers for the tools.
//
// M2-4: every tool that writes pixels or paths needs the same three
// things, and each of them used to invent its own (or skip it):
//
//   1. The scene position -> image pixel conversion, CLAMPED to the
//      canvas. The scene is 1:1 with the image pixels (ImageCanvas sets
//      the scene rect to the pixmap rect), but a drag can leave the
//      canvas entirely -- and a tool that writes there either crashes or
//      silently does nothing.
//
//   2. The canvas size, in the same "which document" terms the tools
//      already use.
//
//   3. A drag delta clamped so that the moved content stays inside the
//      canvas (the move tool needs this).
//
// The helpers live here instead of in each tool because a tool that
// clamps differently from its neighbour produces bugs that look like
// "the tool is broken" rather than "the tool used the wrong bound".
#pragma once

#include <QPointF>
#include <QRect>

class QSize;
class ImageWindow;

namespace tools {

class ToolGeometry
{
public:
    // The size of the canvas the scene is 1:1 with.
    //
    //   Returns an empty size when there is no document.
    static QSize canvasSize(ImageWindow* host);

    // Clamp a scene position into the canvas.
    //
    //   The result is always inside [0, w-1] x [0, h-1]. When the
    //   canvas is empty the position is returned unchanged (there is
    //   nothing to clamp against, and the caller will bail out on the
    //   empty image anyway).
    static QPointF clampToCanvas(ImageWindow* host, const QPointF& scenePos);

    // Clamp a rectangle into the canvas.
    //
    //   The rect is shrunk (not moved) when it hangs over the edge, so
    //   a drag that starts outside keeps its size.
    static QRect clampRectToCanvas(ImageWindow* host, const QRect& rect);

    // Clamp a drag delta so that the content stays inside the canvas.
    //
    //   `contentSize` is the size of the thing being moved. The delta
    //   is limited so that the moved content does not leave the canvas
    //   on either side.
    static QPointF clampDeltaToCanvas(ImageWindow* host,
                                      const QPointF& delta,
                                      const QPointF& contentSize);
};

} // namespace tools
