#include "recentmanager.h"

#include <QFileInfo>
#include <QSettings>
#include <algorithm>

namespace {
constexpr const char *kGroup = "RecentFiles";
constexpr const char *kMaxUnpinned = "RecentFiles/maxUnpinned";
}

RecentManager &RecentManager::instance()
{
    static RecentManager s;
    return s;
}

RecentManager::RecentManager(QObject *parent)
    : QObject(parent)
{
    load();
}

int RecentManager::maxUnpinned() const
{
    return m_maxUnpinned;
}

void RecentManager::setMaxUnpinned(int n)
{
    m_maxUnpinned = qMax(0, n);
    QSettings().setValue(kMaxUnpinned, m_maxUnpinned);
}

QList<RecentEntry> RecentManager::entries() const
{
    return m_entries;
}

QList<RecentEntry> RecentManager::entriesForMode(RecentMode mode) const
{
    if (mode == RecentModeAny) return entries();
    QList<RecentEntry> out;
    out.reserve(m_entries.size());
    for (const auto &e : m_entries) {
        if (e.mode == int(mode)) out.append(e);
    }
    return out;
}

int findIndex(const QList<RecentEntry> &list, const QString &path)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].filePath == path)
            return i;
    }
    return -1;
}

void RecentManager::touchOpen(const QString &filePath, RecentMode mode, const QDateTime &lastEditTime)
{
    if (filePath.isEmpty())
        return;

    const QFileInfo fi(filePath);
    if (!fi.exists() && !filePath.startsWith(QLatin1String(":/")))
        return;

    // 1) 先删除旧条目
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
        [&](const RecentEntry &x){ return x.filePath == filePath; }),
        m_entries.end());

    // 2) 构造新条目 (pinned 标志: 如果原来在列表里, 保留; 新条目默认 false)
    RecentEntry e;
    e.filePath     = filePath;
    e.fileName     = fi.fileName().isEmpty() ? filePath : fi.fileName();
    e.fileType     = fi.suffix().isEmpty() ? QStringLiteral("TXT")
                                           : fi.suffix().toUpper();
    e.mode         = int(mode);
    e.lastEditTime = lastEditTime;
    e.lastOpenTime = QDateTime::currentDateTime();
    // pinned 从已有条目推断; 已被 erase, 但之前没保存原 pinned? 修: 第一次扫
    // 简单做法: 新打开的文件默认 unpinned. 钉住必须用户主动调 togglePin.
    e.pinned       = false;

    // 3) 总是放到 unpinned 段的最前 (pinned 段不参与)
    //    先找最后一个 pinned 的位置, 插在它后面
    int lastPinned = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].pinned) lastPinned = i;
    }
    m_entries.insert(lastPinned + 1, e);

    // 4) 截断非固定记录数量 (保留 pinned 不动)
    int unpinnedKept = 0;
    QList<RecentEntry> trimmed;
    trimmed.reserve(m_entries.size());
    for (const auto &x : std::as_const(m_entries)) {
        if (x.pinned) {
            trimmed.append(x);
        } else {
            if (unpinnedKept >= m_maxUnpinned) continue;
            trimmed.append(x);
            ++unpinnedKept;
        }
    }
    m_entries = trimmed;
    save();
    emit changed();
}

void RecentManager::touchEdit(const QString &filePath)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const RecentEntry &e) { return e.filePath == filePath; });
    if (it == m_entries.end())
        return;
    it->lastEditTime = QDateTime::currentDateTime();
    save();
    emit changed();
}

void RecentManager::togglePin(const QString &filePath)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const RecentEntry &e) { return e.filePath == filePath; });
    if (it == m_entries.end())
        return;
    it->pinned = !it->pinned;

    // 重排: pinned 段(按原顺序) + unpinned 段(按 lastOpenTime 倒序)
    QList<RecentEntry> pinnedList, unpinnedList;
    for (const auto &x : std::as_const(m_entries)) {
        if (x.pinned) pinnedList.append(x);
        else          unpinnedList.append(x);
    }
    // unpinned 按 lastOpenTime 倒序
    std::sort(unpinnedList.begin(), unpinnedList.end(),
              [](const RecentEntry &a, const RecentEntry &b){
                  return a.lastOpenTime > b.lastOpenTime;
              });
    m_entries = pinnedList + unpinnedList;
    save();
    emit changed();
}

bool RecentManager::remove(const QString &filePath)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&](const RecentEntry &e) { return e.filePath == filePath; });
    if (it == m_entries.end())
        return false;
    if (it->pinned)
        return false; // 固定项不可删
    m_entries.erase(it);
    save();
    emit changed();
    return true;
}

void RecentManager::clearUnpinned()
{
    const bool removed = std::any_of(m_entries.begin(), m_entries.end(),
                                     [](const RecentEntry &e) { return !e.pinned; });
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [](const RecentEntry &e) { return !e.pinned; }),
                    m_entries.end());
    if (removed) {
        save();
        emit changed();
    }
}

void RecentManager::clearAll()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    save();
    emit changed();
}

void RecentManager::save() const
{
    QSettings s;
    s.beginWriteArray(QString(kGroup) + QStringLiteral("/items"));
    int idx = 0;
    for (const auto &e : m_entries) {
        s.setArrayIndex(idx++);
        s.setValue(QStringLiteral("path"),        e.filePath);
        s.setValue(QStringLiteral("name"),        e.fileName);
        s.setValue(QStringLiteral("type"),        e.fileType);
        s.setValue(QStringLiteral("mode"),        e.mode);
        s.setValue(QStringLiteral("lastEdit"),    e.lastEditTime);
        s.setValue(QStringLiteral("lastOpen"),    e.lastOpenTime);
        s.setValue(QStringLiteral("pinned"),      e.pinned);
    }
    s.endArray();
    s.setValue(kMaxUnpinned, m_maxUnpinned);
}

void RecentManager::load()
{
    QSettings s;
    m_maxUnpinned = s.value(kMaxUnpinned, 20).toInt();

    const int n = s.beginReadArray(QString(kGroup) + QStringLiteral("/items"));
    m_entries.clear();
    m_entries.reserve(n);
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        RecentEntry e;
        e.filePath     = s.value(QStringLiteral("path")).toString();
        e.fileName     = s.value(QStringLiteral("name")).toString();
        e.fileType     = s.value(QStringLiteral("type")).toString();
        e.mode         = s.value(QStringLiteral("mode"), int(RecentModeModeling)).toInt();
        e.lastEditTime = s.value(QStringLiteral("lastEdit")).toDateTime();
        e.lastOpenTime = s.value(QStringLiteral("lastOpen")).toDateTime();
        e.pinned       = s.value(QStringLiteral("pinned"), false).toBool();
        if (!e.filePath.isEmpty())
            m_entries.append(e);
    }
    s.endArray();
}
