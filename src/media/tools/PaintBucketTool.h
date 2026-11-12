// SPDX-License-Identifier: MIT
//
// PaintBucketTool
//
// The paint bucket / fill (K).
//
//   The user clicks on a pixel and the tool fills the contiguous region
//   of similar colour around it (the Photoshop "contiguous" fill).
//
//   The tolerance decides how different two neighbouring pixels may be
//   and still count as "the same region" (the Photoshop default is 32;
//   the option page changes it).
//
//   The fill is written into the ACTIVE LAYER's pixels and pushed as
//   one undo command.
//
//   The region is found with a scanline flood fill on the layer's own
//   pixels -- the fill does NOT cross the selection boundary, and the
//   selection acts as a mask on the result.
//
//   The selection clipping itself happens in ONE place:
//   ImageWindow::pushLayerImageEdit() -> clipEditToSelection(). Doing it
//   here as well would mean two clippers with two different rules (a hard
//   cut here, a feathered blend there) and the feather would be lost.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class PaintBucketTool : public ToolState
{
public:
    PaintBucketTool() = default;
    ~PaintBucketTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::PaintBucket; }
    QString pageTitle() const override { return QStringLiteral("Paint Bucket"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== configuration =====
    void setTolerance(int t) { m_tolerance = std::clamp(t, 0, 255); }
    int  tolerance() const { return m_tolerance; }

    // ===== test accessors =====
    //   The number of pixels the last fill REALLY touched -- counted
    //   AFTER the selection clip, so it is the number of pixels the user
    //   ended up with, not the number the tool painted before the clip.
    //
    //   A test needs this to tell "the fill happened" from "the fill
    //   happened but in the wrong place" -- the count alone is not
    //   enough, so the pixel probes do the real work.
    int  lastFilledPixelCount() const { return m_lastFilledCount; }

    // Find the region that a fill at (x,y) would cover.
    //
    //   Returns a mask (CV_8UC1, 255 = inside the region).
    static cv::Mat findRegion(const cv::Mat& layer, int x, int y, int tolerance);

private:
    // Count the pixels in `layer` that carry the fill colour, up to
    //   `limit`. Used to report the post-clip fill size.
    static int countFilledPixels(const cv::Mat& layer, const QColor& fg, int limit);

    QPointer<ImageWindow> m_host;

    int  m_tolerance = 32;
    int  m_lastFilledCount = 0;
    int  m_layerIndex = -1;
    cv::Mat m_backup;
};

} // namespace tools
