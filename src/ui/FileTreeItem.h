#ifndef FILETREEITEM_H
#define FILETREEITEM_H

// =============================================================
// FileTreeItem — 文件树节点 (2026-09-02 决策 5 重构)
//
// 跟 FileTreeNodeData 分开:
//   - 自己只管树结构 (parent / children / row / lazy load 状态)
//   - payload 在 FileTreeNodeData
//
// 设计原则 (用户原话: "树的节点, 和节点数据分开管理, 目前没有复杂操作需求,
//   但是为了以后这个文件树方便扩展, 改成 modelview 会更好"):
//   - 后续加 drag-drop / 拖入新文件 / 自定义右键菜单 / 文件状态徽标 都不需要改 model
//   - 后续加缩略图 / metadata cache 直接扩 FileTreeNodeData 字段, model 不用动
// =============================================================

#include "FileTreeNodeData.h"

#include <QList>
#include <QString>

class FileTreeItem
{
public:
    explicit FileTreeItem(const FileTreeNodeData &data, FileTreeItem *parent = nullptr);
    ~FileTreeItem();

    // ----- 父子关系 (跟 QAbstractItemModel::index/parent 对应) -----
    FileTreeItem *child(int row) const;
    int           childCount() const;
    int           rowInParent() const;        // 在 parent->children 里的 index, 顶层 = 0
    FileTreeItem *parentItem() const;

    void appendChild(FileTreeItem *c);
    void removeAllChildren();                 // 重新扫描前清空

    // ----- payload -----
    const FileTreeNodeData &data() const { return m_data; }
    FileTreeNodeData       &data()       { return m_data; }
    void setData(const FileTreeNodeData &d) { m_data = d; }

    // ----- lazy load (目录默认占位 child, 第一次 expand 才真扫) -----
    bool lazyLoaded() const { return m_lazyLoaded; }
    void setLazyLoaded(bool v) { m_lazyLoaded = v; }
    bool hasLazyPlaceholder() const;          // 占位 child ("...") 是否还在

private:
    FileTreeNodeData      m_data;
    FileTreeItem         *m_parent = nullptr;
    QList<FileTreeItem*>  m_children;
    bool                  m_lazyLoaded = false;
};

#endif // FILETREEITEM_H
