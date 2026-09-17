// =============================================================================
//  SmartObjectWatcher implementation - P1.4.1 (2026-09-17)
// =============================================================================

#include "SmartObjectWatcher.h"
#include "Layer.h"

#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDateTime>

namespace layers {

SmartObjectWatcher::SmartObjectWatcher(QObject *parent)
    : QObject(parent),
      m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &SmartObjectWatcher::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &SmartObjectWatcher::onDirectoryChanged);

    // Polling fallback. 2s is responsive enough for "user just saved
    // externally" use cases while keeping CPU use negligible.
    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout,
            this, &SmartObjectWatcher::onPollTick);
    m_pollTimer.start();
}

SmartObjectWatcher::~SmartObjectWatcher() = default;

QString SmartObjectWatcher::resolveWatchPath(const LayerStack *stack, int index) const
{
    if (!stack) return {};
    auto l = stack->at(index);
    if (!l || l->kind != Layer::SmartObject) return {};
    // We watch the actual file on disk (linked: sourceFilePath; embedded: cache file).
    QString p = l->sourceFilePath;
    if (l->sourceEmbedded) {
        // Reuse LayerStack's MD5-based cache path generator so we always
        // match the path the rest of the stack uses.
        p = LayerStack::smartObjectCachePathFor(p);
    }
    if (p.isEmpty()) return {};
    return QFileInfo(p).absoluteFilePath();
}

void SmartObjectWatcher::addWatch(const QString &absPath, int layerIndex)
{
    if (absPath.isEmpty()) return;
    // Replace any prior entry for the same path or same index.
    if (m_pathToEntry.contains(absPath)) {
        m_watcher->removePath(absPath);
        int oldIdx = m_pathToEntry.value(absPath).layerIndex;
        m_indexToPath.remove(oldIdx);
        m_pathToEntry.remove(absPath);
    }
    if (QFileInfo::exists(absPath)) {
        m_watcher->addPath(absPath);
    }
    WatchEntry e;
    e.layerIndex = layerIndex;
    e.absPath = absPath;
    e.lastMtime = QFileInfo(absPath).lastModified();
    m_pathToEntry.insert(absPath, e);
    m_indexToPath.insert(layerIndex, absPath);
}

void SmartObjectWatcher::removeWatch(const QString &absPath)
{
    if (absPath.isEmpty()) return;
    if (m_pathToEntry.contains(absPath)) {
        m_watcher->removePath(absPath);
        int idx = m_pathToEntry.value(absPath).layerIndex;
        m_indexToPath.remove(idx);
        m_pathToEntry.remove(absPath);
    }
}

void SmartObjectWatcher::watchLayer(const LayerStack *stack, int index)
{
    const QString p = resolveWatchPath(stack, index);
    if (p.isEmpty()) return;
    // If we already have an entry for this index pointing to a different
    // path (source changed), drop the old one first.
    if (m_indexToPath.contains(index)) {
        const QString oldPath = m_indexToPath.value(index);
        if (oldPath != p) removeWatch(oldPath);
    }
    addWatch(p, index);
}

void SmartObjectWatcher::unwatchLayer(int index)
{
    if (!m_indexToPath.contains(index)) return;
    removeWatch(m_indexToPath.value(index));
}

void SmartObjectWatcher::rewatchAll(const LayerStack *stack)
{
    if (!stack) return;
    // Drop indices that no longer exist.
    QList<int> stale;
    for (auto it = m_indexToPath.begin(); it != m_indexToPath.end(); ++it) {
        if (it.key() < 0 || it.key() >= stack->count()) stale << it.key();
    }
    for (int i : stale) unwatchLayer(i);

    // (Re)register every current SmartObject layer.
    for (int i = 0; i < stack->count(); ++i) {
        auto l = stack->at(i);
        if (l && l->kind == Layer::SmartObject) {
            watchLayer(stack, i);
        } else if (m_indexToPath.contains(i)) {
            unwatchLayer(i);
        }
    }
}

int SmartObjectWatcher::watchedCount() const
{
    return m_pathToEntry.size();
}

void SmartObjectWatcher::pollNow()
{
    onPollTick();
}

void SmartObjectWatcher::onFileChanged(const QString &path)
{
    // QFileSystemWatcher fires once per change; re-add in case the file was
    // atomically replaced (some editors delete+recreate on save).
    if (QFileInfo::exists(path)) {
        if (!m_watcher->files().contains(path)) m_watcher->addPath(path);
    }
    if (m_pathToEntry.contains(path)) {
        WatchEntry e = m_pathToEntry.value(path);
        QFileInfo fi(path);
        const QDateTime newMtime = fi.lastModified();
        if (newMtime != e.lastMtime || !newMtime.isValid()) {
            e.lastMtime = newMtime;
            m_pathToEntry.insert(path, e);
            emit sourceFileChanged(e.layerIndex, path);
        }
    }
}

void SmartObjectWatcher::onDirectoryChanged(const QString & /*path*/)
{
    // Some editors save by atomic rename; the file event handles that, but
    // we trigger a poll to catch any cached mtime drift.
    onPollTick();
}

void SmartObjectWatcher::onPollTick()
{
    // Iterate a snapshot of entries since emit may trigger callbacks that
    // mutate m_pathToEntry.
    const QList<WatchEntry> snapshot = m_pathToEntry.values();
    for (const WatchEntry &e : snapshot) {
        QFileInfo fi(e.absPath);
        if (!fi.exists()) {
            // File disappeared — fire once with invalid mtime so the UI
            // can decide to show "missing" rather than silently no-op.
            if (e.lastMtime.isValid()) {
                WatchEntry e2 = e;
                e2.lastMtime = QDateTime();
                m_pathToEntry.insert(e.absPath, e2);
                emit sourceFileChanged(e.layerIndex, e.absPath);
            }
            continue;
        }
        const QDateTime mtime = fi.lastModified();
        if (mtime.isValid() && mtime != e.lastMtime) {
            WatchEntry e2 = e;
            e2.lastMtime = mtime;
            m_pathToEntry.insert(e.absPath, e2);
            emit sourceFileChanged(e.layerIndex, e.absPath);
        }
    }
}

} // namespace layers