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
#include <QFrame>

namespace tools {

LeftSidePanel::LeftSidePanel(QWidget* parent)
    : QWidget(parent)
{
    m_root = new QVBoxLayout(this);
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
    m_root->addWidget(m_leftToolBar);

    // 1px sep line between LeftToolBar and ImageOptionBar
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Plain);
    sep1->setFixedHeight(1);
    m_root->addWidget(sep1);

    // ImageOptionBar: tool options. BrushOptionsPanel etc. Expanding.
    m_imageOptionBar = new ImageOptionBar(this);
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
}

LeftSidePanel::~LeftSidePanel() = default;

} // namespace tools