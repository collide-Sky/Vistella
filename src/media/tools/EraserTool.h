// SPDX-License-Identifier: MIT
//
// EraserTool
//
// The eraser (E).
//
//   Photoshop behaviour:
//     * On a bitmap layer it removes the pixels under the brush
//       (the pixels become transparent -- with the alpha channel the
//       layer has, that is "erase").
//     * On a layer WITHOUT an alpha channel the eraser writes the
//       background colour instead (Photoshop does the same).
//
//   The stroke is painted into the ACTIVE LAYER's pixels, not into
//   the composite cache (docs/CONTRACTS.md): the cache is a render
//   result and the next render would wipe the stroke.
//
//   The undo contract is the same one the brush uses: snapshot the
//   layer on press, write the pixels during the drag, push the
//   before/after pair on release.
//
//   The brush shape / size / flow come from the shared brush state
//   (the same panel the brush tool uses), so the eraser reacts to the
//   option bar immediately.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class EraserTool : public ToolState
{
public:
    EraserTool() = default;
    ~EraserTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::Eraser; }
    QString pageTitle() const override { return QStringLiteral("Eraser"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== test accessors =====
    //   The eraser writes pixels, so the tests need to see whether a
    //   stroke happened at all and how big the brush was.
    bool    isStroking() const { return m_strokeOpen; }
    int     brushSize()   const { return m_brushSize; }
    void    setBrushSize(int px) { m_brushSize = std::max(1, px); }

private:
    QPointer<ImageWindow> m_host;

    bool    m_strokeOpen = false;
    int     m_layerIndex = -1;
    int     m_brushSize  = 20;
    cv::Mat m_backup;              // pre-stroke pixels of the active layer
    QPointF m_lastPos;

    // The brush radius in scene pixels -> the rectangle that gets
    // cleared.
    QRect  brushRect(const QPointF& scenePos) const;
    // Clear the pixels under the brush.
    void   eraseAt(ImageWindow* host, const QPointF& scenePos);
    // Push the before/after pair into the undo stack.
    void   commitStroke(ImageWindow* host);
};

} // namespace tools
