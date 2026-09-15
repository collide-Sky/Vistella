// SPDX-License-Identifier: MIT
//
// PatchTool - P0-9.3 (2026-09-15)
//
// PS 同款修补工具:
//   - 拖矩形选 source 区域
//   - release 后, 拖动 destination 让 source 区域复制到 destination
//
// P0-9.3 简化版:
//   - source 矩形 + destination 矩形 = source 像素复制到 destination
//   - PS 完整版用 Content Aware Fill (纹理合成) 做无缝修复
//   - TODO (P0-9.4): 加 cv::seamlessClone 或 inpaint
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

class PatchTool : public ToolState
{
public:
    explicit PatchTool(QWidget* parent = nullptr);
    ~PatchTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::Patch; }
    QString pageTitle() const override { return QStringLiteral("修补工具"); }
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    bool    hasSource() const { return m_hasSource; }
    QPointF sourceA() const { return m_sourceA; }
    QPointF sourceB() const { return m_sourceB; }

    void clearSourceForTest() { m_hasSource = false; }

private:
    QPointer<ImageWindow> m_host;
    QPointF m_pressScenePos;
    QPointF m_lastScenePos;
    bool    m_hasSource = false;
    QPointF m_sourceA, m_sourceB;       // source rectangle
    bool    m_selectingSource = false;  // 第一次拖: 选 source; 第二次拖: 选 destination

    void applyPatch(ImageWindow* host, const QPointF& dstA, const QPointF& dstB);
};

} // namespace tools