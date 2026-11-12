// SPDX-License-Identifier: MIT
//
// HistoryBrushTool implementation
//
//   See HistoryBrushTool.h for the why.
#include "HistoryBrushTool.h"

#include "../imagewindow.h"
#include "logger.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace tools {

void HistoryBrushTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    m_haveLastPaint = false;
    m_lastPaintCount = 0;
    LOG_DEBUG("[HistoryBrushTool] onEnter host={}", host ? "yes" : "null");
}

void HistoryBrushTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_dragging = false;
    m_host = nullptr;
}

bool HistoryBrushTool::captureCurrent(ImageWindow* host)
{
    if (!host) return false;
    if (!host->layerStack()) return false;

    // A FRESH composite, not the render cache: the cache is refreshed
    // asynchronously for images above 1 MP, so it can still hold the
    // PREVIOUS frame when the user asks to snapshot.
    const cv::Mat composite = host->layerStack()->render();
    if (composite.empty()) return false;

    m_source = composite.clone();
    return true;
}

int HistoryBrushTool::paintSource(cv::Mat& target, const cv::Mat& source,
                                  int cx, int cy, int radius, int strength)
{
    if (target.empty() || source.empty()) return 0;
    if (target.channels() != source.channels()) return 0;
    if (target.size() != source.size()) return 0;

    const int r = std::max(1, radius);
    const int x0 = std::max(0, cx - r);
    const int y0 = std::max(0, cy - r);
    const int x1 = std::min(target.cols, cx + r + 1);
    const int y1 = std::min(target.rows, cy + r + 1);
    if (x1 <= x0 || y1 <= y0) return 0;

    const int s = std::clamp(strength, 0, 100);
    if (s == 0) return 0;

    const int ch = target.channels();
    // The alpha channel is never painted (CP-07): the layer's own alpha
    // is the truth, so only the colour channels are blended.
    const int colourCh = (ch == 4) ? 3 : ch;
    const double factor = s / 100.0;

    int written = 0;
    for (int y = y0; y < y1; ++y) {
        uchar*       trow = target.ptr<uchar>(y);
        const uchar* srow = source.ptr<uchar>(y);
        for (int x = x0; x < x1; ++x) {
            // A soft round brush: the coverage falls off towards the rim.
            const double dx = x - cx;
            const double dy = y - cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > r) continue;
            const double coverage = (dist <= r - 1.0) ? 1.0 : (r - dist);
            if (coverage <= 0.0) continue;

            const double alpha = factor * coverage;
            for (int c = 0; c < colourCh; ++c) {
                const int tv = trow[x * ch + c];
                const int sv = srow[x * ch + c];
                const int v = static_cast<int>(std::lround(tv + (sv - tv) * alpha));
                trow[x * ch + c] = static_cast<uchar>(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            ++written;
        }
    }
    return written;
}

void HistoryBrushTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[HistoryBrushTool] no editable layer, ignore");
        return;
    }
    if (m_source.empty()) {
        LOG_WARN("[HistoryBrushTool] no source snapshot, take one first");
        return;
    }

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_dragging = true;
    m_haveLastPaint = false;
    m_lastPaintCount = 0;

    const int cx = static_cast<int>(std::lround(scenePos.x()));
    const int cy = static_cast<int>(std::lround(scenePos.y()));
    m_lastPaintCount += paintSource(*target, m_source, cx, cy, m_brushSize / 2, m_strength);
    m_lastPaintPos = scenePos;
    m_haveLastPaint = true;
    host->refreshCanvasNow();
}

void HistoryBrushTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_dragging || !host) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;

    // The brush paints along the path, spaced by a quarter of the brush
    // radius so the stroke has no gaps.
    const double step = std::max(1.0, m_brushSize / 4.0);
    const QPointF from = m_haveLastPaint ? m_lastPaintPos : scenePos;
    const double dx = scenePos.x() - from.x();
    const double dy = scenePos.y() - from.y();
    const double dist = std::sqrt(dx * dx + dy * dy);

    if (dist < step) return;

    const int n = static_cast<int>(dist / step);
    for (int i = 1; i <= n; ++i) {
        const double t = (step * i) / dist;
        const QPointF p(from.x() + dx * t, from.y() + dy * t);
        const int cx = static_cast<int>(std::lround(p.x()));
        const int cy = static_cast<int>(std::lround(p.y()));
        m_lastPaintCount += paintSource(*target, m_source, cx, cy, m_brushSize / 2, m_strength);
        m_lastPaintPos = p;
        m_haveLastPaint = true;
    }
    host->refreshCanvasNow();
}

void HistoryBrushTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_dragging) return;
    m_dragging = false;

    if (!host) { m_backup.release(); return; }

    cv::Mat* cur = host->activeLayerImage();
    if (cur && m_layerIndex >= 0 && !m_backup.empty()
        && cur->size() == m_backup.size() && cur->type() == m_backup.type()) {
        host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                 QStringLiteral("History Brush"));
    }
    m_backup.release();
    host->commitLayerEdit(m_layerIndex);
}

QWidget* HistoryBrushTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    auto* sizeRow = new QWidget(page);
    auto* sizeLay = new QHBoxLayout(sizeRow);
    sizeLay->setContentsMargins(0, 0, 0, 0);
    auto* sizeLbl = new QLabel(QString::number(m_brushSize), sizeRow);
    auto* sizeSlider = new QSlider(Qt::Horizontal, sizeRow);
    sizeSlider->setRange(1, 200);
    sizeSlider->setValue(m_brushSize);
    sizeLay->addWidget(sizeSlider, 1);
    sizeLay->addWidget(sizeLbl);
    layout->addRow(QCoreApplication::translate("tools::HistoryBrushTool", "Brush size:"), sizeRow);
    QObject::connect(sizeSlider, &QSlider::valueChanged, page,
                     [this, sizeLbl](int v) {
                         setBrushSize(v);
                         sizeLbl->setText(QString::number(v));
                     });

    auto* strengthRow = new QWidget(page);
    auto* strengthLay = new QHBoxLayout(strengthRow);
    strengthLay->setContentsMargins(0, 0, 0, 0);
    auto* strengthLbl = new QLabel(QString::number(m_strength), strengthRow);
    auto* strengthSlider = new QSlider(Qt::Horizontal, strengthRow);
    strengthSlider->setRange(0, 100);
    strengthSlider->setValue(m_strength);
    strengthLay->addWidget(strengthSlider, 1);
    strengthLay->addWidget(strengthLbl);
    layout->addRow(QCoreApplication::translate("tools::HistoryBrushTool", "Strength:"), strengthRow);
    QObject::connect(strengthSlider, &QSlider::valueChanged, page,
                     [this, strengthLbl](int v) {
                         setStrength(v);
                         strengthLbl->setText(QString::number(v));
                     });

    auto* capture = new QPushButton(QCoreApplication::translate("tools::HistoryBrushTool", "Snapshot current state"), page);
    layout->addRow(QString(), capture);
    QObject::connect(capture, &QPushButton::clicked, page,
                     [this](bool) { captureCurrent(m_host); });

    return page;
}

} // namespace tools
