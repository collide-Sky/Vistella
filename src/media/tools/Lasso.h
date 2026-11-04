// SPDX-License-Identifier: MIT
//
// Lasso
//
// PS-style Lasso tool. P2.1: option page (Feather slider + Anti-alias checkbox)
// wired to the strategy; free-form polygon -> SelectionModel mask.
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

namespace selection { class LassoSelectionStrategy; }

namespace tools {

class Lasso : public ToolState
{
public:
    explicit Lasso(QWidget* parent = nullptr);
    ~Lasso() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override;
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // P2.1: option UI hooks
    void setFeather(int r);
    void setAntiAlias(bool b);

    // Q4.2.3 / M2-5: selection mode (for ModeSelectorBar buttons).
    //   The persistent value lives in the document's SelectionModel --
    //   see SelectionModel.h. These two are thin accessors onto it, so
    //   the mode survives a tool switch (the tool instance does not).
    //   Modifier keys at click time still override.
    void setSelectionMode(selection::SelectionModel::Mode m);
    selection::SelectionModel::Mode selectionMode() const;

private:
    std::unique_ptr<selection::LassoSelectionStrategy> m_strategy;

    // P2.1: option page widgets (raw pointers; parented to optionPage QWidget)
    QSlider*  m_featherSlider = nullptr;
    QCheckBox* m_antiAlias    = nullptr;

    // Weak view onto the document's selection model (bound in onEnter).
    //   nullptr while the tool is detached from a document.
    selection::SelectionModel* m_selectionModel = nullptr;

    // Fallback used ONLY while no document is attached (unit tests and
    //   tool previews). It is never read while a document is attached,
    //   so there is only ever one live copy of the mode.
    selection::SelectionModel::Mode m_detachedMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
