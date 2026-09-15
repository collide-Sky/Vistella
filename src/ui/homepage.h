#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class HomePage; }
QT_END_NAMESPACE

class QSortFilterProxyModel;
class RecentListModel;

// 主页"模块"区 4 个按钮对应的模块入口
//   命名跟阶段 0 规划的 4 个 Worker 对应: imageWorker / audioWorker / videoWorker / visionWorker
//   enum value 跟 string id 一一对应 (main.cpp MediaDispatcher 按 string 路由)
enum class HomeModule {
    ImageWorker  = 0,    // 图片处理
    AudioWorker  = 1,    // 音频处理
    VideoWorker  = 2,    // 视频处理
    VisionWorker = 3,    // 视觉处理 (YOLO 工业检测)
};

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    ~HomePage() override;

    // 主题切换时调用
    void applyTheme();

    // (3D 模块已移除, 2026-09-02: setNewButtonEnabled / setMode 删除)
    //   后续如果需要多模态切分再加回

signals:
    void newFileRequested();                       // 新建空白文本 (默认只支持 txt)
    void openFileRequested();                      // 打开单个文件 → MediaDispatcher 分发
    void openFolderRequested();                    // 打开文件夹 → InfoTreeDock 加载目录
    void openPathRequested(const QString &path);   // 双击最近记录 / 信息树选中回车
    void moduleRequested(HomeModule mod);          // 点击 4 个模块按钮

protected:
    // Stage G (2026-09-15): 视图菜单切回主页后自动 focus tableView
    //   之前: 切到主页时焦点还在 doc tab, 用户点击最近记录无响应
    //   现在: showEvent 抢焦点到 tableView + 选第一行, 单击立即响应
    void showEvent(QShowEvent *e) override;

private slots:
    void onRowDoubleClicked(const QModelIndex &proxyIdx);
    void onPinClicked();
    void onDeleteClicked();

    void onNewClicked();
    void onOpenClicked();
    void onOpenFolderClicked();
    void onModuleClicked();

private:
    Ui::HomePage          *ui;
    RecentListModel       *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
};

#endif // HOMEPAGE_H
