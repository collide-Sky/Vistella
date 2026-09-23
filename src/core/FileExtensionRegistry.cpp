#include "FileExtensionRegistry.h"

#include <QFileInfo>

namespace FileExtensionRegistry {

namespace {

// 决策 5 扩展名清单 (2026-09-02 跟用户确认, 阶段 1 扩展图形处理, 3D 模型相关全删)
struct ModuleExt {
    const char *id;
    QStringList exts;
};

const QHash<QString, QStringList> &buildTable()
{
    static const QHash<QString, QStringList> kTable = {
        // imageWorker - 图像处理 (PS+Lr 风格, 阶段 1 主战场)
        { QStringLiteral("imageWorker"), {
            QStringLiteral(".png"),
            QStringLiteral(".jpg"),
            QStringLiteral(".jpeg"),
            QStringLiteral(".bmp"),
            QStringLiteral(".webp"),
            QStringLiteral(".tif"),
            QStringLiteral(".tiff"),
            // P3.5 (2026-09-23): Camera Raw 扩展名 (libraw 解码).
            //   不依赖 libraw 编译 (CameraRawLoader 提供 stub 接口); 用户
            //   启动后调 onOpenRawFile 自动拦截这些扩展名, 没 libraw 时返错误.
            QStringLiteral(".cr2"),
            QStringLiteral(".cr3"),
            QStringLiteral(".nef"),
            QStringLiteral(".arw"),
            QStringLiteral(".dng"),
            QStringLiteral(".raf"),
            QStringLiteral(".orf"),
            QStringLiteral(".rw2"),
            QStringLiteral(".pef"),
            QStringLiteral(".srw"),
            QStringLiteral(".x3f"),
            QStringLiteral(".nrw"),
        }},
        // audioWorker - 音频处理 (Audition+ffmpeg, 阶段 2 扩展)
        { QStringLiteral("audioWorker"), {
            QStringLiteral(".wav"),
            QStringLiteral(".mp3"),
            QStringLiteral(".flac"),
            QStringLiteral(".aac"),
            QStringLiteral(".ogg"),
        }},
        // videoWorker - 视频处理 (PR+剪映, 阶段 3 扩展)
        { QStringLiteral("videoWorker"), {
            QStringLiteral(".mp4"),
            QStringLiteral(".mkv"),
            QStringLiteral(".avi"),
            QStringLiteral(".mov"),
            QStringLiteral(".webm"),
        }},
        // visionWorker - 视觉处理 (YOLO 工业检测, 阶段 1 扩展图形处理)
        //   跟 imageWorker 共享扩展名 (vision 跑在图像上)
        { QStringLiteral("visionWorker"), {
            QStringLiteral(".png"),
            QStringLiteral(".jpg"),
            QStringLiteral(".jpeg"),
            QStringLiteral(".bmp"),
            QStringLiteral(".webp"),
            QStringLiteral(".tif"),
            QStringLiteral(".tiff"),
        }},
    };
    return kTable;
}

// 从 "image.png" / ".png" / "D:/a/b.png" 里抽出 ".png" (小写, 含点)
QString normalizeSuffix(const QString &suffixOrPath)
{
    if (suffixOrPath.isEmpty()) return QString();
    // 先看是不是 ".xxx" 形式
    if (suffixOrPath.startsWith(QLatin1Char('.')) && !suffixOrPath.contains(QLatin1Char('/'))
        && !suffixOrPath.contains(QLatin1Char('\\'))) {
        return suffixOrPath.toLower();
    }
    // 否则按文件路径处理
    const int dot = suffixOrPath.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return QString();
    return suffixOrPath.mid(dot).toLower();
}

} // namespace

const QHash<QString, QStringList> &moduleTable()
{
    return buildTable();
}

QStringList extensionsFor(const QString &moduleId)
{
    return buildTable().value(moduleId);
}

// 阶段 1 前置优化 (2026-09-03): 优先返回通用模块 (imageWorker / audioWorker / videoWorker),
//   visionWorker (工业检测/YOLO) 作为兜底.
//   决策 5 中 visionWorker 跟 imageWorker 共享扩展名, QHash 遍历顺序未定义,
//   默认走通用 imageWorker 更符合用户期望 (用户拖普通照片不应自动走 YOLO).
QString moduleIdForExtension(const QString &suffixOrPath)
{
    const QString suf = normalizeSuffix(suffixOrPath);
    if (suf.isEmpty()) return QString();
    const auto &tbl = buildTable();
    // 第一轮: 通用模块按声明顺序查找
    static const QStringList kGeneralPriority = {
        QStringLiteral("imageWorker"),
        QStringLiteral("audioWorker"),
        QStringLiteral("videoWorker"),
    };
    for (const QString &id : kGeneralPriority) {
        if (tbl.value(id).contains(suf)) {
            return id;
        }
    }
    // 第二轮: 视觉模块 (visionWorker) 兜底
    if (tbl.value(QStringLiteral("visionWorker")).contains(suf)) {
        return QStringLiteral("visionWorker");
    }
    return QString();
}

bool isSupportedExtension(const QString &suffixOrPath)
{
    return !moduleIdForExtension(suffixOrPath).isEmpty();
}

QStringList allSupportedExtensions()
{
    QStringList out;
    const auto &tbl = buildTable();
    for (auto it = tbl.cbegin(); it != tbl.cend(); ++it) {
        for (const QString &e : it.value()) {
            if (!out.contains(e)) out.append(e);
        }
    }
    out.sort();
    return out;
}

QStringList allModuleIds()
{
    QStringList out;
    const auto &tbl = buildTable();
    for (auto it = tbl.cbegin(); it != tbl.cend(); ++it) {
        out.append(it.key());
    }
    out.sort();
    return out;
}

} // namespace FileExtensionRegistry
