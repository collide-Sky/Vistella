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

    // The selection clips the fill, and that clipping happens ONCE --
    //   centrally, in ImageWindow::pushLayerImageEdit() -> clipEditToSelection().
    //
    //   It used to be done here as well, with a hard cut
    //   (maskRow[x] == 0 -> skip the pixel). Two clippers with two
    //   different rules: on a feathered edge the hard cut kept the old
    //   colour, and the central clip then blended old -> old = old, so
    //   the feather was lost. Now there is exactly one clipper, and it
    //   is the one that knows about feathering.
    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();

    const int channels = target->channels();
    int painted = 0;
    for (int y = 0; y < target->rows; ++y) {
        uchar* row = target->ptr<uchar>(y);
        for (int x = 0; x < target->cols; ++x) {
            if (region.at<uchar>(y, x) == 0) continue;

            uchar* p = row + x * channels;
            p[0] = static_cast<uchar>(fg.blue());
            p[1] = static_cast<uchar>(fg.green());
            p[2] = static_cast<uchar>(fg.red());
            if (channels == 4) p[3] = 255;
            ++painted;
        }
    }

    if (painted > 0) {
        // pushLayerImageEdit applies the selection clip AND writes the
        //   clipped pixels back into the layer, so after this call the
        //   layer holds exactly what the user sees.
        host->pushLayerImageEdit(m_layerIndex, m_backup, *target,
                                 QStringLiteral("Paint Bucket"));
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);

        // The count reports the pixels that SURVIVED the selection clip.
        //   Counting the pre-clip pixels would tell the user "N pixels
        //   filled" for a fill that the selection cut down to almost
        //   nothing -- and the test asserts on this number.
        m_lastFilledCount = countFilledPixels(*target, fg, painted);
    } else {
        m_lastFilledCount = 0;
        m_backup.release();
    }
    host->refreshCanvasNow();
}

// Count the pixels that really carry the fill colour after the clip.
//
//   The pre-clip count says how many pixels the TOOL painted; the
//   post-clip count says how many the USER got. With a selection that
//   cuts the region, those are very different numbers.
int PaintBucketTool::countFilledPixels(const cv::Mat& layer, const QColor& fg, int limit)
{
    if (layer.empty()) return 0;
    const int channels = layer.channels();
    const int b = fg.blue();
    const int g = fg.green();
    const int r = fg.red();

    int n = 0;
    for (int y = 0; y < layer.rows; ++y) {
        const uchar* row = layer.ptr<uchar>(y);
        for (int x = 0; x < layer.cols; ++x) {
            const uchar* p = row + x * channels;
            if (p[0] == b && p[1] == g && p[2] == r) {
                ++n;
                if (n >= limit) return n;
            }
        }
    }
    return n;
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
