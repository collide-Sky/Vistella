// SPDX-License-Identifier: MIT
//
// HistoryBrushTool
//
// The history brush (Photoshop: paint back an earlier version of the
// document).
//
//   The user picks a history state and then paints; the brush writes the
//   pixels of THAT state into the current layer.
//
//   The undo stack in this project keeps only the CHANGED PATCH of each
//   command (M1-7: a full-layer clone per stroke cost 288 MB on a 24 MP
//   image), so the undo stack cannot hand back "the document as it was
//   at step N". Reconstructing it by replaying the patches would cost
//   more memory than the stack itself.
//
//   So the history brush paints from a SNAPSHOT: the document's
//   composited pixels captured at a moment the user chose. The snapshot
//   is a full copy of the composite, taken on demand.
//
//   The paint goes into the ACTIVE LAYER's pixels, and the whole stroke
//   is one undo command.
//
//   The selection clips the stroke, and that clipping happens ONCE --
//   centrally, in ImageWindow::pushLayerImageEdit().
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>
#include <QString>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class HistoryBrushTool : public ToolState
{
public:
    HistoryBrushTool() = default;
    ~HistoryBrushTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::HistoryBrush; }
    QString pageTitle() const override { return QStringLiteral("History Brush"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== the source snapshot =====
    // The snapshot the brush paints from. Empty means "no source" and
    // the tool does nothing.
    void setSource(const cv::Mat& snapshot) { m_source = snapshot.clone(); }
    const cv::Mat& source() const { return m_source; }
    bool hasSource() const { return !m_source.empty(); }

    // Capture the CURRENT composited document as the source.
    //
    //   Returns false when there is nothing to capture (no host, no
    //   layer stack, empty render).
    bool captureCurrent(ImageWindow* host);

    // ===== configuration =====
    // The brush diameter in pixels.
    void setBrushSize(int px) { m_brushSize = std::clamp(px, 1, 500); }
    int  brushSize() const { return m_brushSize; }

    // How strongly the source replaces the current pixels, 0..100.
    void setStrength(int percent) { m_strength = std::clamp(percent, 0, 100); }
    int  strength() const { return m_strength; }

    // ===== test accessors =====
    int  lastPaintCount() const { return m_lastPaintCount; }

    // Paint the source over the target at (x,y) with the given radius.
    //
    //   Static so the test can exercise the blending without a host.
    //   `strength` is 0..100: 100 replaces the pixel outright, 50 mixes
    //   the source and the target evenly.
    //   Returns the number of pixels that were written.
    static int paintSource(cv::Mat& target, const cv::Mat& source,
                           int cx, int cy, int radius, int strength);

private:
    QPointer<ImageWindow> m_host;

    cv::Mat m_source;
    int m_brushSize = 30;
    int m_strength = 100;

    // Stroke state.
    bool m_dragging = false;
    int  m_layerIndex = -1;
    cv::Mat m_backup;
    int  m_lastPaintCount = 0;
    QPointF m_lastPaintPos;
    bool m_haveLastPaint = false;
};

} // namespace tools
