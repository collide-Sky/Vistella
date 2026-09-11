#ifndef FILETREENODEDATA_H
#define FILETREENODEDATA_H

// =============================================================
// FileTreeNodeData — 文件树节点数据 (2026-09-02 决策 5 重构)
//
// 跟 FileTreeItem 分开:
//   - FileTreeItem 管树结构 (parent / children / row)
//   - FileTreeNodeData 管节点内容 (文件名 / 路径 / 图标 / 大小)
//
// 单一职责: 只装 payload, 不含任何 QObject 信号/树逻辑
//   - 方便以后扩展 (比如加 tag / thumbnail / 状态)
//   - 方便序列化 / 调试
//   - 跟 QAbstractItemModel 的 data() 配合: model 从 item.data() 拿 payload
//     再根据 role 返回不同字段
// =============================================================

#include <QString>
#include <QDateTime>

struct FileTreeNodeData {
    QString    fileName;        // 显示名: "image.png"
    QString    absolutePath;    // 绝对路径: "D:/.../image.png"
    bool       isDir = false;   // true = 目录, false = 文件
    QString    iconKey;         // 复用 QStyle::StandardPixmap 字符串 key, e.g. "Dir", "File"
    qint64     fileSize = 0;    // 字节; 目录 = 0
    QDateTime  lastModified;    // 修改时间
    QString    moduleId;        // 文件: 命中的模块 id (imageWorker 等); 目录: 空
    bool       supportedExt = false;  // 文件: 扩展名是否在决策 5 表里; 目录: 永远 true

    bool isValid() const { return !absolutePath.isEmpty(); }
};

#endif // FILETREENODEDATA_H
