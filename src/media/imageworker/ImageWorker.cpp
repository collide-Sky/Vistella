// =============================================================
// ImageWorker 实现
//  骨架 — 详见 ImageWorker.h 注释
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
    mi.iconPath.clear();   //   主题系统加 (未实装)
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
    //  暂未实现空白画布, 返 nullptr 让 MediaDispatcher 报 "not support createNew"
    // W4 图层系统再加 (new blank cv::Mat + createNew)
    return nullptr;
}
