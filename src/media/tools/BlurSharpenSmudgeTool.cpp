// SPDX-License-Identifier: MIT
//
// BlurSharpenSmudgeTool implementation
//
// See BlurSharpenSmudgeTool.h for the why.
#include "BlurSharpenSmudgeTool.h"
#include "ToolContext.h"
#include "ToolOptionsPanel.h"

#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QMouseEvent>
#include <QObject>
#include <QRect>
#include <QWidget>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace tools {

void BlurSharpenSmudgeTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_strokeOpen = false;
    m_layerIndex = -1;
    m_backup.release();
    m_smudgeColor.release();
    LOG_DEBUG("[BlurSharpenSmudgeTool] onEnter host={}", host ? "yes" : "null");
}

void BlurSharpenSmudgeTool::onExit(ImageWindow* host)
{
    if (m_strokeOpen) commitStroke(host);
    m_strokeOpen = false;
    m_host = nullptr;
}

void BlurSharpenSmudgeTool::blurRegion(cv::Mat& layer, const cv::Rect& region, int strength)
{
    if (region.empty() || layer.empty()) return;
    const cv::Rect clipped = region & cv::Rect(0, 0, layer.cols, layer.rows);
    if (clipped.empty()) return;

    // The kernel size grows with the strength. It must stay odd --
    // OpenCV rejects an even kernel size.
    const int base = 3 + (std::clamp(strength, 1, 100) * 6) / 100;
    const int ksize = std::min(base | 1, 31);

    cv::Mat roi = layer(clipped).clone();

    // The alpha channel is NOT blurred: blurring the alpha would make
    // the edges of the layer transparent.
    const int channels = roi.channels();
    cv::Mat colour;
    if (channels == 4) {
        cv::cvtColor(roi, colour, cv::COLOR_BGRA2BGR);
    } else {
        colour = roi;
    }

    cv::Mat blurred;
    cv::GaussianBlur(colour, blurred, cv::Size(ksize, ksize), 0);

    // The strength is the blend between the original and the blurred.
    const double alpha = std::clamp(strength, 1, 100) / 100.0;
    cv::Mat blended;
    cv::addWeighted(blurred, alpha, colour, 1.0 - alpha, 0.0, blended);

    if (channels == 4) {
        cv::Mat out;
        cv::cvtColor(blended, out, cv::COLOR_BGR2BGRA);
        // Keep the original alpha.
        cv::Mat alphaCh;
        cv::extractChannel(roi, alphaCh, 3);
        cv::insertChannel(alphaCh, out, 3);
        out.copyTo(layer(clipped));
    } else {
        blended.copyTo(layer(clipped));
    }
}

void BlurSharpenSmudgeTool::sharpenRegion(cv::Mat& layer, const cv::Rect& region, int strength)
{
    if (region.empty() || layer.empty()) return;
    const cv::Rect clipped = region & cv::Rect(0, 0, layer.cols, layer.rows);
    if (clipped.empty()) return;

    cv::Mat roi = layer(clipped).clone();
    const int channels = roi.channels();

    cv::Mat colour;
    if (channels == 4) {
        cv::cvtColor(roi, colour, cv::COLOR_BGRA2BGR);
    } else {
        colour = roi;
    }

    // The unsharp mask: the original minus a blurred copy, added back
    // with the strength as the amount.
    const int ksize = 3;
    cv::Mat blurred;
    cv::GaussianBlur(colour, blurred, cv::Size(ksize, ksize), 0);

    cv::Mat highPass = colour - blurred;
    const double amount = std::clamp(strength, 1, 100) / 50.0;
    cv::Mat sharp = colour + highPass * amount;
    sharp.convertTo(sharp, sharp.type(), 1.0, 0.0);

    if (channels == 4) {
        cv::Mat out;
        cv::cvtColor(sharp, out, cv::COLOR_BGR2BGRA);
        cv::Mat alphaCh;
        cv::extractChannel(roi, alphaCh, 3);
        cv::insertChannel(alphaCh, out, 3);
        out.copyTo(layer(clipped));
    } else {
        sharp.copyTo(layer(clipped));
    }
}

void BlurSharpenSmudgeTool::applyAt(ImageWindow* host, const QPointF& scenePos)
{
    if (!host) return;
    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) return;

    const int half = m_brushSize / 2;
    const int cx = static_cast<int>(std::lround(scenePos.x()));
    const int cy = static_cast<int>(std::lround(scenePos.y()));
    const cv::Rect region(cx - half, cy - half, m_brushSize, m_brushSize);

    switch (m_mode) {
    case Mode::Blur:
        blurRegion(*target, region, m_strength);
        break;
    case Mode::Sharpen:
        sharpenRegion(*target, region, m_strength);
        break;
    case Mode::Smudge:
        // The smudge drags the colour of the previous brush sample
        // towards the current position.
        {
            const cv::Rect clipped = region & cv::Rect(0, 0, target->cols, target->rows);
            if (clipped.empty()) break;
            if (m_smudgeColor.empty()) {
                // The first sample: the colour under the brush.
                m_smudgeColor = (*target)(clipped).clone();
            } else {
                cv::Mat roi = (*target)(clipped).clone();
                const int channels = roi.channels();
                cv::Mat blended = roi.clone();
                for (int y = 0; y < roi.rows; ++y) {
                    uchar* dst = blended.ptr<uchar>(y);
                    const uchar* src = m_smudgeColor.ptr<uchar>(y);
                    for (int x = 0; x < roi.cols; ++x) {
                        uchar* d = dst + x * channels;
                        const uchar* s = src + x * channels;
                        for (int c = 0; c < 3 && c < channels; ++c) {
                            d[c] = static_cast<uchar>(
                                (int(d[c]) + int(s[c])) / 2);
                        }
                    }
                }
                blended.copyTo((*target)(clipped));
                m_smudgeColor = blended;
            }
        }
        break;
    }
}

void BlurSharpenSmudgeTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!host || !m_host) return;
    if (e->button() != Qt::LeftButton) return;

    cv::Mat* target = host->activeLayerImage();
    if (!target || target->empty()) {
        LOG_WARN("[BlurSharpenSmudgeTool] no editable layer, ignore");
        return;
    }

    if (auto* ctx = host->toolContext()) {
        m_brushSize = std::max(1, ctx->brushSharedState().brushSize);
    }

    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_strokeOpen = true;
    m_lastPos = scenePos;
    m_smudgeColor.release();

    applyAt(host, scenePos);
    host->refreshCanvasNow();
}

void BlurSharpenSmudgeTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_strokeOpen || !host) return;

    // The effect is applied along the stroke so that a fast mouse
    // movement does not leave gaps.
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

void BlurSharpenSmudgeTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    Q_UNUSED(scenePos);
    if (!m_strokeOpen) return;
    commitStroke(host);
}

void BlurSharpenSmudgeTool::commitStroke(ImageWindow* host)
{
    if (!m_strokeOpen) return;
    m_strokeOpen = false;

    if (host) {
        cv::Mat* cur = host->activeLayerImage();
        if (cur && !m_backup.empty() && cur->size() == m_backup.size()
            && cur->type() == m_backup.type()) {
            host->pushLayerImageEdit(m_layerIndex, m_backup, *cur,
                                     QStringLiteral("Blur / Sharpen / Smudge"));
        }
        m_backup.release();
        host->commitLayerEdit(m_layerIndex);
    } else {
        m_backup.release();
    }
    m_smudgeColor.release();
}

QWidget* BlurSharpenSmudgeTool::optionPage(QWidget* parent)
{
    auto* panel = new ToolOptionsPanel(ToolOptionsPanel::Kind::BlurSharpenSmudge, parent);
    panel->setMode(static_cast<int>(m_mode));
    panel->setStrength(m_strength);
    panel->setBrushSize(m_brushSize);

    QObject::connect(panel, &ToolOptionsPanel::modeChanged, panel,
                     [this](int index) {
        switch (index) {
        case 1:  this->setMode(Mode::Sharpen); break;
        case 2:  this->setMode(Mode::Smudge);  break;
        default: this->setMode(Mode::Blur);    break;
        }
    });
    QObject::connect(panel, &ToolOptionsPanel::strengthChanged, panel,
                     [this](int v) { this->setStrength(v); });
    QObject::connect(panel, &ToolOptionsPanel::brushSizeChanged, panel,
                     [this](int v) { this->setBrushSize(v); });
    return panel;
}

} // namespace tools
