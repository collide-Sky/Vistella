#include "infotreedock.h"
#include "ui_infotreedock.h"
#include "FileTreeModel.h"
#include "FileTreeItem.h"

#include <QKeyEvent>
#include <QTreeView>
#include <QHeaderView>

InfoTreeDock::InfoTreeDock(QWidget *parent)
    : QDockWidget(parent)
    , ui(new Ui::InfoTreeDock)
    , m_model(new FileTreeModel(this))
{
    ui->setupUi(this);

    setObjectName(QStringLiteral("InfoTreeDock"));
    setWindowTitle(tr("信息树"));
    setFeatures(QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    // 树外观: 单列, 无 header, 行高 28px, 不双击展开 (双击 = 打开)
    //   2026-09-02: 节点高度 28px (跟 VS Code / Qt Creator 接近), 浅绿悬停强调
    //   图标由 FileTreeModel::data(DecorationRole) 直接返 QIcon (用 static QFileIconProvider)
    auto *tree = ui->tree;
    tree->setModel(m_model);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    tree->setRootIsDecorated(true);
    tree->setExpandsOnDoubleClick(false);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setAttribute(Qt::WA_TranslucentBackground, false);
    tree->viewport()->setAutoFillBackground(false);
    tree->viewport()->setAttribute(Qt::WA_TranslucentBackground, false);
    // 行高 28px (统一高度, 不用 uniformRowHeights=false 那种"按 sizeHint 算"的方案)
    //   悬停: 浅绿透明背景 (accent #4caf80, 12% 不透明)
    //   选中: accent 50% 不透明
    //   选中 + 悬停: accent 80%
    tree->setStyleSheet(
        QStringLiteral("QTreeView::item { height: 28px; padding-left: 4px; }"
                       "QTreeView::item:hover { background-color: #4caf8020; }"
                       "QTreeView::item:selected { background-color: #4caf8050; color: #1a1a1a; }"
                       "QTreeView::item:selected:hover { background-color: #4caf8080; }")
    );

    // 信号
    connect(tree, &QTreeView::activated,
            this, &InfoTreeDock::onActivated);
    connect(tree, &QTreeView::expanded,
            this, &InfoTreeDock::onExpanded);

    // 按回车触发打开
    tree->installEventFilter(this);
}

InfoTreeDock::~InfoTreeDock()
{
    delete ui;
}

QString InfoTreeDock::rootPath() const
{
    return m_model ? m_model->rootPath() : QString();
}

void InfoTreeDock::setRootPath(const QString &path)
{
    if (m_model) m_model->setRootPath(path);
}

void InfoTreeDock::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (FileTreeModel::isDirForIndex(index)) {
        auto *tree = ui->tree;
        tree->setExpanded(index, !tree->isExpanded(index));
        return;
    }
    const QString p = FileTreeModel::pathForIndex(index);
    if (!p.isEmpty()) emit openFileRequested(p);
}

void InfoTreeDock::onExpanded(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (!FileTreeModel::isDirForIndex(index)) return;
    m_model->expand(index);
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
    if (FileTreeModel::isDirForIndex(idx)) {
        tree->setExpanded(idx, !tree->isExpanded(idx));
        return;
    }
    const QString p = FileTreeModel::pathForIndex(idx);
    if (!p.isEmpty()) emit openFileRequested(p);
}
