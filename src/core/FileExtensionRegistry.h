#ifndef FILEEXTENSIONREGISTRY_H
#define FILEEXTENSIONREGISTRY_H

// =============================================================
// FileExtensionRegistry — 全局文件扩展名注册表 (决策 5)
//
// 职责 (单一数据源):
//   1. 维护 4 大处理模块的扩展名清单 (imageWorker / audioWorker / videoWorker / visionWorker)
//   2. 给 MediaDispatcher / InfoTreeDock / QFileDialog 共享
//   3. visionWorker 跟 imageWorker 共享扩展名 (vision = 在图像上跑 YOLO/检测)
//
// 设计原则:
//   - 纯静态 + namespace, 不需要单例, 不需要 new
//   - 放在 src/core, src/ui 和 src/app 都能 include
//   - 阶段 0: 写死. 阶段 1+ IModule 注册时, MediaDispatcher 用 union (注册 ∪ 内置默认)
//     避免注册空 module 时所有文件都"无法路由"
// =============================================================

#include <QString>
#include <QStringList>
#include <QHash>

namespace FileExtensionRegistry {

// 模块 id (跟 IModule::info().id 一致, 阶段 0 IModule 还没注册时 MediaDispatcher 用这个)
namespace ModuleId {
inline constexpr const char *ImageWorker  = "imageWorker";
inline constexpr const char *AudioWorker  = "audioWorker";
inline constexpr const char *VideoWorker  = "videoWorker";
inline constexpr const char *VisionWorker = "visionWorker";
} // namespace ModuleId

// 内置默认扩展名清单 (决策 5, 2026-09-02 跟用户确认)
//   - 全部小写, 含点, e.g. ".png"
//   - visionWorker 跟 imageWorker 共享 (vision 跑在图像上)
QStringList extensionsFor(const QString &moduleId);

// 反查: 扩展名 -> 模块 id (阶段 0 默认表; IModule 注册后取并集)
//   "image.png" -> "imageWorker"
//   ".png"      -> "imageWorker"
//   找不到 -> 空 QString
QString moduleIdForExtension(const QString &suffixOrPath);

// 判断扩展名是否被任一模块支持 (InfoTreeDock 过滤用)
//   "image.png" -> true
//   ".png"      -> true
//   ".xyz"      -> false
bool isSupportedExtension(const QString &suffixOrPath);

// 全部支持的扩展名 (扁平 list, 给 QFileDialog filter 用)
//   返回: [".png", ".jpg", ..., ".mp3", ..., ".mp4", ...]
QStringList allSupportedExtensions();

// 全部模块 id (给 SettingsDialog / About 面板调试用)
QStringList allModuleIds();

// 内部: 模块 id -> 扩展名列表 (初始化一次, 给上面查询加速)
const QHash<QString, QStringList> &moduleTable();

} // namespace FileExtensionRegistry

#endif // FILEEXTENSIONREGISTRY_H
