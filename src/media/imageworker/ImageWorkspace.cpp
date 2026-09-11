// =============================================================
// ImageWorkspace 实现
// 阶段 1 W1 骨架 (2026-09-03) — 详见 ImageWorkspace.h 注释
// =============================================================

#include "ImageWorkspace.h"

#include "../imagewindow.h"
#include "../imageprocessor.h"

#include <QFileInfo>
#include <QBoxLayout>
#include <QWidget>

// ---- 构造 ----
//   用 QVBoxLayout 包 ImageWindow, 让 IWorkspace 内部一个 QMainWindow 显示
//   注意: ImageWindow 已经是 QMainWindow, 它自己管理 menuBar/toolBar/statusBar,
//         IWorkspace 只负责把 QMainWindow 嵌入自己
ImageWorkspace::ImageWorkspace(QWidget *parent)
    : IWorkspace(parent)
    , m_imageWindow(new ImageWindow(this))   // parent = this, 跟随 IWorkspace 析构
{
    // IWorkspace 内部是 QWidget, 装一个 QVBoxLayout 容纳 QMainWindow
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_imageWindow);

    setWindowTitle(m_imageWindow->displayTitle());

    // 转发 ImageWindow 信号到 IWorkspace 信号, 让 MainWindow 能接 dirty/filePath/close
    connect(m_imageWindow, &ImageWindow::dirtyChanged,    this, &IWorkspace::dirtyChanged);
    connect(m_imageWindow, &ImageWindow::filePathChanged,  this, &IWorkspace::filePathChanged);
    connect(m_imageWindow, &ImageWindow::closeRequested,   this, &IWorkspace::closeRequested);
    // displayNameChanged: ImageWindow::displayTitle 变化时刷新 tab title
    //   ImageWindow 内部没 displayNameChanged 信号, 改 filePath / dirty 时直接刷新
    connect(m_imageWindow, &ImageWindow::dirtyChanged,    this, [this]{
        emit displayNameChanged(displayName());
    });
    connect(m_imageWindow, &ImageWindow::filePathChanged,  this, [this]{
        emit displayNameChanged(displayName());
    });
}

ImageWorkspace::~ImageWorkspace()
{
    // m_imageWindow parent = this, 自动 delete
}

// ---- IWorkspace 接口 (全部转发到 m_imageWindow) ----

QString ImageWorkspace::filePath() const
{
    // P0-1.3 (2026-09-07): QString(m_imageWindow->filePath()) 强拷贝, ref count +1
    // 避免直接返回 inline accessor 的 QString 引用, 导致 m_imageWindow 还在
    // 但 QString 内部 d 指针被覆盖时的 use-after-free.
    return m_imageWindow ? QString(m_imageWindow->filePath()) : QString();
}

QString ImageWorkspace::displayName() const
{
    if (!m_imageWindow) return QString();
    const QString title = m_imageWindow->displayTitle();
    return title.isEmpty() ? m_imageWindow->filePath() : title;
}

bool ImageWorkspace::isDirty() const
{
    return m_imageWindow ? m_imageWindow->isDirty() : false;
}

bool ImageWorkspace::save()
{
    if (!m_imageWindow) return false;
    m_imageWindow->savePublic();
    // ImageWindow::onSave 内部会处理保存, 但它不返 bool
    // 简化: 假设 savePublic 成功 (现有 mainwindow.cpp 也是这样用)
    return true;
}

bool ImageWorkspace::saveAs(const QString &path)
{
    if (!m_imageWindow) return false;
    m_imageWindow->saveAsPublic();   // 现有 ImageWindow::onSaveAs 弹文件对话框, 不接 path
    return m_imageWindow->filePath() == path;
}

bool ImageWorkspace::loadFile(const QString &path)
{
    if (!m_imageWindow) return false;
    QString err;
    const bool ok = m_imageWindow->loadFile(path, &err);
    if (ok) {
        setWindowTitle(m_imageWindow->displayTitle());
        emit displayNameChanged(displayName());
        emit filePathChanged(m_imageWindow->filePath());
    }
    return ok;
}
