// SPDX-License-Identifier: MIT
//
// PatternStampTool implementation
//
//   See PatternStampTool.h for the why.
#include "PatternStampTool.h"

#include "../imagewindow.h"
#include "logger.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QSlider>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace tools {

void PatternStampTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    m_haveLastStamp = false;
    m_lastStampCount = 0;
    LOG_DEBUG("[PatternStampTool] onEnter host={}", host ? "yes" : "null");
}

void PatternStampTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_dragging = false;
    m_host = nullptr;
}

cv::Mat PatternStampTool::capturePatch(const cv::Mat& layer, const QPointF& center, int patchSize)
{
    if (layer.empty()) return cv::Mat();
    const int size = std::clamp(patchSize, 4, 512);
    const int half = size / 2;

    const int cx = static_cast<int>(std::lround(center.x()));
    const int cy = static_cast<int>(std::lround(center.y()));

    const cv::Rect imgRect(0, 0, layer.cols, layer.rows);
    const cv::Rect want(cx - half, cy - half, size, size);
    const cv::Rect clipped = want & imgRect;
    if (clipped.empty()) return cv::Mat();

    // The patch is copied out of the layer, so later stamps tile from a
    // snapshot and not from the pixels the tool is overwriting.
    return layer(clipped).clone();
}

void PatternStampTool::stampAt(ImageWindow* host, const QPointF& scenePos)
{
    if (!host) return;
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;
    if (m_sourcePatch.empty()) return;

    const int channels = target->channels();
    const int patchChannels = m_sourcePatch.channels();
    if (patchChannels != channels) return;

    const int ph = m_sourcePatch.rows;
    const int pw = m_sourcePatch.cols;

    // The stamp position is the top-left of the patch. The source was
    // captured centred on the alt-click, so the stamp is placed centred
    // on the current position too.
    const int half = m_patchSize / 2;
    const int dx = static_cast<int>(std::lround(scenePos.x())) - half;
    const int dy = static_cast<int>(std::lround(scenePos.y())) - half;

    const cv::Rect imgRect(0, 0, target->cols, target->rows);
    const cv::Rect dstRect(dx, dy, pw, ph);
    const cv::Rect clipped = dstRect & imgRect;
    if (clipped.empty()) return;

    // The patch may be larger than the part that fits on the canvas;
    // copy the overlapping part only.
    const cv::Rect srcRect(clipped.x - dx, clipped.y - dy, clipped.width, clipped.height);

    m_sourcePatch(srcRect).copyTo((*target)(clipped));
    ++m_lastStampCount;
}

void PatternStampTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;

    cv::Mat* target = host->activeLayerImage();

    // Alt + click captures the source patch. No stroke is opened.
    if ((e->modifiers() & Qt::AltModifier) && e->button() == Qt::LeftButton) {
        if (!target || target->empty()) return;
        const cv::Mat patch = capturePatch(*target, scenePos, m_patchSize);
        if (patch.empty()) {
            LOG_WARN("[PatternStampTool] sample outside the canvas");
            return;
        }
        m_sourcePatch = patch;
        m_sourcePoint = scenePos;
        m_hasSource = true;
        LOG_DEBUG("[PatternStampTool] sampled {}x{} at ({}, {})",
                  patch.cols, patch.rows, scenePos.x(), scenePos.y());
        return;
    }

    if (e->button() != Qt::LeftButton) return;
    if (!m_hasSource) {
        LOG_WARN("[PatternStampTool] no source, Alt+click first to set the pattern");
        return;
    }
    if (!target || target->empty()) return;

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_dragging = true;
    m_haveLastStamp = false;
    m_lastStampCount = 0;

    stampAt(host, scenePos);
    m_lastStampPos = scenePos;
    m_haveLastStamp = true;
    host->refreshCanvasNow();
}

void PatternStampTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_dragging || !host) return;

    // The stamps are spaced by `spacing` percent of the patch size. The
    // walk interpolates between the last stamp and the current position
    // so that a fast mouse does not leave gaps in the pattern.
    const double step = std::max(1.0, m_patchSize * m_spacing / 100.0);
    const QPointF from = m_haveLastStamp ? m_lastStampPos : scenePos;
    const QPointF to = scenePos;

    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double dist = std::sqrt(dx * dx + dy * dy);

    if (dist < step) {
        // Still inside the spacing -- nothing to stamp yet.
        return;
    }

    const int n = static_cast<int>(dist / step);
    for (int i = 1; i <= n; ++i) {
        const double t = (step * i) / dist;
        const QPointF p(from.x() + dx * t, from.y() + dy * t);
        stampAt(host, p);
        m_lastStampPos = p;
        m_haveLastStamp = true;
    }
    host->refreshCanvasNow();
}

void PatternStampTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_dragging) return;
    m_dragging = false;

    if (!host) { m_backup.release(); return; }

    // Push the whole stroke as ONE undo command.
    cv::Mat* cur = host->activeLayerImage();
    if (cur && m_layerIndex >= 0 && !m_backup.empty()
        && cur->size() == m_backup.size() && cur->type() == m_backup.type()) {
        host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                 QStringLiteral("Pattern Stamp"));
    }
    m_backup.release();
    host->commitLayerEdit(m_layerIndex);
}

QWidget* PatternStampTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    auto* sizeRow = new QWidget(page);
    auto* sizeLay = new QHBoxLayout(sizeRow);
    sizeLay->setContentsMargins(0, 0, 0, 0);
    auto* sizeLbl = new QLabel(QString::number(m_patchSize), sizeRow);
    auto* sizeSlider = new QSlider(Qt::Horizontal, sizeRow);
    sizeSlider->setRange(4, 128);
    sizeSlider->setValue(m_patchSize);
    sizeLay->addWidget(sizeSlider, 1);
    sizeLay->addWidget(sizeLbl);
    layout->addRow(QCoreApplication::translate("tools::PatternStampTool", "Pattern size:"), sizeRow);
    QObject::connect(sizeSlider, &QSlider::valueChanged, page,
                     [this, sizeLbl](int v) {
                         setPatchSize(v);
                         sizeLbl->setText(QString::number(v));
                     });

    auto* spacingRow = new QWidget(page);
    auto* spacingLay = new QHBoxLayout(spacingRow);
    spacingLay->setContentsMargins(0, 0, 0, 0);
    auto* spacingLbl = new QLabel(QString::number(m_spacing), spacingRow);
    auto* spacingSlider = new QSlider(Qt::Horizontal, spacingRow);
    spacingSlider->setRange(5, 200);
    spacingSlider->setValue(m_spacing);
    spacingLay->addWidget(spacingSlider, 1);
    spacingLay->addWidget(spacingLbl);
    layout->addRow(QCoreApplication::translate("tools::PatternStampTool", "Spacing:"), spacingRow);
    QObject::connect(spacingSlider, &QSlider::valueChanged, page,
                     [this, spacingLbl](int v) {
                         setSpacing(v);
                         spacingLbl->setText(QString::number(v));
                     });

    auto* hint = new QLabel(QCoreApplication::translate("tools::PatternStampTool", "(Alt + click to capture the pattern)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    return page;
}

} // namespace tools
