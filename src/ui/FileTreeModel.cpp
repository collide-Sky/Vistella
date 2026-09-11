#include "FileTreeModel.h"
#include "../core/FileExtensionRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QIcon>

FileTreeModel::FileTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    // invisible root - 不暴露给 view
    FileTreeNodeData rootData;
    rootData.fileName     = QStringLiteral("<root>");
    rootData.absolutePath = QString();
    rootData.isDir        = true;
    m_root = new FileTreeItem(rootData, nullptr);
}

FileTreeModel::~FileTreeModel()
{
    delete m_root;
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!m_root || column != 0 || row < 0) return QModelIndex();
    FileTreeItem *parentItem = parent.isValid() ? static_cast<FileTreeItem *>(parent.internalPointer())
                                                : m_root;
    FileTreeItem *childItem = parentItem->child(row);
    if (!childItem) return QModelIndex();
    return createIndex(row, column, childItem);
}

QModelIndex FileTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return QModelIndex();
    FileTreeItem *childItem  = static_cast<FileTreeItem *>(index.internalPointer());
    if (!childItem) return QModelIndex();
    FileTreeItem *parentItem = childItem->parentItem();
    if (!parentItem || parentItem == m_root) return QModelIndex();
    return createIndex(parentItem->rowInParent(), 0, parentItem);
}

int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_root) return 0;
    FileTreeItem *p = parent.isValid() ? static_cast<FileTreeItem *>(parent.internalPointer())
                                       : m_root;
    return p->childCount();
}

int FileTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    FileTreeItem *item = static_cast<FileTreeItem *>(index.internalPointer());
    if (!item) return QVariant();
    const FileTreeNodeData &d = item->data();

    switch (role) {
    case Qt::DisplayRole:
        return d.fileName;
    case Qt::DecorationRole: {
        // 直接返 QIcon (QTreeView 走 DecorationRole 拿 QIcon 自动画)
        // 用 static QFileIconProvider 避免每次构造
        static QFileIconProvider s_iconProv;
        if (d.isDir) {
            return s_iconProv.icon(QFileIconProvider::Folder);
        }
        return s_iconProv.icon(QFileInfo(d.absolutePath));
    }
    case PathRole:
        return d.absolutePath;
    case IsDirRole:
        return d.isDir;
    case ModuleIdRole:
        return d.moduleId;
    default:
        return QVariant();
    }
}

bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    if (!m_root) return false;
    FileTreeItem *p = parent.isValid() ? static_cast<FileTreeItem *>(parent.internalPointer())
                                       : m_root;
    // 顶层 invisible root: 用户没设 rootPath, 0 children
    if (p == m_root) {
        return m_root->childCount() > 0;
    }
    // 目录: 永远 hasChildren (lazy load 用占位 "..." 触发)
    if (p->data().isDir) return true;
    return false;
}

Qt::ItemFlags FileTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void FileTreeModel::setRootPath(const QString &path)
{
    beginResetModel();
    removeAllChildren(m_root);
    m_rootPath.clear();

    if (path.isEmpty()) {
        endResetModel();
        return;
    }
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        endResetModel();
        return;
    }
    m_rootPath = fi.absoluteFilePath();

    // 根节点 (顶层 Item, parent = m_root)
    FileTreeNodeData rd;
    rd.fileName     = fi.fileName().isEmpty() ? fi.absoluteFilePath() : fi.fileName();
    rd.absolutePath = fi.absoluteFilePath();
    rd.isDir        = true;
    rd.iconKey      = QStringLiteral("Dir");
    rd.supportedExt = true;
    auto *rootItem = new FileTreeItem(rd, m_root);
    m_root->appendChild(rootItem);

    // 占位 child (lazy load)
    FileTreeNodeData ph;
    ph.fileName = QStringLiteral("...");
    rootItem->appendChild(new FileTreeItem(ph, rootItem));

    endResetModel();
}

void FileTreeModel::expand(const QModelIndex &index)
{
    if (!index.isValid()) return;
    FileTreeItem *item = static_cast<FileTreeItem *>(index.internalPointer());
    if (!item) return;
    if (!item->data().isDir) return;
    if (item->lazyLoaded()) return;
    if (!item->hasLazyPlaceholder()) return;

    // 2026-09-02 修复: 删占位必须用 beginRemoveRows/endRemoveRows 通知 Qt
    //   旧版直接 removeAllChildren 改 m_children, view 不知道 → 视觉上像折叠
    const QModelIndex parentIdx = indexForItem(item);
    beginRemoveRows(parentIdx, 0, 0);   // 占位是唯一一个 child
    item->removeAllChildren();
    endRemoveRows();

    // 真扫
    scanChildren(item);

    item->setLazyLoaded(true);
}

void FileTreeModel::scanChildren(FileTreeItem *parentItem)
{
    if (!parentItem) return;
    const QString dirPath = parentItem->data().absolutePath;
    if (dirPath.isEmpty()) return;
    QDir d(dirPath);
    if (!d.exists()) return;

    // 收集
    struct Entry {
        QString name;
        QString path;
        bool    isDir;
        qint64  size;
        QDateTime mtime;
    };
    QList<Entry> dirs, files;

    const QFileInfoList dList = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : dList) {
        dirs.append({ fi.fileName(), fi.absoluteFilePath(), true, 0, fi.lastModified() });
    }

    const QFileInfoList fList = d.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : fList) {
        // 决策 5: 不在扩展名表里的文件不显示
        if (!isFileSupported(fi.absoluteFilePath())) continue;
        files.append({ fi.fileName(), fi.absoluteFilePath(), false,
                       fi.size(), fi.lastModified() });
    }

    const int total = dirs.size() + files.size();
    if (total == 0) return;

    // 2026-09-02 修复: beginInsertRows 的 parent 必须是 parentItem 的真实 QModelIndex
    //   旧版用 QModelIndex() (invalid) 让 Qt 以为根全部受影响 → 视觉抖动 + 偶发折叠
    const QModelIndex parentIdx = indexForItem(parentItem);
    beginInsertRows(parentIdx, 0, total - 1);

    for (const Entry &e : dirs) {
        FileTreeNodeData nd;
        nd.fileName     = e.name;
        nd.absolutePath = e.path;
        nd.isDir        = true;
        nd.iconKey      = QStringLiteral("Dir");
        nd.lastModified = e.mtime;
        nd.supportedExt = true;
        auto *item = new FileTreeItem(nd, parentItem);
        parentItem->appendChild(item);
        // 占位 child (让 QTreeView 显示 expand 三角)
        FileTreeNodeData ph;
        ph.fileName = QStringLiteral("...");
        item->appendChild(new FileTreeItem(ph, item));
    }
    for (const Entry &e : files) {
        FileTreeNodeData nd;
        nd.fileName     = e.name;
        nd.absolutePath = e.path;
        nd.isDir        = false;
        nd.iconKey      = QStringLiteral("File");
        nd.fileSize     = e.size;
        nd.lastModified = e.mtime;
        nd.moduleId     = FileExtensionRegistry::moduleIdForExtension(e.path);
        nd.supportedExt = true;
        auto *item = new FileTreeItem(nd, parentItem);
        parentItem->appendChild(item);
    }

    endInsertRows();
}

void FileTreeModel::removeAllChildren(FileTreeItem *parentItem)
{
    if (!parentItem || parentItem->childCount() == 0) return;
    // 2026-09-02 修复: 这里不调 begin/end rows, 由调用方根据场景包
    //   - setRootPath: 用 beginResetModel/endResetModel (整 model 重置)
    //   - expand():    用 beginRemoveRows/endRemoveRows 删占位
    //   嵌套在 beginResetModel 里的 beginRemoveRows 会被 Qt 视作非法
    parentItem->removeAllChildren();
}

QModelIndex FileTreeModel::indexForItem(FileTreeItem *item) const
{
    if (!item || item == m_root) return QModelIndex();
    const int row = item->rowInParent();
    if (row < 0) return QModelIndex();
    return createIndex(row, 0, item);
}

bool FileTreeModel::isFileSupported(const QString &filePath) const
{
    // 决策 5: 用 FileExtensionRegistry 判断
    return FileExtensionRegistry::isSupportedExtension(filePath);
}

QString FileTreeModel::pathForIndex(const QModelIndex &index)
{
    FileTreeItem *it = itemForIndex(index);
    return it ? it->data().absolutePath : QString();
}

bool FileTreeModel::isDirForIndex(const QModelIndex &index)
{
    FileTreeItem *it = itemForIndex(index);
    return it ? it->data().isDir : false;
}

FileTreeItem *FileTreeModel::itemForIndex(const QModelIndex &index)
{
    if (!index.isValid()) return nullptr;
    return static_cast<FileTreeItem *>(index.internalPointer());
}
