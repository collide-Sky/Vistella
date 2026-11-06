#include "infotreedock.h"
#include "ui_infotreedock.h"
#include "FileTreeProxyModel.h"

#include <QKeyEvent>
#include <QTreeView>
#include <QHeaderView>
#include <QFileSystemModel>

InfoTreeDock::InfoTreeDock(QWidget *parent)
    : QDockWidget(parent)
    , ui(new Ui::InfoTreeDock)
    , m_fsModel(new QFileSystemModel(this))
    , m_proxy(new FileTreeProxyModel(this))
{
    ui->setupUi(this);

    setObjectName(QStringLiteral("InfoTreeDock"));
    setWindowTitle(tr("信息树"));
    setFeatures(QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    // M2-7b: QFileSystemModel reads the directories in a worker thread,
    // the proxy filters by extension and owns the expand arrow of an
    // empty directory.
    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_proxy->setSourceModel(m_fsModel);

    // Tree look: one column, no header, 28px rows, no double click expand
    // (double click = open).
    //   Row height 28px (close to VS Code / Qt Creator), light green
    //   hover accent. The icons come from the DecorationRole of the
    //   QFileSystemModel (QFileIconProvider).
    auto *tree = ui->tree;
    tree->setModel(m_proxy);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    tree->setRootIsDecorated(true);
    tree->setExpandsOnDoubleClick(false);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setAttribute(Qt::WA_TranslucentBackground, false);
    tree->viewport()->setAutoFillBackground(false);
    tree->viewport()->setAttribute(Qt::WA_TranslucentBackground, false);
    // Row height 28px (uniform height, not the "compute it from sizeHint"
    // approach).
    //   hover: light green transparent background (accent #4caf80, 12%)
    //   selected: accent 50%
    //   selected + hover: accent 80%
    tree->setStyleSheet(
        QStringLiteral("QTreeView::item { height: 28px; padding-left: 4px; }"
                       "QTreeView::item:hover { background-color: #4caf8020; }"
                       "QTreeView::item:selected { background-color: #4caf8050; color: #1a1a1a; }"
                       "QTreeView::item:selected:hover { background-color: #4caf8080; }")
    );

    // signals
    connect(tree, &QTreeView::activated,
            this, &InfoTreeDock::onActivated);
    connect(m_proxy, &FileTreeProxyModel::directoryRead,
            this, &InfoTreeDock::onDirectoryRead);

    // Enter opens the file.
    tree->installEventFilter(this);
}

InfoTreeDock::~InfoTreeDock()
{
    delete ui;
}

QString InfoTreeDock::rootPath() const
{
    return m_rootPath;
}

void InfoTreeDock::setRootPath(const QString &path)
{
    if (path.isEmpty()) {
        m_rootPath.clear();
        m_fsModel->setRootPath(QString());
        ui->tree->setRootIndex(QModelIndex());
        return;
    }

    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        // The path does not exist: keep the old root, the tree must not
        // become empty.
        return;
    }

    m_rootPath = fi.absoluteFilePath();
    // The rootPath of the QFileSystemModel decides which tree the model
    // exposes; the top of the tree is the content of the root directory.
    m_fsModel->setRootPath(m_rootPath);
    updateRootIndex();
}

void InfoTreeDock::updateRootIndex()
{
    // The root index belongs to the VIEW: without it the tree would show
    // the drives of the machine instead of the root directory.
    //
    // The index is taken FRESH here: a proxy index may not survive a
    // change of the source model (see the rules in FileTreeProxyModel.h).
    const QModelIndex rootIdx = m_proxy->indexForPath(m_rootPath);
    ui->tree->setRootIndex(rootIdx);
}

void InfoTreeDock::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (FileTreeProxyModel::isDirForIndex(index)) {
        toggleDirectory(index);
        return;
    }
    const QString p = FileTreeProxyModel::pathForIndex(index);
    if (!p.isEmpty()) emit openFileRequested(p);
}

void InfoTreeDock::toggleDirectory(const QModelIndex &index)
{
    auto *tree = ui->tree;
    const bool expanded = tree->isExpanded(index);
    tree->setExpanded(index, !expanded);
}

void InfoTreeDock::onDirectoryRead(const QString &path)
{
    Q_UNUSED(path);
    // The directory has been read: the expand arrow of an empty
    // directory has to disappear -> let the view lay out again.
    //
    // The root index is taken again because the read changed the source
    // model, and a proxy index does not survive such a change.
    updateRootIndex();
    ui->tree->doItemsLayout();
}

bool InfoTreeDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->tree && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            onReturnPressed();
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void InfoTreeDock::onReturnPressed()
{
    auto *tree = ui->tree;
    const QModelIndex idx = tree->currentIndex();
    if (!idx.isValid()) return;
    if (FileTreeProxyModel::isDirForIndex(idx)) {
        toggleDirectory(idx);
        return;
    }
    const QString p = FileTreeProxyModel::pathForIndex(idx);
    if (!p.isEmpty()) emit openFileRequested(p);
}
