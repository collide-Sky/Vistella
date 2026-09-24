// SPDX-License-Identifier: MIT
//
// Lasso - F-L (2026-09-10) + P0-4.5 (2026-09-10) + P2.1 (2026-09-22)
//
// PS-style Lasso tool. P2.1: option page (Feather slider + Anti-alias checkbox)
// wired to the strategy; free-form polygon -> SelectionModel mask.
// Q4.2.3 (2026-09-24): explicit selection mode (Replace/Add/Subtract/Intersect)
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

    // Q4.2.3: selection mode (for ModeSelectorBar buttons).
    //   Default Replace. Modifier keys at click time still override.
    void setSelectionMode(selection::SelectionModel::Mode m) { m_selectionMode = m; }
    selection::SelectionModel::Mode selectionMode() const { return m_selectionMode; }

private:
    std::unique_ptr<selection::LassoSelectionStrategy> m_strategy;

    // P2.1: option page widgets (raw pointers; parented to optionPage QWidget)
    QSlider*  m_featherSlider = nullptr;
    QCheckBox* m_antiAlias    = nullptr;

    // Q4.2.3: explicit selection mode (Replace/Add/Subtract/Intersect).
    selection::SelectionModel::Mode m_selectionMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
