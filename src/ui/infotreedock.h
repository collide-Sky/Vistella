#ifndef INFOTREEDOCK_H
#define INFOTREEDOCK_H

#include <QDockWidget>
#include <QModelIndex>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class InfoTreeDock; }
class QTreeView;
class QFileSystemModel;
class FileTreeProxyModel;
QT_END_NAMESPACE

// =============================================================
// InfoTreeDock -- the information tree / generic project browser
//
//  Responsibilities:
//    1. setRootPath(path)  load the file tree of the given directory
//    2. the tree itself (QTreeView + QFileSystemModel + FileTreeProxyModel)
//    3. double click a file / select + press enter
//       -> emit openFileRequested(path)
//       MainWindow connects to it and routes through the MediaDispatcher
//
// M2-7b: QFileSystemModel + QSortFilterProxyModel
//   The old hand written FileTreeModel read every directory on the UI
//   thread (two full scans plus a stat() per file), and it answered
//   "yes" to hasChildren() for EVERY directory, so an empty directory
//   kept its expand arrow forever.
//   Now the directories are read in a worker thread by the
//   QFileSystemModel; the FileTreeProxyModel filters the extensions and
//   owns the expand arrow.
//
//   The tree is navigated by PATH, never by a stored proxy index -- a
//   proxy index does not survive a change of the source model. See the
//   rule in FileTreeProxyModel.h.
// =============================================================

class InfoTreeDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit InfoTreeDock(QWidget *parent = nullptr);
    ~InfoTreeDock() override;

    // Switch to the given directory as the root; an empty path clears
    // the content and rescans.
    void setRootPath(const QString &path);
    QString rootPath() const;

protected:
    // intercept the tree's key event -- onReturnPressed
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // The user double clicked a file in the tree / selected it and
    // pressed enter -> the absolute file path
    void openFileRequested(const QString &path);

private slots:
    // QTreeView signals
    void onActivated(const QModelIndex &index);
    void onReturnPressed();
    // After a directory has been read, refresh the expand arrow (an
    // empty directory must not have one).
    void onDirectoryRead(const QString &path);

private:
    // Take the root index of the view FRESH from the path.
    void updateRootIndex();
    // Expand / collapse a directory row.
    void toggleDirectory(const QModelIndex &index);

    Ui::InfoTreeDock *ui;
    QFileSystemModel *m_fsModel = nullptr;
    FileTreeProxyModel *m_proxy = nullptr;
    QString m_rootPath;
    // A cached proxy index. It is NOT used by the production code -- it
    // exists so that the break self-check
    // (tools/break_check_m2_7_filetree.py) can inject the defect
    // "the tree is navigated by a cached proxy index" and prove that the
    // regression tests catch it. A proxy index does not survive a change
    // of the source model, and the read of a directory is exactly such a
    // change -- a cached index then points at freed memory.
    QModelIndex m_cachedRoot;
};

#endif // INFOTREEDOCK_H
