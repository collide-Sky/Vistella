// SPDX-License-Identifier: MIT
//
// PatchTool implementation - P0-9.3 (2026-09-15)
//
// 详见 PatchTool.h 头注释
//
#include "PatchTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QImage>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>     // P0-9.4 cv::rectangle + cv::Point
#include <opencv2/photo.hpp>      // P0-9.4 cv::seamlessClone

namespace tools {

PatchTool::PatchTool(QWidget* /*parent*/) : ToolState() {}

void PatchTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_hasSource = false;
    m_selectingSource = false;
    LOG_DEBUG("[PatchTool] onEnter host={}", host ? "yes" : "null");
}

void PatchTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_hasSource = false;
    m_selectingSource = false;
    m_host = nullptr;
    LOG_DEBUG("[PatchTool] onExit");
}

void PatchTool::applyPatch(ImageWindow* host, const QPointF& dstA, const QPointF& dstB)
{
    if (!host || !m_hasSource) return;
    auto& img = host->currentImage();
    if (img.empty()) return;

    QPoint srcTl(static_cast<int>(m_sourceA.x()), static_cast<int>(m_sourceA.y()));
    QPoint srcBr(static_cast<int>(m_sourceB.x()), static_cast<int>(m_sourceB.y()));
    QRect srcRect(srcTl, srcBr);
    srcRect = srcRect.normalized();
    QPoint dstTl(static_cast<int>(dstA.x()), static_cast<int>(dstA.y()));
    QPoint dstBr(static_cast<int>(dstB.x()), static_cast<int>(dstB.y()));
    QRect dstRect(dstTl, dstBr);
    dstRect = dstRect.normalized();

    const QRect imgRect(0, 0, img.cols, img.rows);
    const QRect srcClip = srcRect.intersected(imgRect);
    const QRect dstClip = dstRect.intersected(imgRect);
    if (srcClip.isEmpty() || dstClip.isEmpty()) return;

    if (srcClip.width() != dstClip.width() || srcClip.height() != dstClip.height()) {
        // P0-9.4 简化: src/dst 不同尺寸不处理
        LOG_WARN("[PatchTool] src/dst size mismatch ({}x{} vs {}x{}), skipping",
                 srcClip.width(), srcClip.height(),
                 dstClip.width(), dstClip.height());
        return;
    }

    // P0-9.4 (2026-09-15): 用 cv::seamlessClone 做 Content Aware Fill
    //   cv::seamlessClone(src, dst, mask, result, centerPoint)
    //   - src: source 矩形 (从 image 切片)
    //   - dst: destination image
    //   - mask: source 区域 mask (255 在 source 区域)
    //   - centerPoint: destination 中心 (在 dst image 中)
    const cv::Rect srcCv(srcClip.x(), srcClip.y(), srcClip.width(), srcClip.height());
    cv::Mat sourceRoi = img(srcCv).clone();

    cv::Mat mask = cv::Mat::zeros(sourceRoi.size(), CV_8UC1);
    cv::rectangle(mask, cv::Rect(0, 0, sourceRoi.cols, sourceRoi.rows), cv::Scalar(255), cv::FILLED);

    const cv::Point center(dstClip.x() + dstClip.width() / 2,
                           dstClip.y() + dstClip.height() / 2);

    cv::Mat result;
    try {
        // signature: seamlessClone(src, dst, mask, center, blend, flags)
        cv::seamlessClone(sourceRoi, img, mask, center, result, cv::NORMAL_CLONE);
    } catch (const cv::Exception& e) {
        LOG_WARN("[PatchTool] cv::seamlessClone failed: {}, fallback to copy", e.what());
        result = img.clone();
        const cv::Rect dstCv(dstClip.x(), dstClip.y(), dstClip.width(), dstClip.height());
        sourceRoi.copyTo(result(dstCv));
    }
    host->setCurrentImage(result);
    LOG_INFO("[PatchTool] applied seamlessClone patch ({}x{} region)",
             dstClip.width(), dstClip.height());
}

void PatchTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_host || e->button() != Qt::LeftButton) return;
    m_pressScenePos = scenePos;
    m_lastScenePos = scenePos;
}

void PatchTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& scenePos)
{
    m_lastScenePos = scenePos;
}

void PatchTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_host || !host) return;

    if (!m_hasSource) {
        // 第一次拖: 选 source
        m_sourceA = m_pressScenePos;
        m_sourceB = scenePos;
        m_hasSource = true;
        LOG_DEBUG("[PatchTool] source selected: ({},{})..({},{})",
                  m_sourceA.x(), m_sourceA.y(),
                  m_sourceB.x(), m_sourceB.y());
        return;
    }
    // 第二次拖: 选 destination + apply
    applyPatch(host, m_pressScenePos, scenePos);
    m_hasSource = false;
}

QWidget* PatchTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* hint = new QLabel(QStringLiteral("(第一次拖选 source, 第二次拖选 destination)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addWidget(hint);
    layout->addStretch(1);

    LOG_DEBUG("[PatchTool] optionPage created");
    return page;
}

} // namespace tools