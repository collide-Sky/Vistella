// SPDX-License-Identifier: MIT
//
// RectSelect - F-L (2026-09-10) + P0-4.5 (2026-09-10)
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

private:
    std::unique_ptr<selection::RectSelectionStrategy> m_strategy;
};

} // namespace tools
