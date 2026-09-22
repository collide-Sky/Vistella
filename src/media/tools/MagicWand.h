// SPDX-License-Identifier: MIT
//
// MagicWand - F-L (2026-09-10) + P0-4.5 (2026-09-10) + P2.1 (2026-09-22)
//
// PS-style MagicWand tool. P2.1: option page (tolerance slider + Contiguous)
// now wired to the strategy; click -> flood fill mask from seed pixel.
//
#pragma once

#include "ToolState.h"
#include <memory>

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

private:
    std::unique_ptr<selection::MagicWandSelectionStrategy> m_strategy;

    // P2.1: option page widgets (raw pointers; parented to optionPage QWidget)
    QSlider*  m_tolSlider   = nullptr;
    QCheckBox* m_contiguous = nullptr;
    QSpinBox*  m_sampleAll  = nullptr;   // P1 reserved (disabled)
};

} // namespace tools
