// SPDX-License-Identifier: MIT
//
// RectSelect
//
// PS-style RectSelect tool. P0-4 stage: drag rectangle -> SelectionModel mask.
//   - Strategy pattern: holds RectSelectionStrategy instance
//   - onMousePress -> strategy->begin(p)
//   - onMouseMove  -> strategy->update(p)
//   - onMouseRelease -> strategy->end(image) -> SelectionModel::setMask(mask, mode)
//
#pragma once

#include "ToolState.h"
#include <memory>

#include "../selection/SelectionModel.h"

class QCursor;
class QWidget;
class QMouseEvent;

namespace selection { class RectSelectionStrategy; }

namespace tools {

class RectSelect : public ToolState
{
public:
    explicit RectSelect(QWidget* parent = nullptr);
    ~RectSelect() override;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    // P0-4.5: gesture lifecycle
    void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
    void onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;

    mediators::ToolId id() const override;
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;

    // The selection mode (New / Add / Subtract / Intersect).
    //   The persistent value lives in the document's SelectionModel --
    //   see SelectionModel.h. These two are thin accessors onto it, so
    //   the mode survives a tool switch (the tool instance does not).
    //   Modifier keys at click time still override the mode.
    void setSelectionMode(selection::SelectionModel::Mode m);
    selection::SelectionModel::Mode selectionMode() const;

private:
    std::unique_ptr<selection::RectSelectionStrategy> m_strategy;

    // Weak view onto the document's selection model (bound in onEnter).
    //   nullptr while the tool is detached from a document.
    selection::SelectionModel* m_selectionModel = nullptr;

    // Fallback used ONLY while no document is attached (unit tests and
    //   tool previews). It is never read while a document is attached,
    //   so there is only ever one live copy of the mode.
    selection::SelectionModel::Mode m_detachedMode = selection::SelectionModel::Mode::Replace;
};

} // namespace tools
