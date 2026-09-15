// SPDX-License-Identifier: MIT
//
// CloneTool implementation - P0-9.3 (2026-09-15)
//
// 详见 CloneTool.h 头注释
//
#include "CloneTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QImage>
#include <QMouseEvent>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

namespace tools {

CloneTool::CloneTool(QWidget* /*parent*/) : ToolState() {}

void CloneTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_hasSample = false;
    m_dragging = false;
    LOG_DEBUG("[CloneTool] onEnter host={}", host ? "yes" : "null");
}

void CloneTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_hasSample = false;
    m_dragging = false;
    m_host = nullptr;
    LOG_DEBUG("[CloneTool] onExit");
}

void CloneTool::paintAt(QImage& img, const QPointF& dst, const QPointF& src)
{
    // 从 src 周围 brushSize 矩形复制到 dst 周围
    const int half = m_brushSize / 2;
    const QRect srcRect(int(src.x()) - half, int(src.y()) - half,
                       m_brushSize, m_brushSize);
    const QRect dstRect(int(dst.x()) - half, int(dst.y()) - half,
                       m_brushSize, m_brushSize);
    // 边界保护
    const QRect imgRect(0, 0, img.width(), img.height());
    const QRect srcClip = srcRect.intersected(imgRect);
    const QRect dstClip = dstRect.intersected(imgRect);
    if (srcClip.isEmpty() || dstClip.isEmpty()) return;

    for (int y = 0; y < srcClip.height(); ++y) {
        for (int x = 0; x < srcClip.width(); ++x) {
            const QColor c = img.pixelColor(srcClip.x() + x, srcClip.y() + y);
            img.setPixelColor(dstClip.x() + x, dstClip.y() + y, c);
        }
    }
}

void CloneTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;

    // Alt + 单击 = 取样
    if (e->modifiers() & Qt::AltModifier && e->button() == Qt::LeftButton) {
        m_samplePoint = scenePos;
        m_hasSample = true;
        LOG_DEBUG("[CloneTool] sampled at ({}, {})", scenePos.x(), scenePos.y());
        return;
    }

    if (e->button() != Qt::LeftButton) return;
    if (!m_hasSample) {
        LOG_WARN("[CloneTool] no sample, Alt+click first to set source");
        return;
    }

    m_lastDstPos = scenePos;
    m_dragging = true;

    // 立即在点击处绘制一次 (跟 PS 同款, click 后立刻 paint 一次)
    auto& img = host->currentImage();
    if (img.empty()) return;
    QImage qimg = ImageProcessor::matToQImage(img);
    if (qimg.isNull()) return;

    // source = sample, dst = scenePos (按 m_lastDstPos 算 offset)
    const QPointF offset = m_lastDstPos - scenePos;
    paintAt(qimg, scenePos, m_samplePoint + offset);

    cv::Mat newMat = ImageProcessor::qImageToMat(qimg);
    host->setCurrentImage(newMat);
}

void CloneTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_dragging || !m_hasSample || !host) return;

    // PS 同款: 拖动时连续 paint
    auto& img = host->currentImage();
    if (img.empty()) return;
    QImage qimg = ImageProcessor::matToQImage(img);
    if (qimg.isNull()) return;

    const QPointF offset = scenePos - m_lastDstPos;
    paintAt(qimg, scenePos, m_samplePoint + offset);

    cv::Mat newMat = ImageProcessor::qImageToMat(qimg);
    host->setCurrentImage(newMat);
    m_lastDstPos = scenePos;
}

void CloneTool::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& /*scenePos*/)
{
    m_dragging = false;
}

void CloneTool::onKeyPress(QKeyEvent* e, ImageWindow* /*host*/)
{
    if (e->key() == Qt::Key_Escape) {
        m_hasSample = false;
        LOG_DEBUG("[CloneTool] Escape: cleared sample");
    }
}

QWidget* CloneTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* sizeLabel = new QLabel(QStringLiteral("笔刷:"), page);
    layout->addWidget(sizeLabel);

    auto* sizeSpin = new QSpinBox(page);
    sizeSpin->setRange(2, 200);
    sizeSpin->setValue(m_brushSize);
    layout->addWidget(sizeSpin);

    auto* hint = new QLabel(QStringLiteral("(Alt + 单击取样)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addWidget(hint);

    layout->addStretch(1);

    QObject::connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     page, [this](int v) { m_brushSize = v; });

    LOG_DEBUG("[CloneTool] optionPage created");
    return page;
}

} // namespace tools