#include "FileTreeItem.h"

FileTreeItem::FileTreeItem(const FileTreeNodeData &data, FileTreeItem *parent)
    : m_data(data)
    , m_parent(parent)
{
}

FileTreeItem::~FileTreeItem()
{
    qDeleteAll(m_children);
    m_children.clear();
}

FileTreeItem *FileTreeItem::child(int row) const
{
    if (row < 0 || row >= m_children.size()) return nullptr;
    return m_children.at(row);
}

int FileTreeItem::childCount() const
{
    return m_children.size();
}

int FileTreeItem::rowInParent() const
{
    if (!m_parent) return 0;
    return m_parent->m_children.indexOf(const_cast<FileTreeItem *>(this));
}

FileTreeItem *FileTreeItem::parentItem() const
{
    return m_parent;
}

void FileTreeItem::appendChild(FileTreeItem *c)
{
    if (!c) return;
    c->m_parent = this;
    m_children.append(c);
}

void FileTreeItem::removeAllChildren()
{
    qDeleteAll(m_children);
    m_children.clear();
}

bool FileTreeItem::hasLazyPlaceholder() const
{
    // 占位 child 数量 = 1 且 fileName == "..."
    if (m_children.size() != 1) return false;
    return m_children.first()->m_data.fileName == QStringLiteral("...");
}
