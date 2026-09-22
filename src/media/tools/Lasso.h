// SPDX-License-Identifier: MIT
//
// Lasso - F-L (2026-09-10) + P0-4.5 (2026-09-10) + P2.1 (2026-09-22)
//
// PS-style Lasso tool. P2.1: option page (Feather slider + Anti-alias checkbox)
// wired to the strategy; free-form polygon -> SelectionModel mask.
//
#pragma once

#include "ToolState.h"
#include <memory>

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

private:
    std::unique_ptr<selection::LassoSelectionStrategy> m_strategy;

    // P2.1: option page widgets (raw pointers; parented to optionPage QWidget)
    QSlider*  m_featherSlider = nullptr;
    QCheckBox* m_antiAlias    = nullptr;
};

} // namespace tools
