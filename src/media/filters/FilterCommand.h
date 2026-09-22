// SPDX-License-Identifier: MIT
//
// FilterCommand - P0-5.8 (2026-09-10), P3.1.1 (2026-09-22)
//
// QUndoCommand for filter operations.
//   Stores before/after image as cv::Mat (clone) + filter name for text.
//   redo() re-applies filter, undo() restores before-image.
//
// Usage (P0-5 default):
//   QUndoStack *stack = m_undoStack;
//   stack->push(new filter::FilterCommand(m_current, kind, filterName(kind)));
//
// P3.1.1 (2026-09-22):
//   - New ctor that takes a pre-built FilterStrategy. Used by FilterDialog
//     after the user adjusts parameter sliders — the dialog already owns a
//     strategy with the user-edited params, no need to recreate with defaults.
//   - Old ctor (kind-based) is kept for backward compatibility and tests.
//   - The redo pattern (factory pattern, see FilterCommand.cpp comment) is
//     unchanged: caller does not pre-setCurrentImage; FilterCommand applies
//     once in ctor and stores m_after for redo().
//
#pragma once

#include <QUndoCommand>
#include <QPointer>
#include <opencv2/core.hpp>
#include "FilterStrategy.h"

class ImageWindow;

namespace filter {

class FilterCommand : public QUndoCommand
{
public:
    // ctor (P0-5.8 default): takes current image, filter kind, and applies
    //   filter to produce after-image. Strategy created internally with
    //   default params.
    //   before-image = input image (cloned)
    //   after-image  = filter(input)
    //   host: ImageWindow for replaceCurrentImage on undo
    FilterCommand(ImageWindow *host, const cv::Mat &before, FilterKind kind,
                  const QString &text, QUndoCommand *parent = nullptr);

    // P3.1.1 (2026-09-22): ctor accepting a caller-provided strategy. Used by
    //   FilterDialog so the user's slider/picker params are applied (instead
    //   of recreating a strategy with default values).
    //   strategy pointer is NOT owned by FilterCommand; it must outlive the
    //   ctor call (the dialog owns it; ctor completes before dialog returns).
    FilterCommand(ImageWindow *host, const cv::Mat &before,
                  FilterStrategy *strategy, const QString &text,
                  QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    FilterKind kind() const { return m_kind; }

private:
    QPointer<ImageWindow> m_host;
    cv::Mat     m_before;
    cv::Mat     m_after;
    FilterKind  m_kind;
    bool        m_firstRedo = true;
};

} // namespace filter