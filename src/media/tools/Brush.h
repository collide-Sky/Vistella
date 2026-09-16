// SPDX-License-Identifier: MIT
//
// Brush tool - P1.1 (2026-09-15) Brush full implementation
//   PS-style painting tool: stroke smoothing + pressure dynamics + stamp-based paint.
//   Owns BrushPreset (current brush config) + StrokeSmoother + PressureCurve.
//   Strokes are pushed as ImageEditCommand to m_undoStack for undo/redo.
//
//   Hooks into ToolState event chain via ImageWindow's eventFilter
//   (see tools/ToolContext::forwardEvent).

#pragma once

#include "ToolState.h"

#include <QColor>
#include <QPointF>

#include <opencv2/core.hpp>

#include "../brushes/BrushPreset.h"

class QMouseEvent;
class QWidget;
class QUndoStack;

namespace brushes {
class PressureCurve;
class StrokeSmoother;
}

namespace tools {

class Brush : public ToolState
{
public:
    explicit Brush(QWidget* parent = nullptr);
    ~Brush() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    mediators::ToolId id() const override;
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // Mouse events from ToolContext (called by ImageWindow eventFilter)
    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    // Active brush config (set by BrushOptionsPanel)
    void setPreset(const brushes::BrushPreset &p);
    const brushes::BrushPreset& preset() const { return m_preset; }

    // Paint color (foreground)
    void setPaintColor(const QColor &c);
    QColor paintColor() const;

    // Getters for UI panels
    brushes::StrokeSmoother* smoother() const { return m_smoother; }
    brushes::PressureCurve*  pressureCurve() const { return m_pressureCurve; }

private:
    void paintSingleStamp(ImageWindow* host, const QPointF& scenePos, double pressure);
    void finalizeStroke(ImageWindow* host);

    brushes::BrushPreset     m_preset;
    brushes::PressureCurve*   m_pressureCurve;
    brushes::StrokeSmoother* m_smoother;
    QColor                    m_paintColor;

    // Stroke state
    bool                      m_active = false;
    cv::Mat                   m_backup;          // pre-stroke image (for undo)
    QPointF                   m_lastStampScene;  // last stamp position (for spacing)
    double                    m_lastStampDist = 0;
};

} // namespace tools