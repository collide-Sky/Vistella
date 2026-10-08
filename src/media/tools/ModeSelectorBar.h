// SPDX-License-Identifier: MIT
//
// ModeSelectorBar - Q4.2.3 (2026-09-24)
//
// PS-style secondary mode selector bar. Sits between LeftToolBar and
// ImageOptionBar in left dock. Shows tool-specific mode buttons for
// the 5 "mode-driven" tools (Crop/MagicWand/Lasso/Brush/Eyedropper):
//
//   MagicWand + Lasso: 4 selection-mode buttons (New/Add/Subtract/Intersect)
//   Crop:              4 aspect-ratio buttons (Free/1:1/4:3/16:9)
//   Brush:             3 blend-mode buttons (Normal/Multiply/Screen)
//   Eyedropper:        4 sample-size buttons (Point/3x3/5x5/11x11)
//
// Other tools: bar hides (empty page). Listens to ToolContext::toolChanged;
// on each switch, shows the relevant page and refreshes its buttons from
// the current tool's state via getters (selectionMode/aspectRatio/blendMode/
// sampleSize).
//
#pragma once

#include <QWidget>

class QStackedWidget;
class QHBoxLayout;
class QToolButton;
class QButtonGroup;


namespace mediators { enum class ToolId : int; }
namespace tools { class ToolContext; class MagicWand; class Lasso; class Crop; class Brush; class EyedropperTool; }

namespace tools {

class ModeSelectorBar : public QWidget
{
    Q_OBJECT
public:
    explicit ModeSelectorBar(QWidget* parent = nullptr);
    ~ModeSelectorBar() override;

    void attach(ToolContext* ctx);
    void detach();

private slots:
    void onToolChanged(mediators::ToolId id);

    // Selection mode handlers (MagicWand + Lasso share)
    void onSelectionModeBtn(int idx);  // 0..3 -> Replace/Add/Subtract/Intersect

    // Aspect ratio handlers (Crop)
    void onAspectBtn(int idx);

    // Blend mode handlers (Brush)
    void onBlendBtn(int idx);

    // Sample size handlers (Eyedropper)
    void onSampleBtn(int idx);

private:
    void setupUi();
    void rebuildPages();
    void showPageForTool(mediators::ToolId id);
    void refreshButtonsFromTool();

    ToolContext* m_ctx = nullptr;

    // 4 mode buttons per tool (selection mode shared between MagicWand + Lasso,
    //   but each tool keeps its own button group + page).
    struct Page {
        QWidget*     widget     = nullptr;
        QButtonGroup* group     = nullptr;
        QList<QToolButton*> buttons;
    };
    Page m_pageSelection;  // MagicWand OR Lasso (mutually exclusive tool selection)
    Page m_pageCrop;
    Page m_pageBrush;
    Page m_pageEyedropper;
    Page m_pageEmpty;
    QStackedWidget*   m_stacked = nullptr;

    mediators::ToolId m_currentTool = static_cast<mediators::ToolId>(-1);
};

} // namespace tools
