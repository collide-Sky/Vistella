// SPDX-License-Identifier: MIT
//
// Crop - F-L (2026-09-10) + P2.3 (2026-09-22)
//
// PS-style Crop tool (C):
//   - drag rectangle on canvas: live preview overlay
//   - release: apply crop (cv::Mat roi + adjust size + push ImageEditCommand)
//
// P2.3 (2026-09-22):
//   - Full mouse press/move/release wiring + applyCrop
//   - optionPage: Width / Height / Aspect Ratio (Free / 1:1 / 4:3 / 16:9)
//   - ImageEditCommand undo integration (MosaicTool pattern)
//   - i18n title via QCoreApplication::translate
//
#pragma once

#include "ToolState.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>
#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class Crop : public ToolState
{
public:
    explicit Crop(QWidget* parent = nullptr);
    ~Crop() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::Crop; }
    QString pageTitle() const override;
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P2.3: aspect ratio lock (Free or fixed W:H)
    enum class AspectRatio { Free = 0, Ratio1_1 = 1, Ratio4_3 = 2, Ratio16_9 = 3 };
    void setAspectRatio(AspectRatio a) { m_aspect = a; }
    AspectRatio aspectRatio() const { return m_aspect; }

    // P2.3: test accessors
    bool    hasCropRect() const { return m_hasCrop; }
    QPointF cropA() const { return m_cropA; }
    QPointF cropB() const { return m_cropB; }

private:
    QPointer<ImageWindow> m_host;
    bool    m_dragging = false;
    bool    m_hasCrop = false;
    QPointF m_cropA, m_cropB;     // current drag rectangle (image space)
    AspectRatio m_aspect = AspectRatio::Free;

    void applyCrop(ImageWindow* host);
    QRect computeClampedRect(int x1, int y1, int x2, int y2) const;
};

} // namespace tools