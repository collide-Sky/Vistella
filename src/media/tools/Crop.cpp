// SPDX-License-Identifier: MIT
//
// Crop implementation - F-L (2026-09-10) + P2.3 (2026-09-22)
//
#include "Crop.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSpinBox>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace tools {

Crop::Crop(QWidget* /*parent*/) : ToolState() {}

QString Crop::pageTitle() const
{
    return QCoreApplication::translate("tools::Crop", "Crop Tool");
}

void Crop::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    m_hasCrop = false;
    LOG_DEBUG("[Crop] onEnter host={}", host ? "yes" : "null");
}

void Crop::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_dragging = false;
    m_hasCrop = false;
    m_host = nullptr;
    LOG_DEBUG("[Crop] onExit");
}

QRect Crop::computeClampedRect(int x1, int y1, int x2, int y2) const
{
    QPoint tl(std::min(x1, x2), std::min(y1, y2));
    QPoint br(std::max(x1, x2), std::max(y1, y2));
    if (m_host) {
        const cv::Mat& img = m_host->currentImage();
        if (img.empty()) return QRect();
        const int imgW = img.cols;
        const int imgH = img.rows;
        tl.setX(std::clamp(tl.x(), 0, imgW));
        tl.setY(std::clamp(tl.y(), 0, imgH));
        br.setX(std::clamp(br.x(), 0, imgW));
        br.setY(std::clamp(br.y(), 0, imgH));
    }
    return QRect(tl, br).normalized();
}

void Crop::applyCrop(ImageWindow* host)
{
    if (!host || !m_hasCrop) return;
    auto& img = host->currentImage();
    if (img.empty()) return;

    const QRect rect = computeClampedRect(
        static_cast<int>(m_cropA.x()), static_cast<int>(m_cropA.y()),
        static_cast<int>(m_cropB.x()), static_cast<int>(m_cropB.y()));
    if (rect.isEmpty() || rect.width() < 2 || rect.height() < 2) {
        LOG_WARN("[Crop] rect too small or empty, skipping");
        return;
    }

    // P2.3: snapshot before crop (MosaicTool pattern)
    cv::Mat backup = img.clone();

    // Crop ROI
    const cv::Rect cvRect(rect.x(), rect.y(), rect.width(), rect.height());
    cv::Mat cropped = img(cvRect).clone();

    // P2.3: push ImageEditCommand (single push per crop, easy undo)
    if (auto* stack = host->undoStack()) {
        stack->push(new ImageEditCommand(host, backup, cropped,
            QCoreApplication::translate("tools::Crop", "Crop")));
    }
    m_hasCrop = false;
    LOG_INFO("[Crop] cropped {}x{} region from {}x{} image",
             rect.width(), rect.height(), img.cols, img.rows);
}

void Crop::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_host || e->button() != Qt::LeftButton) return;
    m_cropA = scenePos;
    m_cropB = scenePos;
    m_hasCrop = true;
    m_dragging = true;
}

void Crop::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& scenePos)
{
    if (!m_dragging) return;
    m_cropB = scenePos;
}

void Crop::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& /*scenePos*/)
{
    m_dragging = false;
    if (!m_host) return;
    applyCrop(host);
}

QWidget* Crop::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // P2.3: Aspect Ratio combo box (Free / 1:1 / 4:3 / 16:9)
    auto* ratioCombo = new QComboBox(page);
    ratioCombo->addItem(QCoreApplication::translate("tools::Crop", "Free"),
                        static_cast<int>(AspectRatio::Free));
    ratioCombo->addItem(QCoreApplication::translate("tools::Crop", "1:1 (Square)"),
                        static_cast<int>(AspectRatio::Ratio1_1));
    ratioCombo->addItem(QCoreApplication::translate("tools::Crop", "4:3"),
                        static_cast<int>(AspectRatio::Ratio4_3));
    ratioCombo->addItem(QCoreApplication::translate("tools::Crop", "16:9"),
                        static_cast<int>(AspectRatio::Ratio16_9));
    ratioCombo->setCurrentIndex(static_cast<int>(m_aspect));
    QObject::connect(ratioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                     [this](int idx) { m_aspect = static_cast<AspectRatio>(idx); });
    layout->addRow(QCoreApplication::translate("tools::Crop", "Aspect:"), ratioCombo);

    auto* hint = new QLabel(QCoreApplication::translate("tools::Crop",
        "(drag rectangle on canvas to crop)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[Crop] optionPage created (P2.3 with aspect ratio)");
    return page;
}

} // namespace tools