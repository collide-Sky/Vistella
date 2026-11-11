// SPDX-License-Identifier: MIT
//
// Crop
//
// PS-style Crop tool (C):
//   - drag rectangle on canvas: live preview overlay
//   - release: apply crop (cv::Mat roi + adjust size + push ImageEditCommand)
//
//   - Full mouse press/move/release wiring + applyCrop
//   - optionPage: Aspect Ratio (Free / 1:1 / 4:3 / 16:9)
//   - ImageEditCommand undo integration (MosaicTool pattern)
//   - i18n title via QCoreApplication::translate
//
// M2-4 的改动:
//   - 宽高比真的生效了. 之前 m_aspect 只在选项面板里被赋值, 裁剪框从来
//     不看它 —— 用户选 1:1 / 4:3 / 16:9, 裁出来的还是任意比例.
//     现在拖拽时按锚点反算另一轴, 保持锁定比例.
//   - 去掉 m_host 的硬依赖: 之前 onMousePress 只看 onEnter 缓存的 m_host,
//     不看传进来的 host, 所以没走过 onEnter 的工具实例会静默丢掉所有
//     鼠标事件 (裁剪完全没反应).
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

    // The width / height ratio the current lock asks for.
    //
    //   Returns 0 for Free (no lock).
    double aspectValue() const;

    // The other end of the drag rectangle, with the aspect lock applied.
    //
    //   `anchor` is the fixed corner (where the drag started) and
    //   `current` is where the pointer is. The locked axis is derived
    //   from the free one, so the rectangle keeps the requested ratio
    //   while the user drags.
    QPointF applyAspect(const QPointF& anchor, const QPointF& current) const;
};

} // namespace tools
