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
    QImage qimg = ImageProcessor::matToQImage(img);
    if (qimg.isNull()) return;

    QPoint srcTl(static_cast<int>(m_sourceA.x()), static_cast<int>(m_sourceA.y()));
    QPoint srcBr(static_cast<int>(m_sourceB.x()), static_cast<int>(m_sourceB.y()));
    QRect srcRect(srcTl, srcBr);
    srcRect = srcRect.normalized();
    QPoint dstTl(static_cast<int>(dstA.x()), static_cast<int>(dstA.y()));
    QPoint dstBr(static_cast<int>(dstB.x()), static_cast<int>(dstB.y()));
    QRect dstRect(dstTl, dstBr);
    dstRect = dstRect.normalized();
    if (srcRect.width() != dstRect.width() || srcRect.height() != dstRect.height()) {
        // P0-9.3 简化: 要求 src 和 dst 同尺寸
        LOG_WARN("[PatchTool] src/dst size mismatch ({}x{} vs {}x{}), skipping",
                 srcRect.width(), srcRect.height(),
                 dstRect.width(), dstRect.height());
        return;
    }
    const QRect imgRect(0, 0, qimg.width(), qimg.height());
    const QRect srcClip = srcRect.intersected(imgRect);
    const QRect dstClip = dstRect.intersected(imgRect);
    if (srcClip.isEmpty() || dstClip.isEmpty()) return;

    for (int y = 0; y < srcClip.height(); ++y) {
        for (int x = 0; x < srcClip.width(); ++x) {
            const QColor c = qimg.pixelColor(srcClip.x() + x, srcClip.y() + y);
            qimg.setPixelColor(dstClip.x() + x, dstClip.y() + y, c);
        }
    }

    cv::Mat newMat = ImageProcessor::qImageToMat(qimg);
    host->setCurrentImage(newMat);
    LOG_INFO("[PatchTool] applied patch ({}x{} region)",
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