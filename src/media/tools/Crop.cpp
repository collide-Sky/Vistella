// SPDX-License-Identifier: MIT
//
// Crop implementation
//
// See Crop.h for the header comment.
#include "Crop.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "ToolGeometry.h"
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

#include <cmath>

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

double Crop::aspectValue() const
{
    switch (m_aspect) {
    case AspectRatio::Ratio1_1:  return 1.0;
    case AspectRatio::Ratio4_3:  return 4.0 / 3.0;
    case AspectRatio::Ratio16_9: return 16.0 / 9.0;
    case AspectRatio::Free:
    default:                     return 0.0;
    }
}

QPointF Crop::applyAspect(const QPointF& anchor, const QPointF& current) const
{
    const double ratio = aspectValue();
    if (ratio <= 0.0) return current;

    // The drag started at `anchor` and the pointer is at `current`. The
    // rectangle spans anchor..current, so its size is the absolute
    // difference in each axis.
    const double w = std::abs(current.x() - anchor.x());
    const double h = std::abs(current.y() - anchor.y());

    // Which axis is "free" is decided by which one the user moved
    // further: the other one is then derived from the ratio. When the
    // user has not moved at all, the width wins (the common case of a
    // horizontal drag).
    QPointF out = current;
    if (w >= h * ratio) {
        // The width leads: the height follows from the ratio.
        const double newH = w / ratio;
        out.setY(anchor.y() + (current.y() >= anchor.y() ? newH : -newH));
    } else {
        // The height leads: the width follows from the ratio.
        const double newW = h * ratio;
        out.setX(anchor.x() + (current.x() >= anchor.x() ? newW : -newW));
    }
    return out;
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
    const cv::Mat& img = host->currentImage();
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

    //  裁剪改的是整张文档 (几何变了) -> 文档级写入
    //  ImageEditCommand 的 undo 会把文档恢复成 backup
    host->setDocumentImage(cropped);

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
    // M2-4: the host that comes with the event is the one that counts.
    //   The old code only looked at m_host (cached by onEnter), so a tool
    //   instance that never went through onEnter dropped every mouse
    //   event and the crop did nothing at all.
    if (!host || e->button() != Qt::LeftButton) return;

    const QPointF clamped = ToolGeometry::clampToCanvas(host, scenePos);
    m_cropA = clamped;
    m_cropB = clamped;
    m_hasCrop = true;
    m_dragging = true;
}

void Crop::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_dragging || !host) return;

    const QPointF clamped = ToolGeometry::clampToCanvas(host, scenePos);
    // M2-4: the aspect lock is applied to the DRAG, not just stored.
    m_cropB = applyAspect(m_cropA, clamped);
}

void Crop::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& /*scenePos*/)
{
    m_dragging = false;
    if (!host) return;
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
    {
        // The current value is looked up by DATA, not by index: the
        //   index is the position in the list, the data is the enum.
        int idx = ratioCombo->findData(static_cast<int>(m_aspect));
        if (idx < 0) idx = 0;
        ratioCombo->setCurrentIndex(idx);
    }
    QObject::connect(ratioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                     [this, ratioCombo](int) {
        m_aspect = static_cast<AspectRatio>(ratioCombo->currentData().toInt());
    });
    layout->addRow(QCoreApplication::translate("tools::Crop", "Aspect:"), ratioCombo);

    auto* hint = new QLabel(QCoreApplication::translate("tools::Crop",
        "(drag rectangle on canvas to crop)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[Crop] optionPage created (P2.3 with aspect ratio)");
    return page;
}

} // namespace tools
