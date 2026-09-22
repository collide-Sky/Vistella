// SPDX-License-Identifier: MIT
//
// CloneTool - P0-9.3 (2026-09-15) + P2.2 (2026-09-22)
//
// PS-style Clone Stamp (S):
//   - Alt + click: sample source point
//   - click + drag: copy source pixels to destination
//   - optionPage: brush size slider
//
// Algorithm (P0-9.3 simplified):
//   - cv::Mat + cv::Rect slice + paste
//   - No edge blending (PS full version has hardness), simplified to pure copy
//
// P2.2 (2026-09-22):
//   - i18n via QCoreApplication::translate("tools::CloneTool", ...) — ToolState
//     is not QObject so tr() member is unavailable (see Lasso/MagicWand.cpp note)
//   - Brush size UI upgraded from QSpinBox to QSlider + value label (consistent
//     with P2.1 MagicWand optionPage style)
//   - ImageEditCommand undo integration (MosaicTool pattern):
//     onMousePress snapshots m_current into m_backup, onMouseRelease pushes
//     a single ImageEditCommand for the entire stroke (avoids per-frame push
//     which would explode the undo stack on long strokes)
//
#pragma once

#include "ToolState.h"

#include <QCursor>
#include <QPointF>
#include <QPointer>
#include <opencv2/core.hpp>

class QMouseEvent;
class QKeyEvent;
class QWidget;
class ImageWindow;

namespace tools {

class CloneTool : public ToolState
{
public:
    explicit CloneTool(QWidget* parent = nullptr);
    ~CloneTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onKeyPress(QKeyEvent* e, ImageWindow* host) override;

    mediators::ToolId id() const override { return mediators::ToolId::Clone; }
    QString pageTitle() const override;
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    bool    hasSample() const { return m_hasSample; }
    QPointF samplePoint() const { return m_samplePoint; }
    int     brushSize() const { return m_brushSize; }

    void clearSampleForTest() { m_hasSample = false; }

protected:
    // P2.2: stroke undo snapshot (MosaicTool pattern)
    cv::Mat m_backup;
    bool    m_strokeOpen = false;

    // P2.2: brush cursor radius accessor (HealTool may want a different size)
    void setBrushSize(int s) { m_brushSize = s; }

    // P2.2: subclass override point (e.g. HealTool may want "Healing Brush" title)
    virtual QString translateContext() const { return QStringLiteral("tools::CloneTool"); }
    virtual QString translateTitle() const { return QStringLiteral("Clone Stamp"); }

    // P2.2: brush cursor helpers
    void showBrushCursor(ImageWindow* host, const QPointF& pos);
    void hideBrushCursor(ImageWindow* host);

protected:
    // P2.2: stroke state exposed to subclasses (HealTool overrides onMouseMove
    //   and reads m_brushSize / m_lastDstPos / m_dragging / m_hasSample).
    bool    m_hasSample = false;
    QPointF m_samplePoint;
    QPointF m_lastDstPos;
    bool    m_dragging = false;
    int     m_brushSize = 20;

private:
    QPointer<ImageWindow> m_host;

    void paintAt(QImage& img, const QPointF& dst, const QPointF& src);
    void commitStroke(const QString& text);
};

} // namespace tools