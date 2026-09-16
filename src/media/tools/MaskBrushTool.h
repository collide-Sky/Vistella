#pragma once

#include <QPointF>
#include <opencv2/core.hpp>

#include "ToolState.h"
#include "mediators/ToolMediator.h"

class QMouseEvent;
class ImageWindow;

namespace tools {

// Mask edit brush.
//
// Operates on the currently selected layer's pixel mask
// (LayerMask::pixel).  When the user enters this tool the layer's mask
// auto-promotes to a pixel mask (initialized to all-zero = "fully
// hidden") if it is not already one.
//
// Brush parameters:
//   size       - stamp radius in image pixels
//   hardness   - 0..1 (0 = soft edge, 1 = hard edge)
//   opacity    - 0..1, how strongly each stamp contributes (alpha
//                blend toward target colour)
//   mode       - Reveal (paint white) / Hide (paint black)
//
// A complete mouse press / move / release stroke emits a single
// MaskBrushCommand into the host's undo stack.
class MaskBrushTool : public ToolState {
public:
    explicit MaskBrushTool();

    // ToolState interface
    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;
    void onMousePress(QMouseEvent* e, ImageWindow* host,
                      const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host,
                     const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host,
                        const QPointF& scenePos) override;

    mediators::ToolId id() const override {
        return mediators::ToolId::MaskBrush;
    }
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // Configuration (called by MaskOptionsPanel).
    void setBrushSize(int px)       { m_brushSize = px; }
    int  brushSize() const          { return m_brushSize; }
    void setHardness(qreal h)       { m_hardness = qBound(qreal(0.0), h, qreal(1.0)); }
    qreal hardness() const          { return m_hardness; }
    void setOpacity(qreal o)        { m_opacity = qBound(qreal(0.0), o, qreal(1.0)); }
    qreal opacity() const           { return m_opacity; }

    enum Mode { Reveal, Hide };
    void setMode(Mode m)            { m_mode = m; }
    Mode mode() const               { return m_mode; }

    // Gaussian-falloff stamp: 1.0 at center, 0 outside `radius`.
    // Public so unit tests can verify the shape directly.
    static qreal stampFalloff(qreal distance, qreal radius, qreal hardness);

private:
    void paintStamp(const QPointF& scenePos, ImageWindow* host);

    QWidget* m_optionPage = nullptr;

    // Brush state.
    int   m_brushSize = 50;
    qreal m_hardness  = 0.7;
    qreal m_opacity   = 1.0;
    Mode  m_mode      = Reveal;

    // Active stroke.
    bool          m_painting = false;
    QPointF       m_lastPos;
    cv::Mat       m_strokeBefore;   // snapshot of mask at stroke start
    int           m_layerIdx = -1;
};

}  // namespace tools