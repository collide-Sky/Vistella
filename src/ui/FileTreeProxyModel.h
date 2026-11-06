// SPDX-License-Identifier: MIT
//
// FileTreeProxyModel -- the filter in front of QFileSystemModel.
//
// Why a proxy and not a hand written model:
//
//   The hand written FileTreeModel (deleted in M2-7b) read every
//   directory on the UI thread -- QDir::entryInfoList twice (once for
//   the directories, once for the files) plus a stat() per file -- and
//   it always answered "yes" to hasChildren() for a directory, so an
//   empty directory kept its expand arrow forever.
//
//   QFileSystemModel reads the directories in a worker thread, so the
//   tree no longer blocks the UI. The proxy adds the two things the
//   product needs on top of it:
//
//     1. the extension filter: a file whose extension the
//        FileExtensionRegistry does not know is hidden. A directory
//        always passes -- the user has to be able to walk into it,
//        even when every file inside it is filtered out.
//
//     2. the expand arrow: a directory that has been read and turned
//        out to be empty has NO arrow. Until the directory has been
//        read the arrow is optimistic, because the read happens in the
//        background and the user must be able to open the directory
//        while it is still loading.
//
//   The set of directories that have been read comes from the
//   QFileSystemModel::directoryLoaded signal.
//
// ---------------------------------------------------------------------------
//  THE ONE RULE OF THIS CLASS  (learned the hard way, see docs/HANDOFF-M2.md)
// ---------------------------------------------------------------------------
//
//  A QModelIndex of the PROXY may not survive a change of the source
//  model.
//
//     QSortFilterProxyModel keeps its row mapping in heap objects
//     ("Mapping") and stores a raw pointer to them in the internalPointer
//     of every proxy index it hands out. When the source model changes,
//     the proxy drops all of those objects and rebuilds them. A proxy
//     index that was taken before the change therefore points at freed
//     memory, and the next use of it crashes with
//
//         ASSERT: "it != source_index_mapping.constEnd()"
//         in qsortfilterproxymodel.cpp
//
//     QFileSystemModel changes constantly: every directory it reads in
//     its worker thread arrives as rowsInserted.
//
//     Consequence: the PATH is the only stable identity in this tree.
//     Everything in this class and in InfoTreeDock works with paths;
//     a proxy index is derived from a path when it is needed and
//     thrown away again immediately.
//
//  NOTE on the lazy fetch: QSortFilterProxyModel does NOT need to be
//  helped here. QAbstractProxyModel::canFetchMore()/fetchMore() map the
//  index to the source themselves before they forward it, so the default
//  implementation is correct and the view can expand a row as usual.
//  (An earlier version of this class overrode the two methods and
//  claimed the default was broken -- it was not, and the override only
//  repeated what the base class already did.)
//
#ifndef FILETREEPROXYMODEL_H
#define FILETREEPROXYMODEL_H

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

class FileTreeProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit FileTreeProxyModel(QObject* parent = nullptr);

    // ----- helpers for the views and the business code -----

    // The absolute path of an index (empty for an invalid index).
    static QString pathForIndex(const QModelIndex& index);
    // Whether the index points at a directory.
    static bool isDirForIndex(const QModelIndex& index);
    // The module id of the file (empty for a directory or an invalid
    // index).
    static QString moduleIdForIndex(const QModelIndex& index);

    // ----- the stable identity: the path -----

    // The proxy index of an absolute path, or an invalid index when the
    // path is not part of the tree (any more).
    //
    // The path is the stable identity of a tree entry -- see the rule in
    // the header comment. This method maps the path to a proxy index
    // ON DEMAND; the caller must not hold on to the result across a
    // change of the source model.
    QModelIndex indexForPath(const QString& path) const;

    // ----- custom role -----
    enum Role {
        ModuleIdRole = Qt::UserRole + 1,   // the module that handles the file
    };

    // The source model is the QFileSystemModel. It is set after the
    // proxy was created, so the connection to directoryLoaded is made
    // here and not in the constructor.
    void setSourceModel(QAbstractItemModel* model) override;

    // Whether the index has children. Public (as in QAbstractItemModel),
    // because the empty directory arrow is part of the contract of this
    // class and the tests have to be able to ask for it.
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;

    // Ask the file system model to read the directory of the index.
    //
    // QFileSystemModel is lazy: it reads a subdirectory only when
    // fetchMore() is called for it, and the read happens in a worker
    // thread. The view does that on its own when the user expands a row
    // -- this method is for the code that wants the contents without a
    // user action.
    void fetchDirectory(const QModelIndex& index);

    // Ask the file system model to read the directory of a PATH.
    // Returns false when the path is not a directory of this tree.
    //
    // This is the way to go: the path survives the change of the source
    // model that the read itself causes, a proxy index does not.
    bool fetchDirectory(const QString& path);

    // Whether the directory has already been read.
    bool isDirectoryLoaded(const QString& path) const;

signals:
    // A directory has been read by the file system model.
    // The view uses it to refresh the expand arrow.
    void directoryRead(const QString& path);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private slots:
    void onDirectoryLoaded(const QString& path);

private:
    // The directories that have already been read. Only for those the
    // expand arrow can be decided.
    QSet<QString> m_loadedDirs;
};

#endif // FILETREEPROXYMODEL_H
