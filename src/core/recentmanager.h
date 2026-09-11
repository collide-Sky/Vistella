#ifndef RECENTMANAGER_H
#define RECENTMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QString>

// 最近文件所属模式 (与 MainWindow::AppMode 数值一致, 0=建模 1=多媒体)
enum RecentMode {
    RecentModeAny        = -1,
    RecentModeModeling   = 0,
    RecentModeMultimedia = 1,
};

// 一条最近文件记录
struct RecentEntry
{
    QString   filePath;     // 绝对路径
    QString   fileName;     // 文件名 (basename)
    QString   fileType;     // 扩展名 (不带点, 大写)
    int       mode = RecentModeModeling;  // 0=建模 1=多媒体 (默认值兼容老数据)
    QDateTime lastEditTime; // 本程序内最近一次保存时间
    QDateTime lastOpenTime; // 最近一次打开时间
    bool      pinned = false;
};

class RecentManager : public QObject
{
    Q_OBJECT
public:
    static RecentManager &instance();

    // 读出全部记录 (固定项在前, 然后按 lastOpenTime 倒序)
    QList<RecentEntry> entries() const;

    // 按模式过滤: mode=RecentModeAny 走原 entries(); 否则只返回匹配 mode 的
    QList<RecentEntry> entriesForMode(RecentMode mode) const;

    // 读取/设置最多保存多少条非固定记录
    int maxUnpinned() const;
    void setMaxUnpinned(int n);

    // 添加/更新一条记录: 如果已存在, 更新时间和编辑时间; 否则追加
    // mode 标记该文件归属哪个模式 (建模/多媒体)
    // lastEditTime 默认 = now
    void touchOpen(const QString &filePath,
                   RecentMode mode = RecentModeModeling,
                   const QDateTime &lastEditTime = QDateTime::currentDateTime());

    // 更新最后编辑/保存时间 (保存文件后调用)
    void touchEdit(const QString &filePath);

    // 切换固定状态
    void togglePin(const QString &filePath);

    // 删除单条 (固定项不允许删, 返回 false)
    bool remove(const QString &filePath);

    // 清除所有非固定记录
    void clearUnpinned();

    // 清除全部 (含固定)
    void clearAll();

signals:
    void changed();

private:
    explicit RecentManager(QObject *parent = nullptr);
    RecentManager(const RecentManager &) = delete;
    RecentManager &operator=(const RecentManager &) = delete;

    void save() const;
    void load();

    QList<RecentEntry> m_entries;
    int m_maxUnpinned = 20;
};

#endif // RECENTMANAGER_H
