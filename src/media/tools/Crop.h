// SPDX-License-Identifier: MIT
//
// Crop - F-L (2026-09-10)
//
// Basic PS-style tool subclass. F-L stage: id/pageTitle/cursor/optionPage simple impl.
//   - optionPage returns QWidget + QLabel placeholder (TODO: actual controls later)
//   - Mouse event forwarding: F-N stage eventFilter
//
#pragma once

#include "ToolState.h"

class QCursor;
class QWidget;

namespace tools {

class Crop : public ToolState
{
public:
    explicit Crop(QWidget* parent = nullptr);
    ~Crop() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host) override;

    mediators::ToolId id() const override;
    QString pageTitle() const override;
    QCursor cursor() const override;
    QWidget* optionPage(QWidget* parent = nullptr) override;
};

} // namespace tools
