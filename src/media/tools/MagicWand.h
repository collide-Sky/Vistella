// SPDX-License-Identifier: MIT
//
// MagicWand - F-L (2026-09-10) + P0-4.5 (2026-09-10)
//
// PS-style MagicWand tool. P0-4 stage: click -> flood fill mask from seed pixel.
//
#pragma once

#include "ToolState.h"
#include <memory>

class QCursor;
class QWidget;
class QMouseEvent;

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

private:
    std::unique_ptr<selection::MagicWandSelectionStrategy> m_strategy;
};

} // namespace tools
