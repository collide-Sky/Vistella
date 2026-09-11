#include "MediaDispatcher.h"
#include "FileExtensionRegistry.h"

#include <QFileInfo>

// =============================================================
// MediaDispatcher 单例实现
// =============================================================

MediaDispatcher &MediaDispatcher::instance()
{
    static MediaDispatcher inst;
    return inst;
}

MediaDispatcher::MediaDispatcher(QObject *parent)
    : QObject(parent)
{
}

void MediaDispatcher::registerModule(IModule *mod)
{
    if (!mod) return;
    const QString id = mod->info().id;
    if (id.isEmpty()) {
        qWarning("[MediaDispatcher] registerModule: empty id, skip");
        return;
    }
    if (m_modules.contains(mod)) return;  // 已注册
    if (m_idMap.contains(id)) {
        qWarning("[MediaDispatcher] duplicate module id '%s', replacing", qPrintable(id));
        // 移除旧的
        IModule *old = m_idMap.value(id);
        m_modules.removeAll(old);
    }
    m_modules.append(mod);
    m_idMap.insert(id, mod);
    rebuildExtMap();
    emit modulesChanged();
}

void MediaDispatcher::unregisterModule(IModule *mod)
{
    if (!mod) return;
    if (!m_modules.contains(mod)) return;
    m_modules.removeAll(mod);
    m_idMap.remove(mod->info().id);
    rebuildExtMap();
    emit modulesChanged();
}

void MediaDispatcher::unregisterAll()
{
    if (m_modules.isEmpty() && m_idMap.isEmpty()) return;
    m_modules.clear();
    m_idMap.clear();
    // 阶段 1 前置修复 (2026-09-03): 清 m_extMap 后调 rebuildExtMap 重建默认表
    //   旧版直接 m_extMap.clear() 破坏了"默认 FileExtensionRegistry 表"导致
    //   isExtensionKnown() 永远返 false (测试 test_isExtensionKnown 暴露)
    rebuildExtMap();
    emit modulesChanged();
}

void MediaDispatcher::rebuildExtMap()
{
    m_extMap.clear();

    // 决策 5 (2026-09-02): 阶段 0 IModule 还没注册, 先填 FileExtensionRegistry 默认表
    //   注册的 IModule 覆盖默认 (后注册覆盖前注册, 跟之前 duplicate id 行为一致)
    //   这样: 阶段 0 时路由仍能工作 (返回 FileExtensionRegistry 默认的模块 id),
    //         MainWindow 收到 openFailed("module xxx not registered") 走 "暂未实现" 提示
    const auto &tbl = FileExtensionRegistry::moduleTable();
    for (auto it = tbl.cbegin(); it != tbl.cend(); ++it) {
        // 用 nullptr 占位, 实际模块未注册. moduleForFile 走 extMap 命中后
        // openFile 再做二次校验 (mod 必须非空) 才会成功.
        for (const QString &ext : it.value()) {
            if (!m_extMap.contains(ext.toLower())) {
                m_extMap.insert(ext.toLower(), nullptr);
            }
        }
    }

    // IModule 已注册的: 覆盖默认
    for (IModule *m : m_modules) {
        for (const QString &ext : m->info().extensions) {
            m_extMap.insert(ext.toLower(), m);
        }
    }
}

IModule *MediaDispatcher::moduleForId(const QString &id) const
{
    return m_idMap.value(id);
}

IModule *MediaDispatcher::moduleForFile(const QString &path) const
{
    if (path.isEmpty()) return nullptr;
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return nullptr;
    return m_extMap.value(path.mid(dot).toLower());
}

bool MediaDispatcher::isExtensionKnown(const QString &path) const
{
    if (path.isEmpty()) return false;
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return false;
    return m_extMap.contains(path.mid(dot).toLower());
}

IWorkspace *MediaDispatcher::openFile(const QString &path, QWidget *parent)
{
    if (path.isEmpty()) {
        emit openFailed(path, QStringLiteral("empty path"));
        return nullptr;
    }
    IModule *mod = moduleForFile(path);
    if (!mod) {
        // 决策 5 (2026-09-02): 区分 "未知扩展名" vs "扩展名在表里但模块没注册"
        //   阶段 0 时 4 大 IModule 都没注册, 走第二种分支给"暂未实现"提示
        //   阶段 1+ 真模块都注册后, 实际触发的是第一种分支
        QString reason;
        if (isExtensionKnown(path)) {
            const QString mid = FileExtensionRegistry::moduleIdForExtension(path);
            reason = QStringLiteral("module '%1' not registered (阶段 0 占位)").arg(mid);
        } else {
            reason = QStringLiteral("no module supports extension '%1'")
                        .arg(QFileInfo(path).suffix());
        }
        emit openFailed(path, reason);
        return nullptr;
    }
    IWorkspace *w = mod->openFile(path);
    if (!w) {
        emit openFailed(path, QStringLiteral("module '%1' failed to load").arg(mod->info().id));
        return nullptr;
    }
    if (parent) {
        w->setParent(parent);
        // 默认无 dirty, 显示默认名
        w->setWindowTitle(w->displayName());
    }
    return w;
}

IWorkspace *MediaDispatcher::openModuleById(const QString &id, QWidget *parent)
{
    IModule *m = moduleForId(id);
    if (!m) {
        emit openFailed(QString(),
                        QStringLiteral("module '%1' not registered").arg(id));
        return nullptr;
    }
    IWorkspace *w = m->createNew();
    if (!w) {
        emit openFailed(QString(),
                        QStringLiteral("module '%1' does not support createNew").arg(id));
        return nullptr;
    }
    if (parent) {
        w->setParent(parent);
        w->setWindowTitle(w->displayName());
    }
    return w;
}
