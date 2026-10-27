// VCompositeStrategy.h
//
// Composite strategy for a RightPanelDock (and any other composite container
// holding several sub-pages).
//   placeholders for multi-threaded compositing
//   Actual parallel compositing is wired in P8.4 (EngineContext::background
//   pool). For now the enum is the single switch site, so the upgrade is a
//   1-line flip in the producer.
//
//   - Sequential : current behaviour. Sub-pages rendered one after another
//                  on the UI thread. Safe, deterministic, easy to reason about.
//   - Parallel   : P8.4 will run each sub-page composite on the background
//                  pool, then merge. Requires pages to be free of shared
//                  mutable state outside the documented attach() contract.
#pragma once

namespace vistella::ui {

enum class CompositeStrategy {
    Sequential = 0,
    Parallel   = 1,
};

} // namespace vistella::ui
