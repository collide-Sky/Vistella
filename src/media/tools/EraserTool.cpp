// SPDX-License-Identifier: MIT
//
// EraserTool implementation
//
// See EraserTool.h for the why.
#include "EraserTool.h"
#include "ToolContext.h"
#include "ToolGeometry.h"
#include "ToolOptionsPanel.h"

#include "../imagewindow.h"
#include "logger.h"

#include <QMouseEvent>
#include <QObject>
#include <QRect>
#include <QWidget>

#include <algorithm>

namespace tools {

void EraserTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_strokeOpen = false;
    m_layerIndex = -1;
    m_backup.release();
    LOG_DEBUG("[EraserTool] onEnter host={}", host ? "yes" : "null");
}

void EraserTool::onExit(ImageWindow* host)
{
    // A stroke that is still open when the tool is switched away must
    // be committed, otherwise the pixels stay edited but the undo
    // history does not know about it.
    if (m_strokeOpen) commitStroke(host);
    m_strokeOpen = false;
    m_host = nullptr;
}

QRect EraserTool::brushRect(const QPointF& scenePos) const
{
    const int half = m_brushSize / 2;
    const int cx = static_cast<int>(std::lround(scenePos.x()));
    const int cy = static_cast<int>(std::lround(scenePos.y()));
    return QRect(cx - half, cy - half, m_brushSize, m_brushSize);
}

void EraserTool::eraseAt(ImageWindow* host, const QPointF& scenePos)
{
    if (!host) return;
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;

    // The scene is 1:1 with the image pixels, so the brush rectangle
    // maps straight onto the layer buffer.
    const QRect r = brushRect(scenePos).intersected(
        QRect(0, 0, target->cols, target->rows));
    if (r.isEmpty()) return;

    const cv::Rect roi(r.x(), r.y(), r.width(), r.height());

    if (target->channels() == 4) {
        // The layer has an alpha channel: erase means "make it
        // transparent".
        cv::Mat roiView = (*target)(roi);
        for (int y = 0; y < roiView.rows; ++y) {
            uchar* px = roiView.ptr<uchar>(y);
            for (int x = 0; x < roiView.cols; ++x) {
                px[x * 4 + 3] = 0;   // alpha = 0
            }
        }
    } else {
        // No alpha channel: Photoshop writes the background colour.
        // The document's background colour comes from the colour
        // state (the same one the swatches show).
        uchar b = 255, g = 255, r = 255;
        if (auto* cs = host->colorState()) {
            const QColor bg = cs->backgroundColor();
            b = static_cast<uchar>(bg.blue());
            g = static_cast<uchar>(bg.green());
            r = static_cast<uchar>(bg.red());
        }
        cv::Mat roiView = (*target)(roi);
        for (int y = 0; y < roiView.rows; ++y) {
            uchar* px = roiView.ptr<uchar>(y);
            for (int x = 0; x < roiView.cols; ++x) {
                px[x * 3 + 0] = b;
                px[x * 3 + 1] = g;
                px[x * 3 + 2] = r;
            }
        }
    }
}

void EraserTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[EraserTool] no editable layer, ignore");
        return;
    }

    // The brush size comes from the shared brush state, so the option
    // bar controls the eraser just like the brush.
    if (auto* ctx = host->toolContext()) {
        m_brushSize = std::max(1, ctx->brushSharedState().brushSize);
    }

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_strokeOpen = true;
    m_lastPos = scenePos;

    eraseAt(host, scenePos);
    host->refreshCanvasNow();
}

void EraserTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strokeOpen || !host) return;

    // Interpolate between the last position and the current one, so a
    // fast mouse movement still erases a continuous stroke.
    const double dx = scenePos.x() - m_lastPos.x();
    const double dy = scenePos.y() - m_lastPos.y();
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double step = std::max(1.0, double(m_brushSize) / 4.0);

    if (dist >= step) {
        const int steps = static_cast<int>(dist / step);
        for (int i = 1; i <= steps; ++i) {
            const double t = double(i) * step / dist;
            eraseAt(host, m_lastPos + QPointF(dx * t, dy * t));
        }
        m_lastPos = scenePos;
        host->refreshCanvasNow();
    }
}

void EraserTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_strokeOpen) return;
    commitStroke(host);
}

void EraserTool::commitStroke(ImageWindow* host)
{
    if (!m_strokeOpen) return;
    m_strokeOpen = false;

    if (host) {
        cv::Mat* cur = host->activeLayerImage();
        if (cur && !m_backup.empty() && cur->size() == m_backup.size()
            && cur->type() == m_backup.type()) {
            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Erase"));
        }
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    } else {
        m_backup.release();
    }
}

QWidget* EraserTool::optionPage(QWidget* parent)
{
    auto* panel = new ToolOptionsPanel(ToolOptionsPanel::Kind::Eraser, parent);
    panel->setBrushSize(m_brushSize);

    // The panel is the context object: when it dies the connection
    // dies with it, so the tool never sees a dangling lambda.
    QObject::connect(panel, &ToolOptionsPanel::brushSizeChanged, panel,
                     [this](int v) { this->setBrushSize(v); });
    return panel;
}

} // namespace tools
