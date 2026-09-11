// SPDX-License-Identifier: MIT
//
// SelectionCommand implementation - P0-4.4 (2026-09-10)
//
#include "SelectionCommand.h"
#include "SelectionModel.h"
#include "logger.h"

namespace selection {

SelectionCommand::SelectionCommand(SelectionModel* model, Op op, QUndoCommand* parent)
    : QUndoCommand(parent), m_model(model), m_op(op)
{
    if (m_model) {
        m_before = m_model->mask();
    }
    setText(QString::fromUtf8(
        op == Op::SelectAll ? "Select All"
        : op == Op::Deselect ? "Deselect"
        : "Inverse"));
}

void SelectionCommand::redo()
{
    if (!m_model) return;
    if (m_firstRedo) {
        // push() triggers redo once; do the actual op here
        m_firstRedo = false;
    } else {
        // subsequent redo (Ctrl+Y) restores after-state
    }
    switch (m_op) {
    case Op::SelectAll: m_model->selectAll(); break;
    case Op::Deselect:  m_model->clear();     break;
    case Op::Invert:    m_model->invert();    break;
    }
    m_after = m_model->mask();
    LOG_INFO("[SelectionCommand] redo: op={} bbox={}x{}",
             static_cast<int>(m_op), m_model->boundingRect().width(), m_model->boundingRect().height());
}

void SelectionCommand::undo()
{
    if (!m_model) return;
    if (!m_before.isNull()) {
        m_model->setMask(m_before, SelectionModel::Mode::Replace);
    } else {
        m_model->clear();
    }
    LOG_INFO("[SelectionCommand] undo: op={}", static_cast<int>(m_op));
}

} // namespace selection
