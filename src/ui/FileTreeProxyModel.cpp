// SPDX-License-Identifier: MIT
//
// FileTreeProxyModel -- the filter in front of QFileSystemModel.
// See FileTreeProxyModel.h for the contract and for the rule that this
// implementation has to follow.

#include "FileTreeProxyModel.h"

#include "../core/FileExtensionRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>

FileTreeProxyModel::FileTreeProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    // The filter has to be applied to every level, not only to the top
    // level: a file that is not supported has to disappear in every
    // directory.
    setRecursiveFilteringEnabled(true);
    setDynamicSortFilter(true);
}

void FileTreeProxyModel::setSourceModel(QAbstractItemModel* model)
{
    QSortFilterProxyModel::setSourceModel(model);

    // The directories that have been read belong to the old source
    // model, so they are dropped together with it.
    m_loadedDirs.clear();

    // The file system model reads the directories in a worker thread;
    // the signal tells us which directories are already read.
    if (auto* fs = qobject_cast<QFileSystemModel*>(model)) {
        connect(fs, &QFileSystemModel::directoryLoaded,
                this, &FileTreeProxyModel::onDirectoryLoaded);
    }
}

// ---------------------------------------------------------------------------
//  the lazy fetch
//
//  QSortFilterProxyModel does NOT need to be helped here:
//  QAbstractProxyModel::canFetchMore()/fetchMore() map the index to the
//  source themselves before they forward it, so the view can expand a
//  row as usual and the default implementation is correct.
// ---------------------------------------------------------------------------

void FileTreeProxyModel::fetchDirectory(const QModelIndex& index)
{
    if (!index.isValid()) return;
    QAbstractItemModel* src = sourceModel();
    if (!src) return;
    const QModelIndex srcIdx = mapToSource(index);
    if (!srcIdx.isValid()) return;
    if (src->canFetchMore(srcIdx)) {
        src->fetchMore(srcIdx);
    }
}

bool FileTreeProxyModel::fetchDirectory(const QString& path)
{
    auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return false;

    const QModelIndex srcIdx = fs->index(path);
    if (!srcIdx.isValid()) return false;
    if (!fs->canFetchMore(srcIdx)) return false;

    fs->fetchMore(srcIdx);
    return true;
}

bool FileTreeProxyModel::isDirectoryLoaded(const QString& path) const
{
    return m_loadedDirs.contains(QDir::cleanPath(path));
}

// ---------------------------------------------------------------------------
//  the path is the stable identity
// ---------------------------------------------------------------------------

QModelIndex FileTreeProxyModel::indexForPath(const QString& path) const
{
    auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return QModelIndex();
    if (path.isEmpty()) return QModelIndex();

    const QString clean = QDir::cleanPath(path);

    // The source model knows the path -- it is the authority for what
    // exists on disk.
    const QModelIndex srcIdx = fs->index(clean);
    if (!srcIdx.isValid()) return QModelIndex();

    // The proxy index is derived on demand and must not be held across a
    // change of the source model.
    return mapFromSource(srcIdx);
}

// ---------------------------------------------------------------------------
//  the filter
// ---------------------------------------------------------------------------

bool FileTreeProxyModel::filterAcceptsRow(int row,
                                           const QModelIndex& parent) const
{
    const QAbstractItemModel* src = sourceModel();
    if (!src) return false;

    const QModelIndex srcIdx = src->index(row, 0, parent);
    if (!srcIdx.isValid()) return false;

    const QString path = src->data(srcIdx, QFileSystemModel::FilePathRole).toString();

    // A directory always passes: the user has to be able to walk into
    // it, even when every file inside it is filtered out.
    if (QFileInfo(path).isDir()) return true;

    // A file only passes when the registry knows its extension.
    return FileExtensionRegistry::isSupportedExtension(path);
}

// ---------------------------------------------------------------------------
//  the expand arrow
// ---------------------------------------------------------------------------

bool FileTreeProxyModel::hasChildren(const QModelIndex& parent) const
{
    const QAbstractItemModel* src = sourceModel();
    if (!src) return false;

    // The top level: the rows of the root path.
    if (!parent.isValid()) {
        return QSortFilterProxyModel::rowCount() > 0;
    }

    // A file never has children.
    if (!isDirForIndex(parent)) return false;

    const QModelIndex srcIdx = mapToSource(parent);
    if (!srcIdx.isValid()) return false;
    const QString path = src->data(srcIdx, QFileSystemModel::FilePathRole).toString();

    if (m_loadedDirs.contains(QDir::cleanPath(path))) {
        // The directory has been read: the arrow shows only when there
        // really is something inside (the count is the FILTERED count,
        // so a directory whose files are all filtered out has no arrow
        // either).
        return QSortFilterProxyModel::rowCount(parent) > 0;
    }

    // Not read yet: the arrow is optimistic. The read happens in the
    // background, and onDirectoryLoaded() corrects the arrow afterwards.
    return true;
}

void FileTreeProxyModel::onDirectoryLoaded(const QString& path)
{
    m_loadedDirs.insert(QDir::cleanPath(path));

    // Tell the view that the decoration state of that directory may
    // have changed (the expand arrow of an empty directory disappears).
    if (auto* fs = qobject_cast<QFileSystemModel*>(sourceModel())) {
        const QModelIndex srcIdx = fs->index(path);
        if (srcIdx.isValid()) {
            const QModelIndex proxyIdx = mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                emit dataChanged(proxyIdx, proxyIdx, {Qt::DecorationRole});
            }
        }
    }

    emit directoryRead(path);
}

// ---------------------------------------------------------------------------
//  the helpers
// ---------------------------------------------------------------------------

QString FileTreeProxyModel::pathForIndex(const QModelIndex& index)
{
    if (!index.isValid()) return QString();
    return index.data(QFileSystemModel::FilePathRole).toString();
}

bool FileTreeProxyModel::isDirForIndex(const QModelIndex& index)
{
    if (!index.isValid()) return false;
    const QString path = index.data(QFileSystemModel::FilePathRole).toString();
    if (path.isEmpty()) return false;
    return QFileInfo(path).isDir();
}

QString FileTreeProxyModel::moduleIdForIndex(const QModelIndex& index)
{
    if (!index.isValid()) return QString();
    if (isDirForIndex(index)) return QString();
    const QString path = index.data(QFileSystemModel::FilePathRole).toString();
    if (path.isEmpty()) return QString();
    return FileExtensionRegistry::moduleIdForExtension(path);
}
