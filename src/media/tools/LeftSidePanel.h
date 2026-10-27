// SPDX-License-Identifier: MIT
//
// LeftSidePanel
//
// PS-style left side panel: single container widget hosting LeftToolBar +
// ImageOptionBar + ModeSelectorBar + AppBar.
//
// P8.3.2 layout (horizontal, PS shape):
//
//   outerLayout (QHBoxLayout, spacing=0, no margin)
//     [LeftToolBar (56 wide, fixed)
//      vertical sep line (1px)
//      rightColumn (QVBoxLayout)
//        ImageOptionBar (expanding - the tool options form)
//        ModeSelectorBar (fixed height 36)
//        AppBar (fixed height 32)]
//
//   The v2 layout stacked the same four widgets in a QVBoxLayout, so the
//   56px-wide tool column sat ON TOP of a ~320px-wide options form while
//   the host dock was sized for the tool column alone. The options form
//   overflowed the dock and painted over the title bar and the canvas.
//   PS puts the tool column on the LEFT and the options panel to its
//   RIGHT, which is what the horizontal layout gives.
//
// Why one container instead of 4 separate docks:
//   QMainWindow's addDockWidget + splitDockWidget chain produces inconsistent
//   layouts across Qt versions. Multiple docks added to LeftDockWidgetArea can
//   tabify, place at unexpected positions, or collapse when one widget's
//   content has 0 preferred height. v1 (P7.6.x commit da99a1b) used 4 docks
//   and the LeftToolBar collapsed to 0 height (entirely invisible) — exactly
//   what the user reported: "左侧图标显示不完整" (left icons not fully shown).
//
//   Putting everything in one widget + one dock container makes the layout
//   deterministic: the layout allocates space left-to-right, each widget
//   gets its sizeHint, no QMainWindow dock-tabifying quirks.
//
// Trade-offs:
//   - This is NOT a dock anymore — can't be detached/floated by user. PS has
//     the same constraint (left tool column is not detachable). For the
//     actual canvas dock (defer to P7.11), the dock pattern stays valid.
//   - Each child widget is owned by Qt tree (parent=this), raw pointers per
//     P7.6.x smart pointer rule (QWidget in Qt tree -> raw pointer).

#pragma once

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>

#include "LeftToolBar.h"
#include "ModeSelectorBar.h"
#include "ImageOptionBar.h"
#include "AppBar.h"

namespace tools {

class LeftSidePanel : public QWidget
{
    Q_OBJECT
public:
    explicit LeftSidePanel(QWidget* parent = nullptr);
    ~LeftSidePanel() override;

    // Accessors for downstream wiring (imagewindow.cpp creates these widgets
    //   and passes raw pointers here; LeftSidePanel just hosts them in layout).
    LeftToolBar*    leftToolBar()    const { return m_leftToolBar; }
    ModeSelectorBar* modeSelectorBar() const { return m_modeSelectorBar; }
    ImageOptionBar* imageOptionBar() const { return m_imageOptionBar; }
    AppBar*         appBar()         const { return m_appBar; }

private:
    QVBoxLayout*   m_root = nullptr;
    LeftToolBar*   m_leftToolBar    = nullptr;
    ModeSelectorBar* m_modeSelectorBar = nullptr;
    ImageOptionBar* m_imageOptionBar = nullptr;
    AppBar*        m_appBar    = nullptr;
};

} // namespace tools
