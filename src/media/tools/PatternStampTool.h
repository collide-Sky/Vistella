// SPDX-License-Identifier: MIT
//
// PatternStampTool
//
// The pattern stamp (Photoshop: the paint bucket of textures).
//
//   The user alt-clicks a source area, and the tool then stamps that
//   patch of pixels repeatedly along the drag -- a repeating pattern,
//   not a one-off copy. That is the difference from the clone stamp,
//   which paints the source pixels at an offset that follows the mouse.
//
//   The source patch is captured ONCE, at the alt-click, and the stamps
//   are tiled from that snapshot. Tiling from a LIVE source would make
//   the pattern smear as the tool painted over the source area -- the
//   same pixels would be copied into the pixels they came from.
//
//   The stamps are written into the ACTIVE LAYER's pixels and pushed as
//   one undo command, like every other pixel-editing tool.
//
//   The selection clips the stroke, and that clipping happens ONCE --
//   centrally, in ImageWindow::pushLayerImageEdit().
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class PatternStampTool : public ToolState
{
public:
    PatternStampTool() = default;
    ~PatternStampTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::PatternStamp; }
    QString pageTitle() const override { return QStringLiteral("Pattern Stamp"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== configuration =====
    // The size of the source patch that gets tiled, in pixels.
    void setPatchSize(int px) { m_patchSize = std::clamp(px, 4, 512); }
    int  patchSize() const { return m_patchSize; }

    // The distance between two stamps, as a fraction of the patch size
    // (in percent). 100 = the stamps tile edge to edge, 50 = they
    // overlap by half.
    void setSpacing(int percent) { m_spacing = std::clamp(percent, 5, 200); }
    int  spacing() const { return m_spacing; }

    // ===== test accessors =====
    bool hasSource() const { return m_hasSource; }
    QPointF sourcePoint() const { return m_sourcePoint; }
    int  lastStampCount() const { return m_lastStampCount; }
    // The captured source patch (empty until the user alt-clicks).
    cv::Mat sourcePatch() const { return m_sourcePatch; }

    // Capture a source patch from the layer's pixels.
    //
    //   Returns an empty Mat when the layer has no pixels or the patch
    //   would fall entirely outside it.
    static cv::Mat capturePatch(const cv::Mat& layer, const QPointF& center, int patchSize);

private:
    // Stamp the pattern once at (scenePos).
    void stampAt(ImageWindow* host, const QPointF& scenePos);

    QPointer<ImageWindow> m_host;

    int  m_patchSize = 32;
    int  m_spacing = 100;
    bool m_hasSource = false;
    QPointF m_sourcePoint;
    cv::Mat m_sourcePatch;

    // Stroke state.
    bool m_dragging = false;
    int  m_layerIndex = -1;
    cv::Mat m_backup;
    int  m_lastStampCount = 0;
    // The last stamp position, so the spacing walk does not stamp twice
    // on the same spot.
    QPointF m_lastStampPos;
    bool m_haveLastStamp = false;
};

} // namespace tools
