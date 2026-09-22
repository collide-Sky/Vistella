// SPDX-License-Identifier: MIT
//
// RedEyeTool - P0-9.3 (2026-09-15) + P2.2 (2026-09-22)
//
// PS-style Red Eye Removal tool:
//   - drag rectangle to select red-eye region
//   - detect red pixels (R > 1.5*G AND R > 1.5*B)
//   - apply desaturate red: R = (G+B)/2 (PS formula)
//
// Algorithm (simplified):
//   - per-pixel red-eye check, apply PS desaturate formula
//
// P2.2 (2026-09-22):
//   - i18n title via QCoreApplication::translate("tools::RedEyeTool", "Red Eye Tool")
//   - optionPage: Pupil Size slider (default 30px) + Darken slider (default 50%)
//   - ImageEditCommand undo: snapshot before apply, push single command after
//
#pragma once

#include "ToolState.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>
#include <opencv2/core.hpp>

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
    QString pageTitle() const override;
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    static bool isRedEyePixel(int r, int g, int b);

    // P2.2: pupil size & darken amount (driven by optionPage sliders)
    int pupilSize() const { return m_pupilSize; }
    int darken() const { return m_darken; }
    void setPupilSize(int s) { m_pupilSize = s; }
    void setDarken(int d) { m_darken = d; }

private:
    QPointer<ImageWindow> m_host;
    QPointF m_pressScenePos;
    bool    m_dragging = false;

    // P2.2: optionPage-driven parameters
    int m_pupilSize = 30;
    int m_darken = 50;        // 0..100 (% of full desaturate)

    // P2.2: undo snapshot
    cv::Mat m_backup;

    void applyRedEyeAt(ImageWindow* host, const QPointF& a, const QPointF& b);
};

} // namespace tools