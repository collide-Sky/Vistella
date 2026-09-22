// SPDX-License-Identifier: MIT
//
// RedEyeTool implementation - P0-9.3 (2026-09-15) + P2.2 (2026-09-22)
//
#include "RedEyeTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QWidget>
#include <opencv2/core.hpp>

namespace tools {

// PS-style red eye detection: R is significantly larger than G and B.
//   Thresholds: R > 80 (not too dark) AND R > 1.5*G AND R > 1.5*B
//   (Photoshop-same mainstream heuristic)
bool RedEyeTool::isRedEyePixel(int r, int g, int b)
{
    return r > 80 &&                  // not too dark
           r > 1.5 * g &&
           r > 1.5 * b;
}

RedEyeTool::RedEyeTool(QWidget* /*parent*/) : ToolState() {}

QString RedEyeTool::pageTitle() const
{
    return QCoreApplication::translate("tools::RedEyeTool", "Red Eye Tool");
}

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
    // P2.2: darken amount controls how aggressive the desaturation is
    //   0   = leave pixel as-is (no-op for this pixel)
    //   100 = full PS desaturate (R = (G+B)/2)
    const int darkenClamped = std::clamp(m_darken, 0, 100);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QColor c = qimg.pixelColor(x, y);
            if (isRedEyePixel(c.red(), c.green(), c.blue())) {
                // P2.2: partial darken support — new R lerps from original R to (G+B)/2
                const int targetR = (c.green() + c.blue()) / 2;
                const int newR = c.red() + (targetR - c.red()) * darkenClamped / 100;
                qimg.setPixelColor(x, y, QColor(newR, c.green(), c.blue()));
                ++reduced;
            }
        }
    }
    if (reduced > 0) {
        // P2.2: snapshot before applying (MosaicTool pattern)
        m_backup = img.clone();
        cv::Mat newMat = ImageProcessor::qImageToMat(qimg);
        host->setCurrentImage(newMat);
        // P2.2: commit single ImageEditCommand per stroke
        if (auto* stack = host->undoStack()) {
            stack->push(new ImageEditCommand(host, m_backup, newMat,
                QCoreApplication::translate("tools::RedEyeTool", "Red Eye Removal")));
        }
        m_backup.release();
        LOG_INFO("[RedEyeTool] reduced {} pixels in {}x{} region (darken={}%)",
                 reduced, rect.width(), rect.height(), darkenClamped);
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
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // P2.2: Pupil Size slider (1..100, default 30)
    auto* szRow = new QWidget(page);
    auto* szLay = new QHBoxLayout(szRow);
    szLay->setContentsMargins(0, 0, 0, 0);
    auto* szLbl = new QLabel(QString::number(m_pupilSize), szRow);
    auto* szSlider = new QSlider(Qt::Horizontal, szRow);
    szSlider->setRange(1, 100);
    szSlider->setValue(m_pupilSize);
    szSlider->setTickInterval(10);
    szSlider->setTickPosition(QSlider::TicksBelow);
    szLay->addWidget(szSlider, 1);
    szLay->addWidget(szLbl);
    layout->addRow(QCoreApplication::translate("tools::RedEyeTool", "Pupil Size:"), szRow);
    QObject::connect(szSlider, &QSlider::valueChanged, page,
                     [this, szLbl](int v) {
                         setPupilSize(v);
                         szLbl->setText(QString::number(v));
                     });

    // P2.2: Darken slider (0..100, default 50)
    auto* dkRow = new QWidget(page);
    auto* dkLay = new QHBoxLayout(dkRow);
    dkLay->setContentsMargins(0, 0, 0, 0);
    auto* dkLbl = new QLabel(QString::number(m_darken), dkRow);
    auto* dkSlider = new QSlider(Qt::Horizontal, dkRow);
    dkSlider->setRange(0, 100);
    dkSlider->setValue(m_darken);
    dkSlider->setTickInterval(10);
    dkSlider->setTickPosition(QSlider::TicksBelow);
    dkLay->addWidget(dkSlider, 1);
    dkLay->addWidget(dkLbl);
    layout->addRow(QCoreApplication::translate("tools::RedEyeTool", "Darken:"), dkRow);
    QObject::connect(dkSlider, &QSlider::valueChanged, page,
                     [this, dkLbl](int v) {
                         setDarken(v);
                         dkLbl->setText(QString::number(v));
                     });

    auto* hint = new QLabel(QCoreApplication::translate("tools::RedEyeTool",
        "(drag rectangle around the red-eye area)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[RedEyeTool] optionPage created (P2.2 with pupil size + darken)");
    return page;
}

} // namespace tools