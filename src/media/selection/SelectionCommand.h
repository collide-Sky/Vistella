// SPDX-License-Identifier: MIT
//
// SelectionCommand - P0-4.4 (2026-09-10)
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

#include <QUndoCommand>
#include <QPointer>
#include <QImage>

namespace selection {

class SelectionModel;

class SelectionCommand : public QUndoCommand
{
public:
    enum class Op { SelectAll, Deselect, Invert };

    SelectionCommand(SelectionModel* model, Op op, QUndoCommand* parent = nullptr);

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
