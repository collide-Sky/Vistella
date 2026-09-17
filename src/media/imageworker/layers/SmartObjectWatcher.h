#ifndef SMARTOBJECTWATCHER_H
#define SMARTOBJECTWATCHER_H

// =============================================================================
//  SmartObjectWatcher - P1.4.1 (2026-09-17)
//
//  Monitors a LayerStack's SmartObject source files for external modification
//  via QFileSystemWatcher and emits sourceFileChanged(index, path) on mtime
//  change. Used by MainWindow to surface a "source file changed on disk,
//  refresh?" status hint.
//
//  Lifecycle:
//    - MainWindow owns one SmartObjectWatcher per ImageWindow.
//    - watchLayer(stack, index) registers (or refreshes) a watch for that
//      layer's sourceFilePath (linked or embedded cache). Duplicate calls
//      replace the previous watch for that path.
//    - unwatchLayer(index) drops the watch.
//    - rewatchAll(stack) rewires all current SmartObject layer paths — call
//      after layer additions / deletions.
//
//  Polling backup: QFileSystemWatcher can miss events on some platforms
//  (Windows network shares, atomic-save editors). We periodically poll the
//  watched files' QFileInfo::lastModified (every 2 s) and re-emit on change.
//  Cheap because the set of watched paths is bounded by the SmartObject
//  layer count.
// =============================================================================

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QDateTime>

#include <QFileSystemWatcher>

#include "LayerStack.h"

namespace layers {

class SmartObjectWatcher : public QObject
{
    Q_OBJECT
public:
    explicit SmartObjectWatcher(QObject *parent = nullptr);
    ~SmartObjectWatcher() override;

    // Register (or refresh) a watch for the SmartObject layer at `index`.
    // No-op if the layer is not SmartObject or its sourceFilePath is empty.
    void watchLayer(const LayerStack *stack, int index);

    // Drop the watch for `index`. Safe if nothing was watched.
    void unwatchLayer(int index);

    // Re-register watches for every SmartObject layer in `stack`.
    // Drop entries whose indices no longer exist.
    void rewatchAll(const LayerStack *stack);

    // Currently watched count (paths, not indices).
    int  watchedCount() const;

    // For tests: poll immediately rather than waiting for the timer.
    void pollNow();

signals:
    // Emitted when a watched SmartObject source file's mtime changes.
    // `index` is the current index in the LayerStack (may have shifted
    // if the user rearranged layers; consumers should re-resolve by path).
    void sourceFileChanged(int index, const QString &path);

private slots:
    void onDirectoryChanged(const QString &path);
    void onFileChanged(const QString &path);
    void onPollTick();

private:
    struct WatchEntry {
        int     layerIndex;       // last-known index in the stack
        QString absPath;          // absolute file path being watched
        QDateTime lastMtime;      // cached for polling-based change detection
    };

    QFileSystemWatcher *m_watcher;
    QHash<QString, WatchEntry> m_pathToEntry;   // path -> entry (key)
    QHash<int, QString>        m_indexToPath;   // index -> path
    QTimer  m_pollTimer;

    // Internal helpers
    QString resolveWatchPath(const LayerStack *stack, int index) const;
    void    addWatch(const QString &absPath, int layerIndex);
    void    removeWatch(const QString &absPath);
};

} // namespace layers

#endif // SMARTOBJECTWATCHER_H