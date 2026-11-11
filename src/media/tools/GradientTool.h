// SPDX-License-Identifier: MIT
//
// GradientTool
//
// The gradient tool (G).
//
//   The user drags a line; the tool fills the selection (or the whole
//   layer when there is no selection) with a linear gradient that runs
//   along the drag.
//
//   The gradient runs from the foreground colour (at the drag start) to
//   the background colour (at the drag end) -- the Photoshop default.
//
//   Types: linear, radial and angular. The type comes from the option
//   page.
//
//   The fill is written into the ACTIVE LAYER's pixels and pushed as
//   one undo command (the same contract as the brush).
//
//   The gradient is computed in the layer's own channel layout: a
//   4-channel BGRA layer keeps its alpha (the gradient is written
//   opaque), a 3-channel BGR layer gets the colour directly.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class GradientTool : public ToolState
{
public:
    enum class GradientType {
        Linear = 0,
        Radial = 1,
        Angular = 2
    };

    GradientTool() = default;
    ~GradientTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::Gradient; }
    QString pageTitle() const override { return QStringLiteral("Gradient"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== configuration =====
    void setType(GradientType t) { m_type = t; }
    GradientType type() const { return m_type; }

    // ===== test accessors =====
    bool    isDragging()  const { return m_dragging; }
    QPointF startPoint()  const { return m_start; }
    QPointF endPoint()    const { return m_end; }

    // The gradient factor at a point, in [0, 1].
    //
    //   This is the core of the tool and it is exposed so that the
    //   test can check the gradient itself without having to build a
    //   whole window.
    double  gradientFactor(const QPointF& p, const QPointF& start,
                            const QPointF& end, GradientType type) const;

private:
    QPointer<ImageWindow> m_host;

    bool    m_dragging = false;
    int     m_layerIndex = -1;
    cv::Mat m_backup;
    QPointF m_start;
    QPointF m_end;
    GradientType m_type = GradientType::Linear;

    void applyGradient(ImageWindow* host);
    void commitStroke(ImageWindow* host);
};

} // namespace tools
