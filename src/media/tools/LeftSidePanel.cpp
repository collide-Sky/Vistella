// SPDX-License-Identifier: MIT
//
// LeftSidePanel impl - P7.6.x v2 (2026-10-14)

#include "LeftSidePanel.h"

// ImageOptionBar.h transitively includes TransformTool.h which has
//   QPointer<ImageWindow> m_host. QPointer<ImageWindow> instantiation requires
//   full ImageWindow definition (QObject cast in qpointer.h:76). Pull in
//   imagewindow.h first to make ImageWindow complete.
//   Same pattern as ImageOptionBar.cpp top-of-file.
#include "../imagewindow.h"

#include "LeftToolBar.h"
#include "ModeSelectorBar.h"
#include "ImageOptionBar.h"
#include "AppBar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

namespace tools {

LeftSidePanel::LeftSidePanel(QWidget* parent)
    : QWidget(parent)
{
    // P8.3.1 (2026-10-26): horizontal, not vertical.
    //
    //   The v2 layout stacked [tool bar | options] in a QVBoxLayout, so the
    //   56px-wide tool column sat ON TOP of a ~320px-wide options form.
    //   The host dock is 56px wide (it was sized for the tool column alone),
    //   so the options form overflowed the dock and painted over the title
    //   bar and the canvas — the "everything is scrambled" report.
    //
    //   PS puts the tool column on the LEFT and the options panel to its
    //   RIGHT, which is what a horizontal layout gives. The dock is then
    //   sized to the sum: 56 (tools) + 300 (options).
    auto* outer = new QHBoxLayout(this);
    outer->setSpacing(0);
    outer->setContentsMargins(0, 0, 0, 0);

    // Right column: options panel on top, then the two always-on bars.
    m_root = new QVBoxLayout();
    m_root->setSpacing(0);
    m_root->setContentsMargins(0, 0, 0, 0);

    // LeftToolBar: PS-style narrow column. 14 tools (28x28 2 cols) + fg/bg swatch.
    //   Parent=this, raw pointer per P7.6.x smart pointer rule.
    m_leftToolBar = new LeftToolBar(this);
    // Force the LeftToolBar to be narrow (56 wide). Inside, its QVBoxLayout
    //   rootLayout gives toolGridContainer sizeHint.height (~196) + fgBgContainer
    //   50px = ~246 tall. The dock host gives LeftSidePanel full height of left
    //   area, so LeftToolBar naturally takes ~246 + empty space below.
    m_leftToolBar->setMinimumWidth(56);
    m_leftToolBar->setMaximumWidth(56);
    outer->addWidget(m_leftToolBar);

    // 1px sep line between LeftToolBar and ImageOptionBar
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Plain);
    sep1->setFixedWidth(1);
    outer->addWidget(sep1);

    // ImageOptionBar: tool options. BrushOptionsPanel etc. Expanding.
    m_imageOptionBar = new ImageOptionBar(this);
    // The options form is ~320 wide (BrushOptionsPanel's design size). Give
    // it a floor so the dock cannot be squeezed below what its content
    // needs — that squeeze is what produced the clipped labels.
    m_imageOptionBar->setMinimumWidth(300);
    m_root->addWidget(m_imageOptionBar, /*stretch=*/1);

    // ModeSelectorBar: secondary mode selector (Blend/SampleSize/etc.).
    //   Q4.2.3 (2026-09-24) — sits between LeftToolBar and ImageOptionBar in
    //   v1; in v2 it goes BELOW ImageOptionBar (closer to status bar feel).
    m_modeSelectorBar = new ModeSelectorBar(this);
    m_modeSelectorBar->setFixedHeight(36);
    m_root->addWidget(m_modeSelectorBar);

    // AppBar: PS-style always-on global toggles (Quick Mask / Screen Mode / Capture)
    m_appBar = new AppBar(this);
    m_appBar->setFixedHeight(32);
    m_root->addWidget(m_appBar);

    auto* rightColumn = new QWidget(this);
    rightColumn->setLayout(m_root);
    outer->addWidget(rightColumn, /*stretch=*/1);
}

LeftSidePanel::~LeftSidePanel() = default;

} // namespace tools