// SPDX-License-Identifier: MIT
//
// EyedropperTool - F-C (2026-09-09)
//
// State pattern concrete class - Eyedropper (I)
//   cursor: CrossCursor (+)
//   F-C: onMousePress samples pixel from currentImage, stores as m_sampled
//
#pragma once

#include "ToolState.h"
#include <QColor>

namespace tools {

class EyedropperTool : public ToolState
{
public:
    EyedropperTool() = default;
    ~EyedropperTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host)  override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    // Sampled color (F-J DialogMediator will push to color panel)
    QColor sampledColor() const { return m_sampled; }

    mediators::ToolId id()        const override { return mediators::ToolId::Eyedropper; }
    QString pageTitle() const override { return QStringLiteral("Eyedropper"); }
    QCursor cursor()   const override { return QCursor(Qt::CrossCursor); }

private:
    QColor m_sampled;  // BGR->RGB conversion in onMousePress
};

} // namespace tools
