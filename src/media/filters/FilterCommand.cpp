// SPDX-License-Identifier: MIT
//
// FilterCommand implementation - P0-5.8 (2026-09-10), P3.1.1 (2026-09-22)
//
#include "FilterCommand.h"
#include "../imagewindow.h"
#include "FilterFactory.h"
#include "logger.h"

namespace filter {

FilterCommand::FilterCommand(ImageWindow *host, const cv::Mat &before, FilterKind kind,
                             const QString &text, QUndoCommand *parent)
    : QUndoCommand(parent), m_host(host), m_before(before.clone()), m_kind(kind)
{
    setText(text);
    // Apply filter once at ctor time -> m_after
    auto strategy = FilterFactory::createFilter(kind);
    if (strategy && !before.empty()) {
        strategy->apply(before, m_after);
    } else {
        m_after = before.clone();
    }
}

// P3.1.1 (2026-09-22): 用 caller 提供的 strategy (FilterDialog 持有, 已用用户参数)
//   跟原 ctor 行为一致: apply 一次存 m_after; redo() replaceCurrentImage(m_after).
//   关键: FilterCommand 不动 caller 的 m_current, redo 仅替换当前显示, 不重复 apply.
//   这跟 P0-5 factory pattern 一致 — caller (ImageWindow::applyFilterWithStrategy
//   或 FilterDialog::onOkClicked) 不显式 setCurrentImage, FilterCommand 自己 apply.
FilterCommand::FilterCommand(ImageWindow *host, const cv::Mat &before,
                             FilterStrategy *strategy, const QString &text,
                             QUndoCommand *parent)
    : QUndoCommand(parent), m_host(host), m_before(before.clone()),
      m_kind(strategy ? strategy->kind() : FilterKind::FilterGallery)
{
    setText(text);
    if (strategy && !before.empty()) {
        strategy->apply(before, m_after);
    } else {
        m_after = before.clone();
    }
}

void FilterCommand::undo()
{
    if (!m_host) return;
    if (!m_before.empty()) {
        m_host->replaceCurrentImage(m_before);
        LOG_INFO("[FilterCommand] undo: kind={}", static_cast<int>(m_kind));
    }
}

void FilterCommand::redo()
{
    if (!m_host) return;
    if (m_firstRedo) {
        // push() triggers redo once; m_after already prepared
        m_firstRedo = false;
    }
    if (!m_after.empty()) {
        m_host->replaceCurrentImage(m_after);
        LOG_INFO("[FilterCommand] redo: kind={}", static_cast<int>(m_kind));
    }
}

} // namespace filter