#include "MaskBrushTool.h"

#include "MaskBrushCommand.h"
#include "MaskOptionsPanel.h"
#include "imagewindow.h"
#include "imageworker/layers/Layer.h"
#include "imageworker/layers/LayerStack.h"
#include "logger.h"

#include <QCursor>
#include <QMouseEvent>
#include <cmath>

namespace tools {

MaskBrushTool::MaskBrushTool() = default;

void MaskBrushTool::onEnter(ImageWindow* host) {
    Q_UNUSED(host);
    m_painting = false;
}

void MaskBrushTool::onExit(ImageWindow* host) {
    Q_UNUSED(host);
    m_painting = false;
}

QString MaskBrushTool::pageTitle() const {
    return QStringLiteral("Mask Brush");
}

QCursor MaskBrushTool::cursor() const {
    return Qt::CrossCursor;
}

QWidget* MaskBrushTool::optionPage(QWidget* parent) {
    if (!m_optionPage) {
        m_optionPage = new MaskOptionsPanel(this, parent);
    }
    return m_optionPage;
}

qreal MaskBrushTool::stampFalloff(qreal distance, qreal radius, qreal hardness) {
    if (radius <= 0.0) return 0.0;
    if (distance >= radius) return 0.0;
    // Map hardness 0..1 to inner-radius fraction. hardness=1 -> flat 1.0
    // inside the brush; hardness=0 -> linear ramp from 0 at center to 1 at
    // edge (smoother). Interpolate with a power curve for nicer PS feel.
    const qreal t = qBound(qreal(0.0), distance / radius, qreal(1.0));
    // Base shape: ease from 1 at center to 0 at edge; hardness controls
    // how sharply it falls off.
    const qreal power = 1.0 + 4.0 * (1.0 - hardness);  // 1..5
    return std::pow(qreal(1.0) - t, power);
}

void MaskBrushTool::paintStamp(const QPointF& scenePos, ImageWindow* host) {
    if (!host) return;
    auto* stack = host->layerStack();
    if (!stack) return;
    auto l = stack->at(m_layerIdx);
    if (!l) return;

    // Promote the layer's mask to a pixel mask if not yet.
    if (l->mask.kind != layers::LayerMask::Pixel) {
        l->mask.kind = layers::LayerMask::Pixel;
    }
    if (l->mask.pixel.empty() || l->mask.pixel.size() != l->image.size()) {
        l->mask.pixel = cv::Mat(l->image.rows, l->image.cols,
                                CV_8UC1, cv::Scalar(0));
    }

    const qreal radius = m_brushSize * 0.5;
    const int r = int(std::ceil(radius));
    const int cx = int(scenePos.x());
    const int cy = int(scenePos.y());

    const uchar target = (m_mode == Reveal) ? uchar(255) : uchar(0);

    cv::Mat& mask = l->mask.pixel;
    const int w = mask.cols;
    const int h = mask.rows;
    const int x0 = std::max(0, cx - r);
    const int x1 = std::min(w - 1, cx + r);
    const int y0 = std::max(0, cy - r);
    const int y1 = std::min(h - 1, cy + r);

    for (int y = y0; y <= y1; ++y) {
        uchar* row = mask.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const qreal d = std::hypot(qreal(x - cx), qreal(y - cy));
            if (d > radius) continue;
            const qreal fall = stampFalloff(d, radius, m_hardness);
            const qreal blend = fall * m_opacity;
            const qreal old = qreal(row[x]);
            const qreal newVal = old + (qreal(target) - old) * blend;
            row[x] = uchar(qBound(0.0, newVal, 255.0));
        }
    }
    emit stack->layerChanged(m_layerIdx);
}

void MaskBrushTool::onMousePress(QMouseEvent* e, ImageWindow* host,
                                 const QPointF& scenePos) {
    if (!host || e->button() != Qt::LeftButton) return;
    auto* stack = host->layerStack();
    if (!stack) return;
    m_layerIdx = stack->selection();
    if (m_layerIdx < 0) return;

    auto l = stack->at(m_layerIdx);
    if (!l) return;

    // Snapshot the mask BEFORE the stroke so undo can restore.
    if (l->mask.kind == layers::LayerMask::Pixel && !l->mask.pixel.empty()) {
        m_strokeBefore = l->mask.pixel.clone();
    } else {
        m_strokeBefore = cv::Mat();
    }

    m_painting = true;
    m_lastPos = scenePos;
    paintStamp(scenePos, host);
}

void MaskBrushTool::onMouseMove(QMouseEvent* e, ImageWindow* host,
                                const QPointF& scenePos) {
    Q_UNUSED(e);
    if (!m_painting || !host) return;
    // Interpolate stamps along the path so dense dabs don't leave gaps.
    const QPointF delta = scenePos - m_lastPos;
    const qreal step = qreal(m_brushSize) * 0.25;
    const qreal len = std::hypot(delta.x(), delta.y());
    if (len <= step) {
        paintStamp(scenePos, host);
    } else {
        const int n = int(std::ceil(len / step));
        for (int i = 1; i <= n; ++i) {
            const qreal t = qreal(i) / qreal(n);
            const QPointF p = m_lastPos + delta * t;
            paintStamp(p, host);
        }
    }
    m_lastPos = scenePos;
}

void MaskBrushTool::onMouseRelease(QMouseEvent* e, ImageWindow* host,
                                   const QPointF& scenePos) {
    Q_UNUSED(scenePos);
    if (e->button() != Qt::LeftButton) return;
    if (!m_painting || !host || m_layerIdx < 0) return;

    auto* stack = host->layerStack();
    if (!stack) return;
    auto l = stack->at(m_layerIdx);
    if (!l) return;

    cv::Mat after = (l->mask.kind == layers::LayerMask::Pixel)
                    ? l->mask.pixel.clone() : cv::Mat();

    if (!after.empty()) {
        auto* cmd = new MaskBrushCommand(host, m_layerIdx,
                                         std::move(m_strokeBefore),
                                         std::move(after));
        host->undoStack()->push(cmd);
    }

    m_painting = false;
    m_strokeBefore.release();
    m_layerIdx = -1;
}

}  // namespace tools