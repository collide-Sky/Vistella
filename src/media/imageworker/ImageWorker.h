#ifndef IMAGEWORKER_H
#define IMAGEWORKER_H

// =============================================================
// ImageWorker — imageWorker 模块的 IModule 实现
//
// 设计动机 (2026-09-03 W1 骨架):
//    MediaDispatcher 知道 imageWorker 但注册表是空的 (FileExtensionRegistry 默认表)
//    第一个任务: 把现有 ImageWindow 通过 IModule 接口注册到 MediaDispatcher
//   让 MainWindow openFile(path) 走 MediaDispatcher 路由, 而不是直接 new ImageWindow.
//
// 职责:
//   1. info() 返 ModuleInfo (id="imageWorker", name="图像处理", exts=[.png/.jpg/.jpeg/.bmp/.webp/.tif/.tiff])
//   2. openFile(path) 创建 ImageWorkspace, 内部包 ImageWindow, 调 loadFile 加载图片
//   3. createNew 暂未实现 (不做空白画布, 留给 图层系统)
//
//  扩展 — 未接线
//   - W2: 滤镜系统 (在 ImageWorker 这层不直接管, 滤镜是 ImageWindow 内部 ImageEditCommand 责任)
//   - W3: 色彩调整 UI (同上)
//   - W4: createNew() 实现空白画布 (new cv::Mat + blank pixmap)
//   AI 工具 (imageWorker 集成 yolov8n / realesrgan-x4 / sam / gfpgan) — 未实装
//
// 单元测试: tst_ImageWorker 测模块注册 + 路由
// =============================================================

#include "../../core/IModule.h"

class ImageWorker : public IModule
{
public:
    // IModule 不是 QObject, 构造不需要 QObject parent
    //   内存管理: 模块实例由 MediaDispatcher 单例通过 registerModule 持有,
    //   main.cpp 用栈对象 + 不要 delete (随 MediaDispatcher 析构)
    explicit ImageWorker() = default;

    // ----- IModule 接口 -----
    ModuleInfo info() const override;
    IWorkspace *openFile(const QString &path) override;
    IWorkspace *createNew() override;   //  暂未实现, 返 nullptr

    // ----- 模块 id (跟 FileExtensionRegistry::ModuleId::ImageWorker 一致) -----
    static QString moduleId() { return QStringLiteral("imageWorker"); }
};

#endif // IMAGEWORKER_H
