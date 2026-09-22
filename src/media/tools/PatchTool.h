// SPDX-License-Identifier: MIT
//
// PatchTool - P0-9.3 (2026-09-15) + P0-9.4 (2026-09-15) + P2.2 (2026-09-22)
//
// PS-style Patch tool:
//   - drag rectangle for source area
//   - release, then drag destination — source pixels applied to destination
//
// P0-9.4 (2026-09-15): cv::seamlessClone (NORMAL_CLONE) for content-aware fill
//
// P2.2 (2026-09-22):
//   - i18n title via QCoreApplication::translate
//   - optionPage: Patch mode QComboBox (Normal / Mixed / Monochrome Transfer)
//   - ImageEditCommand undo integration: snapshot m_current before applyPatch,
//     push single ImageEditCommand after seamlessClone (MosaicTool pattern)
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

class PatchTool : public ToolState
{
public:
    explicit PatchTool(QWidget* parent = nullptr);
    ~PatchTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override { return mediators::ToolId::Patch; }
    QString pageTitle() const override;
    QCursor cursor() const override { return Qt::CrossCursor; }
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P0-9.3 test accessors
    bool    hasSource() const { return m_hasSource; }
    QPointF sourceA() const { return m_sourceA; }
    QPointF sourceB() const { return m_sourceB; }

    void clearSourceForTest() { m_hasSource = false; }

    // P2.2: patch mode setter (driven by optionPage QComboBox)
    enum class PatchMode { Normal = 0, Mixed = 1, MonochromeTransfer = 2 };
    void setPatchMode(PatchMode m) { m_patchMode = m; }
    PatchMode patchMode() const { return m_patchMode; }

private:
    QPointer<ImageWindow> m_host;
    QPointF m_pressScenePos;
    QPointF m_lastScenePos;
    bool    m_hasSource = false;
    QPointF m_sourceA, m_sourceB;       // source rectangle
    bool    m_selectingSource = false;  // 第一次拖: 选 source; 第二次拖: 选 destination

    // P2.2: patch mode (PS: Source / Destination / Mixed mode; we expose 3 common ones)
    PatchMode m_patchMode = PatchMode::Normal;

    // P2.2: undo snapshot (snapshot before applyPatch, push after)
    cv::Mat m_backup;
    bool    m_strokeOpen = false;

    void applyPatch(ImageWindow* host, const QPointF& dstA, const QPointF& dstB);
};

} // namespace tools