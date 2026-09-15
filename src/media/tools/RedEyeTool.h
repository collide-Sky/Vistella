// SPDX-License-Identifier: MIT
//
// RedEyeTool - P0-9.3 (2026-09-15)
//
// PS 同款红眼工具:
//   - 拖矩形选择红眼区域
//   - 检测红色像素 (R > 1.5*G AND R > 1.5*B)
//   - 对这些像素: 替换 R = (G+B)/2 (PS 同款 desaturate red)
//
// 算法 (简化版):
//   - 每个像素判断 red-eye condition
//   - apply PS 同款 red eye reduction formula
//
#pragma once

#include "ToolState.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;

namespace tools {

class RedEyeTool : public ToolState
{
public:
    explicit RedEyeTool(QWidget* parent = nullptr);
    ~RedEyeTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::RedEye; }
    QString pageTitle() const override { return QStringLiteral("红眼工具"); }
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    static bool isRedEyePixel(int r, int g, int b);

private:
    QPointer<ImageWindow> m_host;
    QPointF m_pressScenePos;
    bool    m_dragging = false;

    void applyRedEyeAt(ImageWindow* host, const QPointF& a, const QPointF& b);
};

} // namespace tools