#ifndef INFOTREEDOCK_H
#define INFOTREEDOCK_H

#include <QDockWidget>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class InfoTreeDock; }
class QTreeView;
class QModelIndex;
class FileTreeModel;
QT_END_NAMESPACE

// =============================================================
// InfoTreeDock — 信息树 / 通用项目浏览器 (2026-09-02 决策 5 重构: Model/View)
//
// 职责:
//   1. setRootPath(path)  加载指定目录的文件树
//   2. 文件树展示 (QTreeView + FileTreeModel + FileTreeItem + FileTreeNodeData)
//   3. 双击文件 / 选中按回车 → emit openFileRequested(path)
//      MainWindow 接收后调 MediaDispatcher 路由
//
// 决策 5 改动 (跟旧 QTreeWidget 版本相比):
//   - 旧: QTreeWidget (item-based, 数据和视图耦合)
//   - 新: QTreeView + FileTreeModel (Model/View, 数据/视图/树结构三层分离)
//   - 节点数据单独抽到 FileTreeNodeData (POD), 树结构在 FileTreeItem
//   - 扩展名过滤: 不在 FileExtensionRegistry 里的文件不显示
// =============================================================

class InfoTreeDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit InfoTreeDock(QWidget *parent = nullptr);
    ~InfoTreeDock() override;

    // 切换到指定目录作为根, 清空旧内容, 重新扫描
    void setRootPath(const QString &path);
    QString rootPath() const;

protected:
    // 拦截 tree 上的回车键 → onReturnPressed
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // 用户在树里双击文件 / 选中后按回车 → 发出文件绝对路径
    void openFileRequested(const QString &path);

private slots:
    // QTreeView 信号
    void onActivated(const QModelIndex &index);
    void onExpanded(const QModelIndex &index);
    void onReturnPressed();

private:
    Ui::InfoTreeDock *ui;
    FileTreeModel   *m_model = nullptr;
};

#endif // INFOTREEDOCK_H
