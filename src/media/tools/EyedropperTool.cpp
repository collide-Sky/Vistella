// SPDX-License-Identifier: MIT
//
// EyedropperTool implementation - F-C (2026-09-09)
//
#include "EyedropperTool.h"

#include "../imagewindow.h"

#include <QStatusBar>
#include <QMouseEvent>
#include <opencv2/core.hpp>

namespace tools {

void EyedropperTool::onEnter(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Eyedropper (I) - click on canvas to sample color"), 5000);
    }
}

void EyedropperTool::onExit(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->clearMessage();
    }
}

void EyedropperTool::onMousePress(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    // F-C stage: sample from currentImage directly
    //   Full version (F-K): DialogMediator pushes to color panel + LayerCommand
    if (!host) return;
    const cv::Mat& img = host->currentImage();
    if (img.empty()) return;
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    if (x < 0 || y < 0 || x >= img.cols || y >= img.rows) return;
    // cv::Mat is BGR, Qt uses RGB
    const cv::Vec3b bgr = img.at<cv::Vec3b>(y, x);
    m_sampled = QColor(bgr[2], bgr[1], bgr[0]);
    // Status bar feedback
    if (host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Eyedropper: (%1,%2) = #%3%4%5")
                .arg(x).arg(y)
                .arg(m_sampled.red(),   2, 16, QChar('0'))
                .arg(m_sampled.green(), 2, 16, QChar('0'))
                .arg(m_sampled.blue(),  2, 16, QChar('0')),
            3000);
    }
}

} // namespace tools
