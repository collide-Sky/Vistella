// SPDX-License-Identifier: MIT
//
// DodgeBurnTool implementation
//
// See DodgeBurnTool.h for the why.
#include "DodgeBurnTool.h"
#include "ToolContext.h"
#include "ToolOptionsPanel.h"

#include "../imagewindow.h"
#include "logger.h"

#include <QMouseEvent>
#include <QObject>
#include <QRect>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace tools {

void DodgeBurnTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_strokeOpen = false;
    m_layerIndex = -1;
    m_backup.release();
    LOG_DEBUG("[DodgeBurnTool] onEnter host={}", host ? "yes" : "null");
}

void DodgeBurnTool::onExit(ImageWindow* host)
{
    if (m_strokeOpen) commitStroke(host);
    m_strokeOpen = false;
    m_host = nullptr;
}

uchar DodgeBurnTool::dodgeChannel(uchar value, int amountPct)
{
    // The Photoshop exposure approximation.
    //
    //   The pixel is pushed away from the mid grey (128). The bigger
    //   the amount, the bigger the push. A pixel that is already at
    //   the limit (255) cannot get lighter and stays there.
    const double amount = std::clamp(amountPct, 1, 100) / 100.0;
    const double v = double(value);

    // The push grows with the distance from the mid grey, so the
    // darker a pixel is the more a dodge brightens it.
    double result = v + (255.0 - v) * amount * (1.0 - v / 255.0) * 2.0;
    result = std::clamp(result, 0.0, 255.0);
    return static_cast<uchar>(std::lround(result));
}

uchar DodgeBurnTool::burnChannel(uchar value, int amountPct)
{
    const double amount = std::clamp(amountPct, 1, 100) / 100.0;
    const double v = double(value);

    // The burn pushes the pixel towards black, with the same
    // "the further from the mid grey the stronger" rule.
    double result = v - v * amount * (v / 255.0) * 2.0;
    result = std::clamp(result, 0.0, 255.0);
    return static_cast<uchar>(std::lround(result));
}

void DodgeBurnTool::applyAt(ImageWindow* host, const QPointF& scenePos)
{
    if (!host) return;
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;

    const int half = m_brushSize / 2;
    const int cx = static_cast<int>(std::lround(scenePos.x()));
    const int cy = static_cast<int>(std::lround(scenePos.y()));
    const QRect r = QRect(cx - half, cy - half, m_brushSize, m_brushSize)
                        .intersected(QRect(0, 0, target->cols, target->rows));
    if (r.isEmpty()) return;

    const cv::Rect roi(r.x(), r.y(), r.width(), r.height());
    cv::Mat view = (*target)(roi);
    const int channels = view.channels();

    for (int y = 0; y < view.rows; ++y) {
        uchar* row = view.ptr<uchar>(y);
        for (int x = 0; x < view.cols; ++x) {
            uchar* p = row + x * channels;
            for (int c = 0; c < 3 && c < channels; ++c) {
                p[c] = (m_mode == Mode::Dodge) ? dodgeChannel(p[c], m_amount)
                                               : burnChannel(p[c], m_amount);
            }
        }
    }
}

void DodgeBurnTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[DodgeBurnTool] no editable layer, ignore");
        return;
    }

    // The brush size comes from the shared brush state so the option
    // bar controls it, like the brush and the eraser.
    if (auto* ctx = host->toolContext()) {
        m_brushSize = std::max(1, ctx->brushSharedState().brushSize);
    }

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_strokeOpen = true;
    m_lastPos = scenePos;

    applyAt(host, scenePos);
    host->refreshCanvasNow();
}

void DodgeBurnTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strokeOpen || !host) return;

    const double dx = scenePos.x() - m_lastPos.x();
    const double dy = scenePos.y() - m_lastPos.y();
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double step = std::max(1.0, double(m_brushSize) / 4.0);

    if (dist >= step) {
        const int steps = static_cast<int>(dist / step);
        for (int i = 1; i <= steps; ++i) {
            const double t = double(i) * step / dist;
            applyAt(host, m_lastPos + QPointF(dx * t, dy * t));
        }
        m_lastPos = scenePos;
        host->refreshCanvasNow();
    }
}

void DodgeBurnTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_strokeOpen) return;
    commitStroke(host);
}

void DodgeBurnTool::commitStroke(ImageWindow* host)
{
    if (!m_strokeOpen) return;
    m_strokeOpen = false;

    if (host) {
        cv::Mat* cur = host->activeLayerImage();
        if (cur && !m_backup.empty() && cur->size() == m_backup.size()
            && cur->type() == m_backup.type()) {
            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Dodge / Burn"));
        }
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    } else {
        m_backup.release();
    }
}

QWidget* DodgeBurnTool::optionPage(QWidget* parent)
{
    auto* panel = new ToolOptionsPanel(ToolOptionsPanel::Kind::DodgeBurn, parent);
    panel->setMode(static_cast<int>(m_mode));
    panel->setAmount(m_amount);

    QObject::connect(panel, &ToolOptionsPanel::modeChanged, panel,
                     [this](int index) {
        this->setMode(index == 1 ? Mode::Burn : Mode::Dodge);
    });
    QObject::connect(panel, &ToolOptionsPanel::amountChanged, panel,
                     [this](int v) { this->setAmount(v); });
    return panel;
}

} // namespace tools
