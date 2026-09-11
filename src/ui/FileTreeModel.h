#ifndef FILETREEMODEL_H
#define FILETREEMODEL_H

// =============================================================
// FileTreeModel — 文件树 QAbstractItemModel (2026-09-02 决策 5 重构)
//
// 职责:
//   1. 把 FileTreeItem 树暴露给 QTreeView
//   2. lazy load: 目录第一次 expand 才真扫 (QTreeView::expanded 触发)
//   3. 决策 5 扩展名过滤: 不在表里的文件不显示
//   4. 角色分发: DisplayRole / DecorationRole / UserRole (绝对路径) / UserRole+1 (isDir)
//
// 设计:
//   - 维护一个 invisible root (m_root, 不暴露给 view), 所有顶层节点的 parent
//   - setRootPath(path): 清空 → 建根节点 → beginResetModel/endResetModel
//   - expand(index): 调 QTreeView 展开前由 view 触发; 实际真扫在 lazyLoad(Item)
//   - data(DisplayRole) 用 QFileIconProvider-style key, view 端用 QFileIconProvider 画
//     (避免 model 持有 QIcon, 跨平台一致)
// =============================================================

#include "FileTreeItem.h"

#include <QAbstractItemModel>
#include <QString>
#include <QHash>

class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit FileTreeModel(QObject *parent = nullptr);
    ~FileTreeModel() override;

    // ----- QAbstractItemModel -----
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int  rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int  columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // ----- 业务 API -----
    // 切换到指定目录, 清空旧内容
    void setRootPath(const QString &path);
    QString rootPath() const { return m_rootPath; }

    // 触发目录节点的 lazy load (QTreeView::expanded 信号接这里)
    //   如果没占位 child / 已加载过, no-op
    void expand(const QModelIndex &index);

    // 拿 item 的绝对路径 / isDir (给 dock / 业务调用)
    static QString   pathForIndex(const QModelIndex &index);
    static bool      isDirForIndex(const QModelIndex &index);

    // 把 QModelIndex 还原成 FileTreeItem* (给 dock 内部用)
    static FileTreeItem *itemForIndex(const QModelIndex &index);

    // 自定义 role (跟 FileTreeNodeData 字段对应)
    enum Role {
        PathRole = Qt::UserRole + 1,   // 绝对路径
        IsDirRole,                     // bool: true=目录
        ModuleIdRole,                  // QString: 命中的模块 id, 目录为空
    };

private slots:
    // 占位实现, 留 hooks 给以后 (e.g. file watcher 增量刷新)

private:
    FileTreeItem *createItem(const QString &path, bool isDir, FileTreeItem *parent);
    void scanChildren(FileTreeItem *parentItem);
    void removeAllChildren(FileTreeItem *parentItem);
    bool isFileSupported(const QString &filePath) const;

    // 给定 FileTreeItem 算出 QModelIndex (invisible root 返回 invalid)
    //   用 createIndex + item->rowInParent(), 是 QAbstractItemModel 标准做法
    QModelIndex indexForItem(FileTreeItem *item) const;

    FileTreeItem *m_root = nullptr;     // invisible root
    QString       m_rootPath;
};

#endif // FILETREEMODEL_H
