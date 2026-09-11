#ifndef IMODULE_H
#define IMODULE_H

// =============================================================
// IModule / IWorkspace — 4 大处理模块统一接口
//   4 个 Worker 都实现这两个接口:
//     imageWorker   图像处理
//     audioWorker   音频处理
//     videoWorker   视频处理
//     visionWorker  视觉处理 (YOLO 工业检测)
//
// 设计原则:
//   - 接口在 src/core, 任何模块都能 include
//   - 纯虚函数 + inline 默认实现, 不依赖具体模块
//   - IModule 是"工厂", IWorkspace 是"实例"
//   - MediaDispatcher 通过 IModule* 路由打开请求
//
// 注意: src/core 不能依赖 src/image/src/audio/..., 避免循环依赖
//   IModule.h 只 include QtCore + QtGui + QtWidgets, 不 include 任何具体模块头
// =============================================================

#include <QString>
#include <QStringList>
#include <QWidget>

// IWorkspace 在下方 line 75 完整定义, 但 IModule (line 41) 内部
//   成员函数 (e.g. virtual IWorkspace *openFile(...)) 用了 IWorkspace* 指针.
//   指针的 forward declaration 即可, 不需要完整定义.
//   测试代码 (继承 IWorkspace) 在 tst_MediaDispatcher.cpp 顶部 #include "IModule.h" 后
//   完整定义可见, 能正常继承.
class IWorkspace;

// 模块元信息 (id / 显示名 / 支持的扩展名 / 图标)
struct ModuleInfo {
    QString id;              // 唯一标识, 全小写, e.g. "imageWorker"
    QString name;            // 显示名, e.g. "图像处理"
    QStringList extensions;  // 支持的扩展名 (含点, 小写), e.g. {".png", ".jpg"}
    QString iconPath;        // 资源图标路径, 空 = 用默认

    bool isValid() const { return !id.isEmpty(); }
};

// =============================================================
// IModule: 模块工厂
// =============================================================
class IModule {
public:
    virtual ~IModule() = default;

    // 模块元信息
    virtual ModuleInfo info() const = 0;

    // 是否能打开 path (默认按扩展名匹配; 子类可重写做更细粒度判断)
    virtual bool canOpen(const QString &path) const
    {
        if (!path.isEmpty() && info().extensions.contains(suffixOf(path).toLower())) {
            return true;
        }
        return false;
    }

    // 打开文件, 返回工作区 widget (会被插入到 QTabWidget)
    //   返回 nullptr 表示加载失败
    //   工作区生命周期归调用方管 (一般 insert 到 tabWidget 后由 tabWidget 管理)
    virtual IWorkspace *openFile(const QString &path) = 0;

    // 创建空工作区 (新建), nullptr 表示该模块不支持新建
    //   比如 imageWorker 支持新建空白工程, audioWorker 暂不支持
    virtual IWorkspace *createNew() { return nullptr; }

protected:
    // 工具: 提取文件后缀 (含点, 小写)
    static QString suffixOf(const QString &path)
    {
        const int dot = path.lastIndexOf(QLatin1Char('.'));
        if (dot < 0) return QString();
        return path.mid(dot).toLower();
    }
};

// =============================================================
// IWorkspace: 单个打开的文件 / 工程
//   4 大模块的窗口类都继承 IWorkspace:
//     ImageWindow, AudioWindow, VideoWindow, VisionWindow
// =============================================================
class IWorkspace : public QWidget
{
    Q_OBJECT
public:
    explicit IWorkspace(QWidget *parent = nullptr) : QWidget(parent) {}
    ~IWorkspace() override = default;

    // ----- 文件信息 -----
    virtual QString filePath() const = 0;       // 当前文件路径, 空 = 未保存
    virtual QString moduleId() const = 0;       // 来自 IModule::info().id
    virtual QString displayName() const = 0;    // tab 显示名 (含 dirty 标记)

    // ----- 状态 -----
    virtual bool isDirty() const = 0;           // 是否有未保存修改

    // ----- 操作 -----
    virtual bool save() = 0;                    // 保存到 filePath(); 没路径调 saveAs
    virtual bool saveAs(const QString &path) = 0;
    virtual bool loadFile(const QString &path) = 0;  // 加载文件 (openFile 内部用)

signals:
    // dirty 状态变化 (影响 tab 显示 "*" 标记)
    void dirtyChanged(bool dirty);
    // 文件路径变化 (saveAs 后, tab title 更新)
    void filePathChanged(const QString &path);
    // 关闭请求 (让 MainWindow 处理 tab 关闭)
    void closeRequested();
    // 显示名变化 (文件名/标题/状态变更触发)
    void displayNameChanged(const QString &name);
};

#endif // IMODULE_H
