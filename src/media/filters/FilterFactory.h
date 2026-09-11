// SPDX-License-Identifier: MIT
//
// FilterFactory - P0-5.2 (2026-09-10)
//
// Factory pattern for 20 FilterStrategy instances.
//   - createFilter(FilterKind): unique_ptr<FilterStrategy> (new instance, default params)
//   - createFilter(FilterKind, params QVariantMap): with custom params (P1 reserved)
//
// 集成:
//   - ImageWindow::applyFilter(kind) 调 createFilter + apply + push FilterCommand
//   - FilterDialog 用 createFilter 做 preview
//
#pragma once

#include <memory>
#include "FilterStrategy.h"

namespace filter {

class FilterFactory
{
public:
    // 20 种滤镜的 factory (返回带默认参数的实例, caller 调 setter 改)
    static std::unique_ptr<FilterStrategy> createFilter(FilterKind kind);
};

} // namespace filter
