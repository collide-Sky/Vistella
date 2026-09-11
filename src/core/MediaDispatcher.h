#ifndef MEDIADISPATCHER_H
#define MEDIADISPATCHER_H

// =============================================================
// MediaDispatcher — 中央文件分发器 (阶段 0 第 2 步)
//
// 职责:
//   1. 维护 4 大处理模块 (imageWorker / audioWorker / videoWorker / visionWorker) 的注册表
//   2. 按文件扩展名路由到对应模块的 openFile()
//   3. 按模块 id 路由到对应模块的 createNew()
//
// 设计原则:
//   - 单例 (MediaDispatcher::instance()), 跨模块共享一个分发中心
//   - 路由结果通过 IWorkspace* 返回, 调用方 (MainWindow) 负责 insert 到 QTabWidget
//   - 失败 (无模块 / 模块拒绝) 通过 openFailed 信号告知, 不直接弹窗
//     (弹窗/状态栏交给 MainWindow 决定, 分发器只负责"找谁来做")
//   - 不依赖 src/ui 任何具体模块, 避免循环依赖
//   - 不依赖 HomeModule enum (那个在 homepage.h), 接口用 string id 解耦
// =============================================================

#include "IModule.h"

#include <QObject>
#include <QList>
#include <QHash>
#include <QString>

class MediaDispatcher : public QObject
{
    Q_OBJECT
public:
    static MediaDispatcher &instance();

    // ----- 模块注册 (main.cpp 启动时 / 插件加载时调) -----
    void registerModule(IModule *mod);
    void unregisterModule(IModule *mod);
    void unregisterAll();

    // ----- 查询 -----
    QList<IModule *> modules() const { return m_modules; }
    int  moduleCount() const { return m_modules.size(); }
    IModule *moduleForId(const QString &id) const;
    IModule *moduleForFile(const QString &path) const;

    // 扩展名是否在路由表里 (即使模块没注册, 也返回 true)
    //   用于 openFile 区分 "未知扩展名" vs "未注册模块"
    bool  isExtensionKnown(const QString &path) const;

    // ----- 路由: 打开文件 -----
    //   找模块 → 调 module->openFile(path) → 返回 workspace
    //   失败返回 nullptr, 同时 emit openFailed(path, reason)
    //   parent: 创建的 workspace 会被设成 parent 的子 widget (生命周期由 parent 管)
    IWorkspace *openFile(const QString &path, QWidget *parent = nullptr);

    // ----- 路由: 启动模块空白工作区 -----
    //   找模块 → 调 module->createNew() → 返回 workspace
    //   id 是模块的 IModule::info().id (e.g. "imageWorker")
    //   阶段 0 时各 Worker 还没建, createNew 返回 nullptr → 弹"暂未实现"提示
    IWorkspace *openModuleById(const QString &id, QWidget *parent = nullptr);

signals:
    // 模块列表变化 (SettingsDialog 调试面板用)
    void modulesChanged();
    // 打开失败 (MainWindow 收到后弹提示)
    void openFailed(const QString &path, const QString &reason);

private:
    explicit MediaDispatcher(QObject *parent = nullptr);
    MediaDispatcher(const MediaDispatcher &) = delete;
    MediaDispatcher &operator=(const MediaDispatcher &) = delete;

    // 注册/反注册时重建扩展名 → 模块的 hash, 加速查找
    void rebuildExtMap();

    QList<IModule *> m_modules;
    // 扩展名 (小写, 含点) → 模块指针, 多个模块支持同扩展名时后注册覆盖前面的
    QHash<QString, IModule *> m_extMap;
    // 模块 id → 模块指针
    QHash<QString, IModule *> m_idMap;
};

#endif // MEDIADISPATCHER_H
