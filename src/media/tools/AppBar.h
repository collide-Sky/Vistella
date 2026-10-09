// SPDX-License-Identifier: MIT
//
// AppBar - P5.5+ (2026-10-09)
//
//   PS-style "above-canvas global toggle bar" — three compact groups:
//     - Quick Mask: checkable toggle that drives the MaskBrush tool.
//     - Screen Mode: 3-state segmented selector (standard / fullscreen /
//       fullscreen with menu bar).
//     - Capture: dropdown menu (selected region / window / full screen),
//       saves a PNG to the user's Pictures directory.
//
//   Sits below ImageOptionBar in the Qt::LeftDockWidgetArea dock stack,
//   height ~32 px. Does not compete with active-tool option panels —
//   complements them (always present, irrespective of active tool).
//
// Wiring:
//   emit quickMaskToggled(bool)         -> ImageWindow::onQuickMaskToggled
//   emit screenModeSelected(int)        -> ImageWindow::onScreenModeSelected
//   emit captureRequested(CaptureKind)  -> ImageWindow::onCaptureRequested
//
#pragma once

#include <QWidget>

namespace Ui { class AppBar; }

namespace tools {

class AppBar : public QWidget
{
    Q_OBJECT
public:
    // Capture variants — must match the menu order in AppBar.cpp.
    enum CaptureKind {
        CaptureSelected = 0,
        CaptureWindow   = 1,
        CaptureFullScreen = 2,
    };
    Q_ENUM(CaptureKind)

    // Screen modes — must match the segmented selector order.
    enum ScreenMode {
        ScreenStandard         = 0,
        ScreenFullScreen       = 1,
        ScreenFullScreenMenu   = 2,
    };
    Q_ENUM(ScreenMode)

    explicit AppBar(QWidget* parent = nullptr);
    ~AppBar() override;

signals:
    void quickMaskToggled(bool on);
    void screenModeSelected(int mode);
    void captureRequested(int kind);  // AppBar::CaptureKind

private slots:
    void onQuickMaskClicked(bool checked);
    void onScreenModeBtnClicked(int id);
    void onCaptureMenuTriggered(QAction* a);

private:
    void setupCaptureMenu();

    Ui::AppBar* ui = nullptr;
    bool m_quickMask = false;
};

} // namespace tools
