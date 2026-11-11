// SPDX-License-Identifier: MIT
//
// PaintBucketTool implementation
//
// See PaintBucketTool.h for the why.
#include "PaintBucketTool.h"
#include "ToolGeometry.h"
#include "ToolOptionsPanel.h"

#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "logger.h"

#include <QColor>
#include <QMouseEvent>
#include <QObject>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tools {

void PaintBucketTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_lastFilledCount = 0;
    m_layerIndex = -1;
    m_backup.release();
    LOG_DEBUG("[PaintBucketTool] onEnter host={}", host ? "yes" : "null");
}

void PaintBucketTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_backup.release();
    m_host = nullptr;
}

cv::Mat PaintBucketTool::findRegion(const cv::Mat& layer, int x, int y, int tolerance)
{
    cv::Mat region;
    if (layer.empty()) return region;

    const int w = layer.cols;
    const int h = layer.rows;
    if (x < 0 || y < 0 || x >= w || y >= h) return region;

    const int channels = layer.channels();

    // The seed colour. For a 4-channel layer the alpha is NOT part of
    // the comparison: a transparent pixel and an opaque pixel of the
    // same colour are the same region (Photoshop compares the colour
    // channels only).
    auto colourAt = [&](int px, int py, int c) -> int {
        const uchar* p = layer.ptr<uchar>(py) + px * channels;
        return p[c];
    };

    const int seedB = colourAt(x, y, 0);
    const int seedG = colourAt(x, y, 1);
    const int seedR = colourAt(x, y, 2);

    region = cv::Mat(h, w, CV_8UC1, cv::Scalar(0));

    // Scanline flood fill.
    //
    //   A 4-connected stack-based fill: the pixel is added to the
    //   region when every colour channel is within the tolerance of
    //   the seed. The selection is applied as a mask afterwards, not
    //   during the fill -- the region is a property of the pixels, the
    //   selection decides what the user is allowed to change.
    std::vector<char> visited(static_cast<size_t>(w) * h, 0);
    std::vector<int> stack;
    stack.push_back(y * w + x);
    visited[static_cast<size_t>(y) * w + x] = 1;

    while (!stack.empty()) {
        const int idx = stack.back();
        stack.pop_back();
        const int cy = idx / w;
        const int cx = idx - cy * w;

        region.at<uchar>(cy, cx) = 255;

        const int neighbours[4][2] = {
            {cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}
        };
        for (const auto& n : neighbours) {
            const int nx = n[0];
            const int ny = n[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t nidx = static_cast<size_t>(ny) * w + nx;
            if (visited[nidx]) continue;

            const int db = std::abs(colourAt(nx, ny, 0) - seedB);
            const int dg = std::abs(colourAt(nx, ny, 1) - seedG);
            const int dr = std::abs(colourAt(nx, ny, 2) - seedR);
            if (db > tolerance || dg > tolerance || dr > tolerance) continue;

            visited[nidx] = 1;
            stack.push_back(static_cast<int>(nidx));
        }
    }

    return region;
}

void PaintBucketTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[PaintBucketTool] no editable layer, ignore");
        return;
    }

    const QPointF clamped = ToolGeometry::clampToCanvas(host, scenePos);
    const int px = static_cast<int>(std::lround(clamped.x()));
    const int py = static_cast<int>(std::lround(clamped.y()));

    const cv::Mat region = findRegion(*target, px, py, m_tolerance);
    if (region.empty()) return;

    // The fill colour is the foreground colour.
    QColor fg(Qt::black);
    if (auto* cs = host->colorState()) fg = cs->foregroundColor();

    // The selection masks the fill.
    const QImage mask = host->selectionModel()
                            ? host->selectionModel()->mask()
                            : QImage();
    const bool hasMask = !mask.isNull()
                         && host->selectionModel()
                         && !host->selectionModel()->isEmpty();

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();

    const int channels = target->channels();
    int filled = 0;
    for (int y = 0; y < target->rows; ++y) {
        uchar* row = target->ptr<uchar>(y);
        const uchar* maskRow = hasMask ? mask.constScanLine(y) : nullptr;
        for (int x = 0; x < target->cols; ++x) {
            if (region.at<uchar>(y, x) == 0) continue;
            if (hasMask && maskRow[x] == 0) continue;

            uchar* p = row + x * channels;
            p[0] = static_cast<uchar>(fg.blue());
            p[1] = static_cast<uchar>(fg.green());
            p[2] = static_cast<uchar>(fg.red());
            if (channels == 4) p[3] = 255;
            ++filled;
        }
    }
    m_lastFilledCount = filled;

    if (filled > 0) {
        host->pushLayerImageEdit(m_layerIndex, m_backup, *target,
                                 QStringLiteral("Paint Bucket"));
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    } else {
        m_backup.release();
    }
    host->refreshCanvasNow();
}

QWidget* PaintBucketTool::optionPage(QWidget* parent)
{
    auto* panel = new ToolOptionsPanel(ToolOptionsPanel::Kind::PaintBucket, parent);
    panel->setTolerance(m_tolerance);

    QObject::connect(panel, &ToolOptionsPanel::toleranceChanged, panel,
                     [this](int v) { this->setTolerance(v); });
    return panel;
}

} // namespace tools
