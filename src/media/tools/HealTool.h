// SPDX-License-Identifier: MIT
//
// HealTool - P0-9.3 (2026-09-15)
//
// PS 同款修复画笔 (J):
//   - Alt + 单击: 取样
//   - 拖动: 把 source 区域像素复制到 destination
//
// P0-9.3 简化版:
//   - 算法跟 CloneTool 一样 (纯 copy, 不做边缘 inpaint blending)
//   - PS 完整版用 cv::inpaint + texture synthesis 做无缝边缘修复
//   - TODO (P0-9.4): 加 cv::inpaint integration 让边缘 blend
//
#pragma once

#include "CloneTool.h"

namespace tools {

class HealTool : public CloneTool
{
public:
    explicit HealTool(QWidget* parent = nullptr) : CloneTool(parent) {}

    mediators::ToolId id() const override { return mediators::ToolId::Heal; }
    QString pageTitle() const override { return QStringLiteral("修复画笔"); }
};

} // namespace tools