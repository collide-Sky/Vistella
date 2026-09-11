// SPDX-License-Identifier: MIT
//
// FilterCommand - P0-5.8 (2026-09-10)
//
// QUndoCommand for filter operations.
//   Stores before/after image as cv::Mat (clone) + filter name for text.
//   redo() re-applies filter, undo() restores before-image.
//
// Usage:
//   QUndoStack *stack = m_undoStack;
//   stack->push(new filter::FilterCommand(m_current, kind, filterName(kind)));
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
    // ctor: takes current image, filter kind, and applies filter to produce after-image.
    //   before-image = input image (cloned)
    //   after-image  = filter(input)
    //   host: ImageWindow for replaceCurrentImage on undo
    FilterCommand(ImageWindow *host, const cv::Mat &before, FilterKind kind,
                  const QString &text, QUndoCommand *parent = nullptr);

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
