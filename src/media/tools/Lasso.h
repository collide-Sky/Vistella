// SPDX-License-Identifier: MIT
//
// Lasso - F-L (2026-09-10) + P0-4.5 (2026-09-10)
//
// PS-style Lasso tool. P0-4 stage: free-form polygon -> SelectionModel mask.
//
#pragma once

#include "ToolState.h"
#include <memory>

class QCursor;
class QWidget;
class QMouseEvent;

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

private:
    std::unique_ptr<selection::LassoSelectionStrategy> m_strategy;
};

} // namespace tools
