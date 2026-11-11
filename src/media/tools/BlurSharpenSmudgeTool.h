// SPDX-License-Identifier: MIT
//
// BlurSharpenSmudgeTool
//
// The blur / sharpen / smudge tool (R).
//
//   Photoshop puts three tools behind one button:
//     * Blur    softens the pixels under the brush.
//     * Sharpen makes them crisper.
//     * Smudge  drags the colour of the previous point along the
//               stroke (the "finger painting" effect).
//
//   The mode comes from the option page.
//
//   The effect is applied to the ACTIVE LAYER's pixels and pushed as
//   one undo command.
//
//   The blur and the sharpen are computed on the brush area only (a
//   full-canvas filter per mouse move would be far too slow for an
//   interactive tool). The smudge carries the previous brush sample
//   along the stroke.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class BlurSharpenSmudgeTool : public ToolState
{
public:
    enum class Mode {
        Blur    = 0,   // 模糊
        Sharpen = 1,   // 锐化
        Smudge  = 2    // 涂抹
    };

    BlurSharpenSmudgeTool() = default;
    ~BlurSharpenSmudgeTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::BlurSharpenSmudge; }
    QString pageTitle() const override { return QStringLiteral("Blur / Sharpen / Smudge"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== configuration =====
    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }
    void setStrength(int pct) { m_strength = std::clamp(pct, 1, 100); }
    int  strength() const { return m_strength; }
    void setBrushSize(int px) { m_brushSize = std::max(1, px); }
    int  brushSize() const { return m_brushSize; }

    // ===== test accessors =====
    bool isStroking() const { return m_strokeOpen; }

    // Blur one region of the layer.
    //
    //   Exposed so that the test can check the maths on a known input
    //   without building a window.
    static void blurRegion(cv::Mat& layer, const cv::Rect& region, int strength);
    // Sharpen one region of the layer.
    static void sharpenRegion(cv::Mat& layer, const cv::Rect& region, int strength);

private:
    QPointer<ImageWindow> m_host;

    Mode    m_mode = Mode::Blur;
    int     m_strength = 50;
    int     m_brushSize = 40;
    bool    m_strokeOpen = false;
    int     m_layerIndex = -1;
    cv::Mat m_backup;
    QPointF m_lastPos;
    // The colour the smudge drags along the stroke.
    cv::Mat m_smudgeColor;

    void applyAt(ImageWindow* host, const QPointF& scenePos);
    void commitStroke(ImageWindow* host);
};

} // namespace tools
