#include "homepage.h"
#include "ui_homepage.h"

#include "recentlistmodel.h"
#include "recentmanager.h"
#include "../core/ThemeManager.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QToolButton>

// =============================================================
// ActionButtonDelegate: 一列里画两个可点击的小图标
// =============================================================
class ActionButtonDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Kind { PinButton, DeleteButton };

    ActionButtonDelegate(Kind kind, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_kind(kind) {}

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);

        QModelIndex modelIdx = idx;
        if (auto *proxy = qobject_cast<QSortFilterProxyModel *>(const_cast<QAbstractItemModel *>(modelIdx.model()))) {
            modelIdx = proxy->mapToSource(idx);
        }
        const bool pinnedReal = modelIdx.data(RecentListModel::PinnedRole).toBool();

        p->save();
        if (pinnedReal) {
            p->fillRect(o.rect, QColor(200, 240, 200));
        }
        p->restore();

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = o.rect;
        const QPoint c = r.center();
        const int half = qMin(r.width(), r.height()) / 3;
        QRect iconRect(c.x() - half, c.y() - half, half * 2, half * 2);

        if (m_kind == PinButton) {
            QFont f = o.font;
            f.setPointSizeF(f.pointSizeF() + 2);
            p->setFont(f);
            p->setPen(o.palette.text().color());
            p->drawText(iconRect, Qt::AlignCenter, QStringLiteral("✂"));
            if (pinnedReal) {
                p->setPen(QPen(QColor(80, 160, 80), 2));
                p->drawRoundedRect(iconRect.adjusted(1, 1, -1, -1), 4, 4);
            }
        } else {
            QFont f = o.font;
            f.setPointSizeF(f.pointSizeF() + 2);
            p->setFont(f);
            const bool canDelete = !pinnedReal;
            p->setPen(canDelete ? QColor(200, 60, 60) : QColor(160, 160, 160));
            p->drawText(iconRect, Qt::AlignCenter, QStringLiteral("✖"));
        }
        p->restore();
    }

    bool editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        if (e->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton && option.rect.contains(me->pos())) {
                QModelIndex src = index;
                if (auto *proxy = qobject_cast<QSortFilterProxyModel *>(const_cast<QAbstractItemModel *>(model))) {
                    src = proxy->mapToSource(index);
                }
                const bool pinned = src.data(RecentListModel::PinnedRole).toBool();
                if (m_kind == DeleteButton && pinned)
                    return true;
                emit const_cast<ActionButtonDelegate *>(this)->commitData(
                    qobject_cast<QWidget *>(parent()));
                emit triggered(index);
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(e, model, option, index);
    }

signals:
    void triggered(const QModelIndex &index);

private:
    Kind m_kind;
};

#include "homepage.moc"

// =============================================================
// HomePage
// =============================================================

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomePage)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("HomePage"));

    // 最近文件表格模型
    m_model = new RecentListModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(RecentListModel::PinnedRole);
    m_proxy->sort(0, Qt::DescendingOrder);

    ui->tableView->setModel(m_proxy);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->setShowGrid(false);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->verticalHeader()->setVisible(false);
    ui->tableView->horizontalHeader()->setStretchLastSection(false);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    ui->tableView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    ui->tableView->setColumnWidth(4, 60);
    ui->tableView->setColumnWidth(5, 60);

    auto *pinDel = new ActionButtonDelegate(ActionButtonDelegate::PinButton, ui->tableView);
    auto *delDel = new ActionButtonDelegate(ActionButtonDelegate::DeleteButton, ui->tableView);
    ui->tableView->setItemDelegateForColumn(4, pinDel);
    ui->tableView->setItemDelegateForColumn(5, delDel);

    connect(pinDel, &ActionButtonDelegate::triggered, this, &HomePage::onPinClicked);
    connect(delDel, &ActionButtonDelegate::triggered, this, &HomePage::onDeleteClicked);
    connect(ui->tableView, &QTableView::doubleClicked, this, &HomePage::onRowDoubleClicked);

    // 开始按钮
    connect(ui->btnNew,       &QToolButton::clicked, this, &HomePage::onNewClicked);
    connect(ui->btnOpen,      &QToolButton::clicked, this, &HomePage::onOpenClicked);
    connect(ui->btnOpenFolder,&QToolButton::clicked, this, &HomePage::onOpenFolderClicked);

    // 4 个模块入口按钮 (图片/音频/视频/视觉处理)
    //   阶段 1 才会真正实现, 现在 emit signal 让 MainWindow 知道即可
    connect(ui->modImage,     &QToolButton::clicked, this, &HomePage::onModuleClicked);
    connect(ui->modAudio,     &QToolButton::clicked, this, &HomePage::onModuleClicked);
    connect(ui->modVideo,     &QToolButton::clicked, this, &HomePage::onModuleClicked);
    connect(ui->modVision,    &QToolButton::clicked, this, &HomePage::onModuleClicked);

    // 主题
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HomePage::applyTheme);
    applyTheme();
}

HomePage::~HomePage()
{
    delete ui;
}

// (3D 模块已移除, 2026-09-02: setNewButtonEnabled / setMode 删除)
//   后续如果需要多模态切分再加回

void HomePage::applyTheme()
{
    const auto &p = ThemeManager::instance().palette();
    const QString css = QString(
        "QWidget#HomePage { background: %1; }"
        // 顶部欢迎条
        "QWidget#header { background: %2; border-bottom: 1px solid %3; }"
        "QLabel#headerTitle    { color: %4; }"
        "QLabel#headerSubtitle { color: %5; }"
        "QLabel#headerIcon     { color: %4; }"
        // 区块标题
        "QLabel#sectionTitle   { color: %5; }"
        // 大按钮 (开始区)
        "QToolButton#bigAction {"
        "  background: %6; color: %4; border: 1px solid %3; border-radius: 8px;"
        "  padding: 16px; text-align: left; font-weight: 500;"
        "}"
        "QToolButton#bigAction:hover  { background: %7; border-color: %8; }"
        "QToolButton#bigAction:pressed{ background: %9; }"
        // 模板小卡片
        "QToolButton#tplCard {"
        "  background: %6; color: %4; border: 1px solid %3; border-radius: 6px;"
        "  padding: 10px; font-weight: 500;"
        "}"
        "QToolButton#tplCard:hover  { background: %7; border-color: %8; }"
        "QToolButton#tplCard:pressed{ background: %9; }"
    )
    .arg(p.windowBg.name())           // %1
    .arg(p.dockHeader.name())         // %2
    .arg(p.menuBorder.name())         // %3
    .arg(p.text.name())               // %4
    .arg(p.textSubtle.name())         // %5
    .arg(p.base.name())               // %6
    .arg(p.tabBgHover.name())         // %7
    .arg(p.accent.name())             // %8
    .arg(p.menuHover.name())          // %9
    ;
    setStyleSheet(css);
}

void HomePage::onRowDoubleClicked(const QModelIndex &proxyIdx)
{
    if (!proxyIdx.isValid())
        return;
    QModelIndex src = m_proxy->mapToSource(proxyIdx);
    const QString path = src.data(RecentListModel::FilePathRole).toString();
    if (!path.isEmpty())
        emit openPathRequested(path);
}

void HomePage::onPinClicked()
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(ui->tableView->model());
    if (!proxy) return;
    const QModelIndex idx = ui->tableView->currentIndex();
    if (!idx.isValid()) return;
    QModelIndex src = proxy->mapToSource(idx);
    const QString path = src.data(RecentListModel::FilePathRole).toString();
    if (path.isEmpty()) return;
    RecentManager::instance().togglePin(path);
}

void HomePage::onDeleteClicked()
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(ui->tableView->model());
    if (!proxy) return;
    const QModelIndex idx = ui->tableView->currentIndex();
    if (!idx.isValid()) return;
    QModelIndex src = proxy->mapToSource(idx);
    const QString path = src.data(RecentListModel::FilePathRole).toString();
    if (path.isEmpty()) return;
    if (src.data(RecentListModel::PinnedRole).toBool())
        return;
    RecentManager::instance().remove(path);
}

void HomePage::onNewClicked()
{
    emit newFileRequested();
}

void HomePage::onOpenClicked()
{
    emit openFileRequested();
}

void HomePage::onOpenFolderClicked()
{
    emit openFolderRequested();
}

void HomePage::onModuleClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn) return;
    // 4 个模块按钮 → emit signal, MainWindow 接收后调 MediaDispatcher 启动对应 Worker
    HomeModule mod = HomeModule::ImageWorker;
    if      (btn == ui->modImage)  mod = HomeModule::ImageWorker;
    else if (btn == ui->modAudio)  mod = HomeModule::AudioWorker;
    else if (btn == ui->modVideo)  mod = HomeModule::VideoWorker;
    else if (btn == ui->modVision) mod = HomeModule::VisionWorker;
    emit moduleRequested(mod);
}
