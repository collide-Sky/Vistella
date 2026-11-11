// SPDX-License-Identifier: MIT
//
// DodgeBurnTool
//
// The dodge / burn tool (O).
//
//   Photoshop behaviour:
//     * Dodge  lightens the pixels: the colour moves towards white.
//     * Burn  darkens the pixels: the colour moves towards black.
//
//   The amount comes from the option page (the Photoshop default is
//   50%).
//
//   The effect is applied to the ACTIVE LAYER's pixels and pushed as
//   one undo command.
//
//   "Exposure" style maths (the Photoshop approximation): the pixel is
//   pushed away from (or towards) the mid grey, and the pushed value
//   is clipped. A pixel that is already at the limit does not change.
#pragma once

#include "ToolState.h"

#include <QPointF>
#include <QPointer>

#include <opencv2/core.hpp>

class QMouseEvent;
class QWidget;
class ImageWindow;

namespace tools {

class DodgeBurnTool : public ToolState
{
public:
    enum class Mode {
        Dodge = 0,   // 减淡 -- lighten
        Burn  = 1    // 加深 -- darken
    };

    DodgeBurnTool() = default;
    ~DodgeBurnTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::DodgeBurn; }
    QString pageTitle() const override { return QStringLiteral("Dodge / Burn"); }
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // ===== configuration =====
    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }
    void setAmount(int pct) { m_amount = std::clamp(pct, 1, 100); }
    int  amount() const { return m_amount; }
    void setBrushSize(int px) { m_brushSize = std::max(1, px); }
    int  brushSize() const { return m_brushSize; }

    // ===== test accessors =====
    bool isStroking() const { return m_strokeOpen; }

    // The dodge / burn maths for a single colour channel.
    //
    //   Exposed so that the test can check the maths without building
    //   a window, and so that the "already at the limit" case is
    //   pinned.
    static uchar dodgeChannel(uchar value, int amountPct);
    static uchar burnChannel(uchar value, int amountPct);

private:
    QPointer<ImageWindow> m_host;

    Mode    m_mode = Mode::Dodge;
    int     m_amount = 50;
    int     m_brushSize = 40;
    bool    m_strokeOpen = false;
    int     m_layerIndex = -1;
    cv::Mat m_backup;
    QPointF m_lastPos;

    void applyAt(ImageWindow* host, const QPointF& scenePos);
    void commitStroke(ImageWindow* host);
};

} // namespace tools
