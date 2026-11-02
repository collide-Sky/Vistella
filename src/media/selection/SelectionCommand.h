// SPDX-License-Identifier: MIT
//
// SelectionCommand
//
// QUndoCommand for selection operations (Select All / Deselect / Inverse).
//   Stores before/after mask as 1-channel QImage; redo applies the op, undo
//   restores the previous mask. Compatible with QUndoStack (per-ImageWindow).
//
// F-N: RectSelect/Lasso/MagicWand gestures do NOT push this command — the
// tool pushes via SelectionModel::setMask (gesture-driven, not undoable per
// intermediate frame). Only the high-level ops (Select All/Deselect/Inverse)
// get undo/redo support.
#pragma once

#include "../UndoStack.h"
#include <QUndoCommand>
#include <QPointer>
#include <QImage>

namespace selection {

class SelectionModel;

class SelectionCommand : public QUndoCommand,
                     public vistella::IUndoMemoryFootprint
{
public:
    enum class Op {
        SelectAll,
        Deselect,
        Invert,
        // M1-7: a gesture (lasso / marquee / magic wand) set a new mask.
        //   The before and after masks are stored verbatim and restored
        //   verbatim -- no op is re-run. That makes redo idempotent, which
        //   the op-based variants are not (Invert double-inverted).
        SetMask
    };

    SelectionCommand(SelectionModel* model, Op op, QUndoCommand* parent = nullptr);

    // M1-7: a gesture that replaced the selection.
    //
    //   `after` is the mask the tool already applied to the model; the
    //   command restores `before` on undo and `after` on redo.
    static SelectionCommand* makeSetMask(SelectionModel* model,
                                         const QImage& before,
                                         const QImage& after,
                                         const QString& text = QString());

    // M1-7: bytes held by this command (the two selection masks).
    //
    //   A selection mask is 1 byte per pixel, so a 24 MP document costs
    //   24 MB per mask -- 48 MB per entry. It has to take part in the
    //   budget too.
    size_t memoryFootprint() const override {
        return static_cast<size_t>(m_before.sizeInBytes())
             + static_cast<size_t>(m_after.sizeInBytes());
    }
    void undo() override;
    void redo() override;

private:
    QPointer<SelectionModel> m_model;
    QImage                   m_before;
    QImage                   m_after;
    Op                       m_op;
    bool                     m_firstRedo = true;   // skip push's first redo
};

} // namespace selection
