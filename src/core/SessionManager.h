#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QObject>
#include <QStringList>

// 简单的会话恢复: 启动时把上次打开的文件列表恢复为 tab
// 关闭时把当前所有打开的文件路径写进 QSettings
class SessionManager
{
public:
    static SessionManager &instance();

    // 启动时调用, 拿到上次打开的文件列表
    QStringList restoreOpenFiles() const;
    // 关闭时调用, 把当前所有打开文件写回
    void saveOpenFiles(const QStringList &files);

    // 上次激活的 tab (按文件路径)
    void   saveActiveFile(const QString &path);
    QString restoreActiveFile() const;

    // 清空会话 (例如用户主动 "新建会话")
    void clear();

    // 退出状态标记: 正常退出(true) vs 异常中断(false, 程序崩溃/kill/断电)
    // - 正常退出: 启动时不恢复文件
    // - 异常退出: 启动时弹窗问"是否恢复"
    void setLastExitClean(bool clean);
    bool isLastExitClean() const;   // 默认 true (首次启动视为正常)

private:
    SessionManager() = default;
};

#endif // SESSIONMANAGER_H
