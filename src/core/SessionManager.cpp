#include "SessionManager.h"

#include <QSettings>
#include <QStringList>

namespace {
constexpr const char *kOpenFilesKey      = "Session/openFiles";
constexpr const char *kActiveFileKey     = "Session/activeFile";
constexpr const char *kLastExitCleanKey  = "Session/lastExitClean";
}

SessionManager &SessionManager::instance()
{
    static SessionManager inst;
    return inst;
}

QStringList SessionManager::restoreOpenFiles() const
{
    QSettings s;
    return s.value(kOpenFilesKey).toStringList();
}

void SessionManager::saveOpenFiles(const QStringList &files)
{
    QSettings s;
    s.setValue(kOpenFilesKey, files);
}

void SessionManager::saveActiveFile(const QString &path)
{
    QSettings().setValue(kActiveFileKey, path);
}

QString SessionManager::restoreActiveFile() const
{
    return QSettings().value(kActiveFileKey).toString();
}

void SessionManager::clear()
{
    QSettings s;
    s.remove(kOpenFilesKey);
    s.remove(kActiveFileKey);
}

void SessionManager::setLastExitClean(bool clean)
{
    QSettings().setValue(kLastExitCleanKey, clean);
}

bool SessionManager::isLastExitClean() const
{
    // 默认 true: 首次启动 / QSettings 还没这个 key 都视为正常退出
    return QSettings().value(kLastExitCleanKey, true).toBool();
}
