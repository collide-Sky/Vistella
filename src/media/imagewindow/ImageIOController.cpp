// SPDX-License-Identifier: MIT
//
// ImageIOController - P0-1.4 (2026-09-07) full impl
//   接管 imagewindow.cpp 中所有"打开/保存/另存为/关闭"逻辑:
//     - onOpen: QFileDialog -> m_host->loadFile + RecentManager::touchOpen
//     - onSave: 委托 onSaveAs 如果 m_host->filePath() 空; flattenText + ImageProcessor::saveImage
//               + m_host->markSaved() + RecentManager::touchEdit + m_host->emitEditTimeShouldUpdate()
//     - onSaveAs: QFileDialog -> flattenText + ImageProcessor::saveImage
//                 + m_host->setFilePath(path) (emit filePathChanged)
//                 + m_host->markSaved() + RecentManager::touchEdit + m_host->emitEditTimeShouldUpdate()
//     - onClose: m_host->emitCloseRequested() (MainWindow 听 ImageWindow::closeRequested)
//     - loadFile: 薄包装, 调 m_host->loadFile(path, err)
//   设计原则 (P0-1.2/P0-1.3 风格延续):
//     - ImageIOController 持 m_host (raw pointer) 作为唯一私有成员
//     - 所有 emit 走 m_host 的代理方法 (setFilePath / emitEditTimeShouldUpdate / emitCloseRequested)
//     - 私有方法 (markSaved) 通过 friend class 访问
//   保持原代码语义, 仅**位置**移动, 不**逻辑**删除/简化.
//
#include "ImageIOController.h"
#include "../imagewindow.h"
#include "TextOverlayController.h"
#include "../imageprocessor.h"
#include "../../core/RecentManager.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

ImageIOController::ImageIOController(QObject* parent) : QObject(parent) {}
ImageIOController::~ImageIOController() = default;

// 打开图像: QFileDialog 选路径 -> m_host->loadFile -> RecentManager 记一条
//   主流做法: 用 cv::imread (IMREAD_UNCHANGED) 保留原 channels (灰度图保存后还能 reload)
//   失败时弹 QMessageBox.warning 提示, 不入 recent
void ImageIOController::onOpen()
{
    if (!m_host) return;
    // 强拷贝 m_host->filePath(): 避免 inline accessor 返 QString 引用被覆盖
    const QString fp = QString(m_host->filePath());
    const QString path = QFileDialog::getOpenFileName(
        m_host, tr("打开图像"), QFileInfo(fp).absolutePath(),
        tr("图像 (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);; 全部文件 (*)"));
    if (path.isEmpty()) return;
    QString err;
    if (!m_host->loadFile(path, &err)) {
        QMessageBox::warning(m_host, tr("打开失败"), err);
        return;
    }
    RecentManager::instance().touchOpen(path, RecentModeMultimedia);
}

// 保存: 已有路径直接覆盖, 没路径走 onSaveAs
//   主流做法: 保存前先扁平化所有文字图层 (扁平化后字变成图片像素, 不可撤销/编辑)
void ImageIOController::onSave()
{
    if (!m_host) return;
    // 强拷贝 m_host->filePath(): 避免 inline accessor 返 QString 引用被覆盖
    const QString fp = QString(m_host->filePath());
    if (fp.isEmpty()) { onSaveAs(); return; }
    // 主流做法: 保存前先扁平化所有文字图层 (扁平化后字变成图片像素, 不可撤销/编辑)
    if (m_host->textOverlay()) m_host->textOverlay()->flattenText();
    QString err;
    if (!ImageProcessor::saveImage(m_host->currentImage(), fp, &err)) {
        QMessageBox::warning(m_host, tr("保存失败"), err);
        return;
    }
    m_host->markSaved();
    RecentManager::instance().touchEdit(fp);
    m_host->emitEditTimeShouldUpdate();
}

// 另存为: QFileDialog 选新路径 -> 写图 -> 触发 m_host 改路径 (emit filePathChanged) + markSaved
//   注意 m_host->setFilePath 内部已经 emit filePathChanged (跟原 onSaveAs 内联一致)
void ImageIOController::onSaveAs()
{
    if (!m_host) return;
    // 强拷贝 m_host->filePath(): 避免 inline accessor 返 QString 引用被覆盖
    const QString fp = QString(m_host->filePath());
    const QString suggest = fp.isEmpty()
        ? (QDir::homePath() + QStringLiteral("/untitled.png"))
        : fp;
    const QString path = QFileDialog::getSaveFileName(
        m_host, tr("另存为"), suggest,
        tr("图像 (*.png *.jpg *.bmp);; 全部文件 (*)"));
    if (path.isEmpty()) return;
    // 主流做法: 保存前先扁平化所有文字图层
    if (m_host->textOverlay()) m_host->textOverlay()->flattenText();
    QString err;
    if (!ImageProcessor::saveImage(m_host->currentImage(), path, &err)) {
        QMessageBox::warning(m_host, tr("保存失败"), err);
        return;
    }
    m_host->setFilePath(path);
    m_host->markSaved();
    RecentManager::instance().touchEdit(path);
    m_host->emitEditTimeShouldUpdate();
}

// 关闭: 不在这里直接关 widget, 而是发信号让 MainWindow 统一走 tabCloseRequested 路径
//   避免 parentWidget()->close() 误关 tabWidget 自身
void ImageIOController::onClose()
{
    if (!m_host) return;
    m_host->emitCloseRequested();
}

// 薄包装: 实际逻辑在 ImageWindow::loadFile (P0-1.0 已实装, 不搬)
//   这里只转发, 让 ImageIOController 持 loadFile 入口 (跟 P0-1.1 接口签名一致)
bool ImageIOController::loadFile(const QString& path, QString* err)
{
    if (!m_host) return false;
    return m_host->loadFile(path, err);
}

// ---- 以下是 P0-1.1 拍板的占位接口, 实际未使用, 保持空实现 ----
//   P0-1.4 (2026-09-07): 实际 IO 逻辑都在 onOpen / onSave / onSaveAs / onClose / loadFile 里
//   saveAs / save 不暴露给外部用, 这里保留签名以兼容 P0-1.1 接口
QString ImageIOController::saveAs()  { return QString(); }
bool    ImageIOController::save()    { return false; }
QString ImageIOController::promptForSavePath() const { return QString(); }
QString ImageIOController::promptForOpenPath() const { return QString(); }
bool    ImageIOController::writeImage(const QString& /*path*/) { return false; }
