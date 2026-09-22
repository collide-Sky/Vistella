// SPDX-License-Identifier: MIT
//
// HealTool implementation - P0-9.4 (2026-09-15) + P2.2 (2026-09-22)
//
// PS-style Healing Brush (J) — cv::inpaint integration:
//   - Alt + click: sample source point
//   - drag brush path:
//     1. Build inpaint mask covering brush radius around current position
//     2. cv::inpaint (Telea algorithm) fills mask using surrounding texture
//     3. Write inpainted region back to m_current
//
// P2.2 (2026-09-22):
//   - Page title i18n override (parent class uses default "Clone Stamp")
//   - Inherits ImageEditCommand undo integration + QSlider optionPage from
//     CloneTool base (HealTool doesn't override optionPage — base UI is fine)
//
#include "HealTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>     // P0-9.4 cv::inpaint

namespace tools {

QString HealTool::pageTitle() const
{
    return QCoreApplication::translate("tools::HealTool", "Healing Brush");
}

void HealTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    showBrushCursor(host, scenePos);
    if (!m_dragging || !m_hasSample || !host) return;

    auto& img = host->currentImage();
    if (img.empty()) return;

    // 1. 构造 mask (brush 范围 = 255)
    cv::Mat mask = cv::Mat::zeros(img.size(), CV_8UC1);
    const int half = m_brushSize / 2;
    const int cx = static_cast<int>(scenePos.x());
    const int cy = static_cast<int>(scenePos.y());
    const cv::Rect brushRect(std::max(0, cx - half), std::max(0, cy - half),
                             std::min(m_brushSize, img.cols - std::max(0, cx - half)),
                             std::min(m_brushSize, img.rows - std::max(0, cy - half)));
    if (brushRect.width <= 0 || brushRect.height <= 0) return;
    cv::rectangle(mask, brushRect, cv::Scalar(255), cv::FILLED);

    // 2. cv::inpaint (Telea, PS 同款默认) — 修复 brush 区域
    cv::Mat inpainted;
    cv::inpaint(img, mask, inpainted, 3, cv::INPAINT_TELEA);

    // 3. 写回 img (inplace 写 brushRect 区域)
    cv::Mat roi = inpainted(brushRect);
    roi.copyTo(img(brushRect));

    m_lastDstPos = scenePos;
    host->setCurrentImage(img);
}

} // namespace tools