// SPDX-License-Identifier: MIT
//
// RedEyeTool implementation - P0-9.3 (2026-09-15)
//
// 详见 RedEyeTool.h 头注释
//
#include "RedEyeTool.h"
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

// PS 同款 red eye detection: R 显著大于 G 和 B
//   阈值: R > 1.5 * G AND R > 1.5 * B (主流实现, Photoshop 同款)
bool RedEyeTool::isRedEyePixel(int r, int g, int b)
{
    return r > 80 &&                  // not too dark
           r > 1.5 * g &&
           r > 1.5 * b;
}

RedEyeTool::RedEyeTool(QWidget* /*parent*/) : ToolState() {}

void RedEyeTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    LOG_DEBUG("[RedEyeTool] onEnter host={}", host ? "yes" : "null");
}

void RedEyeTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_dragging = false;
    m_host = nullptr;
    LOG_DEBUG("[RedEyeTool] onExit");
}

void RedEyeTool::applyRedEyeAt(ImageWindow* host, const QPointF& a, const QPointF& b)
{
    auto& img = host->currentImage();
    if (img.empty()) return;
    QImage qimg = ImageProcessor::matToQImage(img);
    if (qimg.isNull()) return;

    const int x1 = static_cast<int>(a.x());
    const int y1 = static_cast<int>(a.y());
    const int x2 = static_cast<int>(b.x());
    const int y2 = static_cast<int>(b.y());
    const QPoint tl(x1, y1);
    const QPoint br(x2, y2);
    QRect rect(tl, br);
    rect = rect.normalized();
    int reduced = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QColor c = qimg.pixelColor(x, y);
            if (isRedEyePixel(c.red(), c.green(), c.blue())) {
                // PS 同款: new R = (G + B) / 2 (desaturate red)
                const int newR = (c.green() + c.blue()) / 2;
                qimg.setPixelColor(x, y, QColor(newR, c.green(), c.blue()));
                ++reduced;
            }
        }
    }
    if (reduced > 0) {
        cv::Mat newMat = ImageProcessor::qImageToMat(qimg);
        host->setCurrentImage(newMat);
        LOG_INFO("[RedEyeTool] reduced {} pixels in {}x{} region",
                 reduced, rect.width(), rect.height());
    }
}

void RedEyeTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host || e->button() != Qt::LeftButton) return;
    m_pressScenePos = scenePos;
    m_dragging = true;
}

void RedEyeTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& /*scenePos*/)
{
    // 不在 move 应用 (PS 同款 release 时应用)
}

void RedEyeTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_dragging || !m_host || !host) {
        m_dragging = false;
        return;
    }
    applyRedEyeAt(host, m_pressScenePos, scenePos);
    m_dragging = false;
}

QWidget* RedEyeTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* hint = new QLabel(QStringLiteral("(拖矩形框选红眼区域)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addWidget(hint);
    layout->addStretch(1);

    LOG_DEBUG("[RedEyeTool] optionPage created");
    return page;
}

} // namespace tools