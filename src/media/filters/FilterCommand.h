// SPDX-License-Identifier: MIT
//
// FilterCommand
//
// QUndoCommand for filter operations.
//
//   The filter acts on the ACTIVE LAYER's pixels, never on the composite:
//   filtering the composite and writing the result back into the base layer
//   composites the upper layers a second time and flattens the layer stack.
//
// Usage:
//   QUndoStack *stack = m_undoStack;
//   stack->push(new filter::FilterCommand(host, kind, filterName));
//
//   - ctor taking a pre-built FilterStrategy. Used by FilterDialog after the
//     user adjusts parameter sliders — the dialog already owns a strategy with
//     the user-edited params, no need to recreate with defaults.
//   - ctor (kind-based) creates the strategy internally with default params.
//
//   The layer index is captured at ctor time, so undo still restores the right
//   layer even if the user selects another one in the meantime.
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
    // ctor (P0-5.8 default): takes the host and the filter kind, applies the
    //   filter to the active layer to produce the after-image.
    //   Strategy created internally with default params.
    FilterCommand(ImageWindow *host, FilterKind kind,
                  const QString &text, QUndoCommand *parent = nullptr);

    // ctor accepting a caller-provided strategy. Used by
    //   FilterDialog so the user's slider/picker params are applied (instead
    //   of recreating a strategy with default values).
    //   strategy pointer is NOT owned by FilterCommand; it must outlive the
    //   ctor call (the dialog owns it; ctor completes before dialog returns).
    FilterCommand(ImageWindow *host, FilterStrategy *strategy,
                  const QString &text, QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    FilterKind kind() const { return m_kind; }
    //  滤镜落在哪个图层 (ctor 时捕获的 active layer)
    int layerIndex() const { return m_layerIndex; }

private:
    void applyToLayer(const cv::Mat &pixels);

    QPointer<ImageWindow> m_host;
    int         m_layerIndex = -1;
    cv::Mat     m_before;
    cv::Mat     m_after;
    FilterKind  m_kind;
};

} // namespace filter
