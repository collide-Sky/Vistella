// SPDX-License-Identifier: MIT
//
// MagicWand
//
// PS-style MagicWand tool. P2.1: option page (tolerance slider + Contiguous)
// now wired to the strategy; click -> flood fill mask from seed pixel.
// explicit selection mode (Replace/Add/Subtract/Intersect)
//   for ModeSelectorBar. Modifier keys (Shift/Alt) still override per-click.
//
#pragma once

#include "ToolState.h"
#include <memory>

#include "../selection/SelectionModel.h"

class QCursor;
class QWidget;
class QMouseEvent;
class QSlider;
class QCheckBox;
class QSpinBox;

namespace selection { class MagicWandSelectionStrategy; }

namespace tools {

class MagicWand : public ToolState
{
public:
    explicit MagicWand(QWidget* parent = nullptr);
    ~MagicWand() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override;
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P2.1: option UI hooks (set by view optionPage constructor); strategy wiring
    void setTolerance(int t);
    void setContiguous(bool c);

    // Q4.2.3 / M2-5: selection mode (for ModeSelectorBar buttons).
    //   The persistent value lives in the document's SelectionModel --
    //   see SelectionModel.h. These two are thin accessors onto it, so
    //   the mode survives a tool switch (the tool instance does not).
    //   Modifier keys at click time still override
    //   (Shift -> Add, Alt -> Subtract, Shift+Alt -> Intersect).
    void setSelectionMode(selection::SelectionModel::Mode m);
    selection::SelectionModel::Mode selectionMode() const;

private:
    std::unique_ptr<selection::MagicWandSelectionStrategy> m_strategy;

    // P2.1: option page widgets (raw pointers; parented to optionPage QWidget)
    QSlider*  m_tolSlider   = nullptr;
    QCheckBox* m_contiguous = nullptr;
    QSpinBox*  m_sampleAll  = nullptr;   // P1 reserved (disabled)

    // Weak view onto the document's selection model (bound in onEnter).
    //   nullptr while the tool is detached from a document.
    selection::SelectionModel* m_selectionModel = nullptr;

    // Fallback used ONLY while no document is attached (unit tests and
    //   tool previews). It is never read while a document is attached,
    //   so there is only ever one live copy of the mode.
    selection::SelectionModel::Mode m_detachedMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
