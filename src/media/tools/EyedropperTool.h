// SPDX-License-Identifier: MIT
//
// EyedropperTool - F-C (2026-09-09) + Q4.2.3 (2026-09-24)
//
// State pattern concrete class - Eyedropper (I)
//   cursor: CrossCursor (+)
//   F-C: onMousePress samples pixel from currentImage, stores as m_sampled
//   Q4.2.3: sample size (1=Point, 3=3x3, 5=5x5, 11=11x11) + optionPage
//
#pragma once

#include "ToolState.h"
#include <QColor>

class QCursor;
class QWidget;
class QMouseEvent;

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
    // TODO(P8.5): sampledColor() has no consumer, so the eyedropper only writes the status bar text -- see docs/DEBT.md#A-EYEDROPPER
    QColor sampledColor() const { return m_sampled; }

    mediators::ToolId id()        const override { return mediators::ToolId::Eyedropper; }
    QString pageTitle() const override { return QStringLiteral("Eyedropper"); }
    QCursor cursor()   const override { return QCursor(Qt::CrossCursor); }

    // Q4.2.3: sample size (1=Point, 3=3x3, 5=5x5, 11=11x11) for ModeSelectorBar.
    //   PS standard: Point / 3x3 Average / 5x5 Average / 11x11 Average.
    void   setSampleSize(int s)  { m_sampleSize = s; }
    int    sampleSize()    const { return m_sampleSize; }

    QWidget* optionPage(QWidget* parent = nullptr) override;

private:
    QColor m_sampled;
    int    m_sampleSize = 1;  // 1=Point default
};

} // namespace tools
