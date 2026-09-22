// SPDX-License-Identifier: MIT
//
// HealTool - P0-9.3 (2026-09-15) + P0-9.4 (2026-09-15) + P2.2 (2026-09-22)
//
// PS-style Healing Brush (J):
//   - Alt + click: sample
//   - drag: inpaint destination region from surrounding pixels
//
// P0-9.4 (2026-09-15): cv::inpaint (Telea) integrates edge blending
//   - unlike CloneTool pure copy, this preserves surrounding texture
//
// P2.2 (2026-09-22):
//   - i18n title override ("Healing Brush")
//   - Inherits ImageEditCommand undo + brush cursor from base
//
#pragma once

#include "CloneTool.h"

namespace tools {

class HealTool : public CloneTool
{
public:
    explicit HealTool(QWidget* parent = nullptr) : CloneTool(parent) {}

    mediators::ToolId id() const override { return mediators::ToolId::Heal; }
    QString pageTitle() const override;        // P2.2: i18n override

    // P2.2: P0-9.4 HealTool overrides onMouseMove to use cv::inpaint (Telea)
    //   instead of plain CloneTool copy. Mouse press/release/sample logic is
    //   inherited from CloneTool base.
    void onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos) override;
};

} // namespace tools