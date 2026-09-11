// SPDX-License-Identifier: MIT
//
// MoveTool - F-C (2026-09-09)
//
// State pattern concrete class - Move tool (V)
//   cursor: SizeAllCursor (4-direction arrow)
//   F-C: no actual move logic; F-N wires eventFilter for layer drag
//
#pragma once

#include "ToolState.h"

namespace tools {

class MoveTool : public ToolState
{
public:
    MoveTool() = default;
    ~MoveTool() override = default;

    void onEnter(ImageWindow* host) override;
    void onExit(ImageWindow* host)  override;

    mediators::ToolId id()        const override { return mediators::ToolId::Move; }
    QString pageTitle() const override { return QStringLiteral("Move"); }
    QCursor cursor()   const override { return QCursor(Qt::SizeAllCursor); }
};

} // namespace tools
