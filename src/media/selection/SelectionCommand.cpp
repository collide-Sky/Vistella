// SPDX-License-Identifier: MIT
//
// SelectionCommand implementation
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

SelectionCommand* SelectionCommand::makeSetMask(SelectionModel* model,
                                                 const QImage& before,
                                                 const QImage& after,
                                                 const QString& text)
{
    auto* cmd = new SelectionCommand(model, Op::SetMask);
    cmd->m_before = before;
    cmd->m_after  = after;
    cmd->setText(text.isEmpty() ? QStringLiteral("Selection") : text);
    return cmd;
}

void SelectionCommand::redo()
{
    if (!m_model) return;

    if (m_op == Op::SetMask) {
        // Restore the after-state VERBATIM.
        //
        //   The op-based variants re-ran the op on every redo, which is
        //   not idempotent: a second redo of `Invert` double-inverted the
        //   selection. Restoring the stored state has no such problem.
        if (!m_after.isNull()) {
            m_model->setMask(m_after, SelectionModel::Mode::Replace);
        } else {
            m_model->clear();
        }
        LOG_INFO("[SelectionCommand] redo: SetMask bbox={}x{}",
                 m_model->boundingRect().width(), m_model->boundingRect().height());
        return;
    }

    switch (m_op) {
    case Op::SelectAll: m_model->selectAll(); break;
    case Op::Deselect:  m_model->clear();     break;
    case Op::Invert:    m_model->invert();    break;
    case Op::SetMask:   break;   // handled above
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
