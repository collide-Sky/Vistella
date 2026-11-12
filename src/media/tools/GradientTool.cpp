// SPDX-License-Identifier: MIT
//
// GradientTool implementation
//
// See GradientTool.h for the why.
#include "GradientTool.h"
#include "ToolGeometry.h"
#include "ToolOptionsPanel.h"

#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "logger.h"

#include <QColor>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace tools {

void GradientTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_dragging = false;
    m_layerIndex = -1;
    m_backup.release();
    LOG_DEBUG("[GradientTool] onEnter host={}", host ? "yes" : "null");
}

void GradientTool::onExit(ImageWindow* host)
{
    if (m_dragging) commitStroke(host);
    m_dragging = false;
    m_host = nullptr;
}

double GradientTool::gradientFactor(const QPointF& p, const QPointF& start,
                                    const QPointF& end, GradientType type) const
{
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();

    if (type == GradientType::Radial) {
        // The factor is the distance from the start relative to the
        // distance from the start to the end.
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) return 0.0;
        const double px = p.x() - start.x();
        const double py = p.y() - start.y();
        const double dist = std::sqrt(px * px + py * py);
        return std::clamp(dist / len, 0.0, 1.0);
    }

    if (type == GradientType::Angular) {
        // The factor is the angle around the start point, normalized by
        // a full turn.
        const double px = p.x() - start.x();
        const double py = p.y() - start.y();
        if (std::sqrt(px * px + py * py) < 1e-6) return 0.0;
        double angle = std::atan2(py, px);
        if (angle < 0.0) angle += 2.0 * M_PI;
        return std::clamp(angle / (2.0 * M_PI), 0.0, 1.0);
    }

    // Linear: the projection of (p - start) onto the drag direction.
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-6) return 0.0;
    const double t = ((p.x() - start.x()) * dx + (p.y() - start.y()) * dy) / len2;
    return std::clamp(t, 0.0, 1.0);
}

void GradientTool::applyGradient(ImageWindow* host)
{
    if (!host) return;
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;

    // The foreground colour at the drag start, the background colour at
    // the drag end (the Photoshop default).
    QColor fg(Qt::black), bg(Qt::white);
    if (auto* cs = host->colorState()) {
        fg = cs->foregroundColor();
        bg = cs->backgroundColor();
    }

    // The selection clips the fill, and that clipping happens ONCE --
    //   centrally, in ImageWindow::pushLayerImageEdit() -> clipEditToSelection().
    //
    //   It used to be done here as well, with a hard cut
    //   (maskRow[x] == 0 -> skip the pixel). That was wrong on a feathered
    //   edge: the manual cut left the pixel at its OLD colour, and the
    //   central clip then blended old -> old = old. The feathered edge of
    //   the gradient therefore faded to the pre-gradient colour instead of
    //   fading into the gradient.
    //
    //   Two clippers, two different rules -- the hard one won, and the
    //   feather was lost. Now there is exactly one clipper, and it is
    //   the one that knows about feathering.
    const int channels = target->channels();
    for (int y = 0; y < target->rows; ++y) {
        uchar* row = target->ptr<uchar>(y);
        for (int x = 0; x < target->cols; ++x) {
            const double t = gradientFactor(QPointF(x, y), m_start, m_end, m_type);

            const double b = fg.blue()   + (bg.blue()   - fg.blue())   * t;
            const double g = fg.green()  + (bg.green()  - fg.green())  * t;
            const double r = fg.red()    + (bg.red()    - fg.red())    * t;

            uchar* px = row + x * channels;
            px[0] = static_cast<uchar>(std::lround(b));
            px[1] = static_cast<uchar>(std::lround(g));
            px[2] = static_cast<uchar>(std::lround(r));
            if (channels == 4) {
                // The gradient is opaque; the layer's alpha is kept
                // for the pixels that were already transparent.
                px[3] = 255;
            }
        }
    }
}

void GradientTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[GradientTool] no editable layer, ignore");
        return;
    }

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_start = ToolGeometry::clampToCanvas(host, scenePos);
    m_end = m_start;
    m_dragging = true;
}

void GradientTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_dragging || !host) return;

    m_end = ToolGeometry::clampToCanvas(host, scenePos);
    applyGradient(host);
    host->refreshCanvasNow();
}

void GradientTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_dragging) return;

    // A drag that did not move leaves a single-point gradient -- that
    // is still a valid gradient (it fills with the foreground colour).
    applyGradient(host);
    commitStroke(host);
}

void GradientTool::commitStroke(ImageWindow* host)
{
    if (!m_dragging) return;
    m_dragging = false;

    if (host) {
        cv::Mat* cur = host->activeLayerImage();
        if (cur && !m_backup.empty() && cur->size() == m_backup.size()
            && cur->type() == m_backup.type()) {
            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Gradient"));
        }
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    } else {
        m_backup.release();
    }
}

QWidget* GradientTool::optionPage(QWidget* parent)
{
    auto* panel = new ToolOptionsPanel(ToolOptionsPanel::Kind::Gradient, parent);
    panel->setGradientType(static_cast<int>(m_type));

    QObject::connect(panel, &ToolOptionsPanel::gradientTypeChanged, panel,
                     [this](int index) {
        switch (index) {
        case 1:  this->setType(GradientType::Radial); break;
        case 2:  this->setType(GradientType::Angular); break;
        default: this->setType(GradientType::Linear); break;
        }
    });
    return panel;
}

} // namespace tools
