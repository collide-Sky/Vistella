// SPDX-License-Identifier: MIT
//
// HistoryDock - P0-8.1 (2026-09-15)
//
// PS 风格历史面板 (RightPanelDock 第 6 tab):
//   - 显示 QUndoStack 所有 command (text() = "添加文字" / "自由变换" / "滤镜: Blur" 等)
//   - 点击某一项 -> undoStack->setIndex(row) 跳到指定步骤
//   - 实时同步: QUndoStack::indexChanged -> 重建 model + 选中当前 row
//
// 设计:
//   - QListView + QStandardItemModel
//   - current row 高亮 = undoStack->index() (已经 done 的最后一步)
//   - row < index 是已 done 的 (正常色), row >= index 是 redo 待执行的 (灰色)
//   - 用户点击 row != current -> 跳到那一步 (setIndex 触发 undo/redo)
//
#pragma once

#include <QWidget>
#include <QPointer>

class QListView;
class QStandardItemModel;
class QUndoStack;
class QModelIndex;

namespace docks {

class HistoryDock : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryDock(QWidget* parent = nullptr);
    ~HistoryDock() override;

    // F-O (2026-09-10) test accessors
    int     rowCount() const;
    int     currentRow() const;
    QString textAt(int row) const;

    // 绑定 QUndoStack (ImageWindow::m_undoStack)
    void setUndoStack(QUndoStack* stack);
    QUndoStack* undoStack() const { return m_undo; }

private slots:
    // QUndoStack::indexChanged -> 重建 model + 同步选中
    void onStackIndexChanged(int idx);
    // QListView::activated (双击/回车) -> setIndex(row)
    void onItemActivated(const QModelIndex& idx);
    // QListView::clicked (单击) -> setIndex(row), PS 同款 (单击也跳)
    void onItemClicked(const QModelIndex& idx);

private:
    void rebuildFromStack();
    void syncCurrentRow();

    QListView*          m_list   = nullptr;
    QStandardItemModel* m_model  = nullptr;
    QPointer<QUndoStack> m_undo;

    // re-entrancy guard: 防止 onStackIndexChanged -> rebuildFromStack -> setCurrentRow
    //   时反过来触发 onItemClicked 再 setIndex 死循环
    int m_syncing = 0;
};

} // namespace docks