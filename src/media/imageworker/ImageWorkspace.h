#ifndef IMAGEWORKSPACE_H
#define IMAGEWORKSPACE_H

// =============================================================
// ImageWorkspace — 阶段 1 imageWorker 模块的 IWorkspace 实现
//
// 设计动机 (2026-09-03 W1 骨架):
//   阶段 0 ImageWindow 继承 QMainWindow, 不能直接当 IWorkspace (IWorkspace 继承 QWidget).
//   阶段 1 imageWorker 模块化时, 不想重写 ImageWindow (QMainWindow 内部菜单栏/工具栏跟 IWorkspace
//   抽象冲突), 所以新建 ImageWorkspace 包一个 ImageWindow.
//
//   架构:
//     IWorkspace (QWidget) ← 抽象接口
//       └── ImageWorkspace (QWidget)  ← 本类
//             └── m_imageWindow (QMainWindow)  ← 现有 ImageWindow, 不动
//
//   调用方 (MainWindow) 拿到 ImageWorkspace* 后:
//     - IWorkspace 接口: filePath()/moduleId()/isDirty()/save()/saveAs() → 转发到 m_imageWindow
//     - 类型判断: qobject_cast<ImageWorkspace*>(ws) 或 ws->moduleId() == "imageWorker"
//     - 取 ImageWindow* 用 imageWindow() getter (saving / undo / close 等主窗口专有功能)
//
//   MediaDispatcher::openFile() 返 IWorkspace*, 调用方强转 ImageWorkspace* 后用 imageWindow()
//   拿到底层 QMainWindow 调专有方法.
//
// 生命周期:
//   - parent (QTabWidget) delete 时, ImageWorkspace delete, m_imageWindow delete (setParent 后跟随)
//   - IWorkspace::setParent 由 MediaDispatcher::openFile() 内部调用
// =============================================================

#include "../../core/IModule.h"

class ImageWindow;

class ImageWorkspace : public IWorkspace
{
    Q_OBJECT
public:
    explicit ImageWorkspace(QWidget *parent = nullptr);
    ~ImageWorkspace() override;

    // ----- IWorkspace 接口 -----
    QString filePath()    const override;
    QString moduleId()    const override { return QStringLiteral("imageWorker"); }
    QString displayName() const override;
    bool    isDirty()     const override;
    bool    save()        override;
    bool    saveAs(const QString &path) override;
    bool    loadFile(const QString &path) override;

    // ----- 阶段 1 W1 公开接口: 取底层 ImageWindow (主窗口专有功能: undo/redo/selectAll 等) -----
    ImageWindow *imageWindow() const { return m_imageWindow; }

private:
    ImageWindow *m_imageWindow = nullptr;
};

#endif // IMAGEWORKSPACE_H
