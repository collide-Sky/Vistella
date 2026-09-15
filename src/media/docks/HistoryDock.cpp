// SPDX-License-Identifier: MIT
//
// HistoryDock implementation - P0-8.1 (2026-09-15)
//
// 详见 HistoryDock.h 头注释
//
#include "HistoryDock.h"
#include "logger.h"

#include <QListView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QUndoStack>
#include <QVBoxLayout>

namespace docks {

HistoryDock::HistoryDock(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_model = new QStandardItemModel(this);
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // PS 同款: 单击就跳到那一步 (主流做法)
    connect(m_list, &QListView::clicked,
            this, &HistoryDock::onItemClicked);
    // 双击/回车也跳 (备选)
    connect(m_list, &QListView::activated,
            this, &HistoryDock::onItemActivated);

    layout->addWidget(m_list);

    // maxHeight 限制 (跟 PropertiesDock 同款, 防止 1316 bug 复发)
    setMaximumHeight(200);
}

HistoryDock::~HistoryDock() = default;

void HistoryDock::setUndoStack(QUndoStack* stack)
{
    if (m_undo == stack) return;
    if (m_undo) {
        disconnect(m_undo, nullptr, this, nullptr);
    }
    m_undo = stack;
    if (m_undo) {
        connect(m_undo, &QUndoStack::indexChanged,
                this, &HistoryDock::onStackIndexChanged);
    }
    rebuildFromStack();
    LOG_DEBUG("[HistoryDock] setUndoStack: {}", stack ? "yes" : "null");
}

void HistoryDock::rebuildFromStack()
{
    ++m_syncing;   // guard: 防止 setCurrentIndex 触发 clicked -> setIndex 死循环
    m_model->clear();
    if (!m_undo) {
        --m_syncing;
        return;
    }
    const int total = m_undo->count();
    const int curIdx = m_undo->index();
    for (int i = 0; i < total; ++i) {
        const QString text = m_undo->text(i);
        auto* item = new QStandardItem(text.isEmpty()
                                       ? QStringLiteral("(未命名)")
                                       : text);
        item->setEditable(false);
        // row >= index 的行是 redo 待执行 (灰色)
        // row <  index 的行是已 done (黑色)
        if (i > curIdx) {
            item->setForeground(QColor(160, 160, 160));  // 灰色
        }
        m_model->appendRow(item);
    }
    --m_syncing;
    syncCurrentRow();
}

void HistoryDock::syncCurrentRow()
{
    if (!m_undo || m_model->rowCount() == 0) return;
    ++m_syncing;
    int row = m_undo->index();
    if (row < 0) row = 0;
    if (row >= m_model->rowCount()) row = m_model->rowCount() - 1;
    const QModelIndex idx = m_model->index(row, 0);
    m_list->setCurrentIndex(idx);
    m_list->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    --m_syncing;
}

void HistoryDock::onStackIndexChanged(int idx)
{
    Q_UNUSED(idx);
    if (m_syncing > 0) return;
    rebuildFromStack();
}

void HistoryDock::onItemActivated(const QModelIndex& idx)
{
    if (!m_undo || idx.row() < 0) return;
    if (m_syncing > 0) return;
    if (idx.row() == m_undo->index()) return;
    LOG_DEBUG("[HistoryDock] onItemActivated: row={}", idx.row());
    m_undo->setIndex(idx.row());
}

void HistoryDock::onItemClicked(const QModelIndex& idx)
{
    // 单击 == 双击 (PS 同款)
    onItemActivated(idx);
}

int HistoryDock::rowCount() const
{
    return m_model ? m_model->rowCount() : 0;
}

int HistoryDock::currentRow() const
{
    return m_list ? m_list->currentIndex().row() : -1;
}

QString HistoryDock::textAt(int row) const
{
    if (!m_model) return QString();
    if (row < 0 || row >= m_model->rowCount()) return QString();
    return m_model->item(row)->text();
}

} // namespace docks