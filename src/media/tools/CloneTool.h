// SPDX-License-Identifier: MIT
//
// CloneTool - P0-9.3 (2026-09-15)
//
// PS 同款仿制图章 (S):
//   - Alt + 单击: 取样 (记录 source point)
//   - 单击 + 拖动: 在 destination 把 source 区域像素复制过来
//   - optionPage: brush size + hardness
//
// 算法 (P0-9.3 简化版):
//   - 用 cv::Mat + cv::Rect 切片 + 粘贴
//   - 不做边缘 blending (PS 完整版有 hardness), 简化为纯 copy
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

class CloneTool : public ToolState
{
public:
    explicit CloneTool(QWidget* parent = nullptr);
    ~CloneTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Clone; }
    QString pageTitle() const override { return QStringLiteral("仿制图章"); }
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    bool    hasSample() const { return m_hasSample; }
    QPointF samplePoint() const { return m_samplePoint; }
    int     brushSize() const { return m_brushSize; }

    void clearSampleForTest() { m_hasSample = false; }

private:
    QPointer<ImageWindow> m_host;
    bool    m_hasSample = false;
    QPointF m_samplePoint;
    QPointF m_lastDstPos;       // 上一次 destination 鼠标位置 (算 source offset)
    bool    m_dragging = false;

    int     m_brushSize = 20;

    void paintAt(QImage& img, const QPointF& dst, const QPointF& src);
};

} // namespace tools