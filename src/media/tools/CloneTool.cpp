// SPDX-License-Identifier: MIT
//
// CloneTool implementation - P0-9.3 (2026-09-15) + P2.2 (2026-09-22)
//
#include "CloneTool.h"
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
#include <opencv2/imgcodecs.hpp>

namespace tools {

CloneTool::CloneTool(QWidget* /*parent*/) : ToolState() {}

QString CloneTool::pageTitle() const
{
    return QCoreApplication::translate(translateContext().toUtf8().constData(),
                                       translateTitle().toUtf8().constData());
}

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
    hideBrushCursor(host);
    m_hasSample = false;
    m_dragging = false;
    m_host = nullptr;
    LOG_DEBUG("[CloneTool] onExit");
}

void CloneTool::showBrushCursor(ImageWindow* host, const QPointF& pos)
{
    if (!host) return;
    auto* cur = host->brushCursor();
    if (!cur) return;
    // P2.2: QGraphicsEllipseItem uses setRect(x, y, w, h); centered at (-r,-r)
    // so the cursor circle is centered on the cursor position
    const int r = m_brushSize;
    cur->setRect(-r, -r, r * 2, r * 2);
    cur->setPos(pos);
    cur->setVisible(true);
}

void CloneTool::hideBrushCursor(ImageWindow* host)
{
    if (!host) return;
    if (auto* cur = host->brushCursor()) cur->setVisible(false);
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

void CloneTool::commitStroke(const QString& text)
{
    if (!m_strokeOpen || !m_host || m_backup.empty()) {
        m_backup.release();
        m_strokeOpen = false;
        return;
    }
    m_strokeOpen = false;
    cv::Mat current = m_host->currentImage().clone();
    if (auto* stack = m_host->undoStack()) {
        stack->push(new ImageEditCommand(m_host, m_backup, current, text));
    }
    m_backup.release();
}

void CloneTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;

    // Alt + 单击 = 取样 (no stroke opened)
    if (e->modifiers() & Qt::AltModifier && e->button() == Qt::LeftButton) {
        m_samplePoint = scenePos;
        m_hasSample = true;
        showBrushCursor(host, scenePos);
        LOG_DEBUG("[CloneTool] sampled at ({}, {})", scenePos.x(), scenePos.y());
        return;
    }

    if (e->button() != Qt::LeftButton) return;
    if (!m_hasSample) {
        LOG_WARN("[CloneTool] no sample, Alt+click first to set source");
        return;
    }

    // P2.2: snapshot for undo before first paint of the stroke
    m_backup = host->currentImage().clone();
    m_strokeOpen = true;

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
    showBrushCursor(host, scenePos);
}

void CloneTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    showBrushCursor(host, scenePos);
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

void CloneTool::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& /*scenePos*/)
{
    m_dragging = false;
    commitStroke(QCoreApplication::translate("tools::CloneTool", "Clone Stamp"));
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
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // P2.2: brush size slider + value label (P2.1 MagicWand-style consistency)
    auto* szRow  = new QWidget(page);
    auto* szLay  = new QHBoxLayout(szRow);
    szLay->setContentsMargins(0, 0, 0, 0);
    auto* szLbl  = new QLabel(QString::number(m_brushSize), szRow);
    auto* slider = new QSlider(Qt::Horizontal, szRow);
    slider->setRange(2, 200);
    slider->setValue(m_brushSize);
    slider->setTickInterval(20);
    slider->setTickPosition(QSlider::TicksBelow);
    szLay->addWidget(slider, 1);
    szLay->addWidget(szLbl);
    layout->addRow(QCoreApplication::translate("tools::CloneTool", "Brush Size:"), szRow);
    QObject::connect(slider, &QSlider::valueChanged, page,
                     [this, szLbl](int v) {
                         setBrushSize(v);
                         szLbl->setText(QString::number(v));
                     });

    auto* hint = new QLabel(QCoreApplication::translate("tools::CloneTool",
        "(Alt + click to sample source)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[CloneTool] optionPage created (P2.2)");
    return page;
}

} // namespace tools