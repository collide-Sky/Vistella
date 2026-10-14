// SPDX-License-Identifier: MIT
//
// LeftSidePanel - P7.6.x v2 (2026-10-14)
//
// PS-style left side panel: single container widget hosting LeftToolBar +
// ModeSelectorBar + ImageOptionBar + AppBar in a QVBoxLayout.
//
//   rootLayout (QVBoxLayout, spacing=0, no margin)
//     [LeftToolBar (P7.1 PS-style, ~56 wide, fixed width by minimumSize)
//      horizontal sep line (1px) for visual break
//      ImageOptionBar (expanding - dominant content)
//      horizontal sep line
//      ModeSelectorBar (fixed height 36)
//      AppBar (fixed height 32)]
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
//   deterministic: QVBoxLayout allocates space top-to-bottom, each widget
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

class QVBoxLayout;

namespace tools {

class LeftToolBar;
class ModeSelectorBar;
class ImageOptionBar;
class AppBar;

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