// SPDX-License-Identifier: MIT
//
// HealTool implementation - P0-9.4 (2026-09-15)
//
// PS 同款修复画笔 (J) — cv::inpaint 集成:
//   - Alt + 单击取样 source point
//   - 拖动 brush path:
//     1. 在 destination 周围 brush 范围做 inpaint mask
//     2. cv::inpaint (Telea 算法) 用 image 周边纹理填充 mask
//     3. 跟 source 区域混合 (texture transfer 简化版: 直接用 inpaint 结果)
//   - 不需要 cv::seamlessClone (PatchTool 才用)
//
// 算法选择: cv::inpaint (cv::INPAINT_TELEA, PS 同款默认算法)
//
#include "HealTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QImage>
#include <QMouseEvent>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>     // P0-9.4 cv::inpaint

namespace tools {

void HealTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_dragging || !m_hasSample || !host) return;

    auto& img = host->currentImage();
    if (img.empty()) return;
    QImage qimg = ImageProcessor::matToQImage(img);
    if (qimg.isNull()) return;

    // 1. 构造 mask (brush 范围 = 255)
    cv::Mat mask = cv::Mat::zeros(img.size(), CV_8UC1);
    const int half = m_brushSize / 2;
    const int cx = static_cast<int>(scenePos.x());
    const int cy = static_cast<int>(scenePos.y());
    const cv::Rect brushRect(std::max(0, cx - half), std::max(0, cy - half),
                             std::min(m_brushSize, img.cols - std::max(0, cx - half)),
                             std::min(m_brushSize, img.rows - std::max(0, cy - half)));
    if (brushRect.width() <= 0 || brushRect.height() <= 0) return;
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