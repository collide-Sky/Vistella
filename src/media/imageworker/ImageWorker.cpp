// =============================================================
// ImageWorker 实现
// 阶段 1 W1 骨架 (2026-09-03) — 详见 ImageWorker.h 注释
// =============================================================

#include "ImageWorker.h"
#include "ImageWorkspace.h"

#include <QFileInfo>
#include <QObject>
#include <QString>

ModuleInfo ImageWorker::info() const
{
    ModuleInfo mi;
    mi.id = QStringLiteral("imageWorker");
    mi.name = QStringLiteral("图像处理");
    // 跟 FileExtensionRegistry::extensionsFor("imageWorker") 一致
    mi.extensions = {
        QStringLiteral(".png"),
        QStringLiteral(".jpg"),
        QStringLiteral(".jpeg"),
        QStringLiteral(".bmp"),
        QStringLiteral(".webp"),
        QStringLiteral(".tif"),
        QStringLiteral(".tiff"),
    };
    mi.iconPath.clear();   // 阶段 1+ 阶段 6 主题系统加
    return mi;
}

IWorkspace *ImageWorker::openFile(const QString &path)
{
    if (path.isEmpty()) return nullptr;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return nullptr;
    if (!info().extensions.contains(fi.suffix().toLower().prepend('.'))) {
        return nullptr;   // 不支持的扩展名
    }
    auto *ws = new ImageWorkspace();
    if (!ws->loadFile(path)) {
        delete ws;
        return nullptr;
    }
    return ws;
}

IWorkspace *ImageWorker::createNew()
{
    // 阶段 1 W1 暂未实现空白画布, 返 nullptr 让 MediaDispatcher 报 "not support createNew"
    // W4 图层系统再加 (new blank cv::Mat + createNew)
    return nullptr;
}
