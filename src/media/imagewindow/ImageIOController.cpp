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
#include "camera_raw/CameraRawLoader.h"
#include "camera_raw/CameraRawDialog.h"
#include "TextOverlayController.h"
#include "../imageprocessor.h"
#include "../dialogs/ExportDialog.h"
#include "../icc/IccProfile.h"
#include "../icc/IccPngEmbed.h"
#include "../icc/IccJpegEmbed.h"
#include "../icc/IccTiffEmbed.h"
#include "../../core/RecentManager.h"
#include "logger.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>
#include <QSharedPointer>

#include <opencv2/imgcodecs.hpp> // P3.5 follow-up cv::imwrite for RAW temp
#include <opencv2/imgproc.hpp>   // P0-8.2 cv::resize + cv::INTER_AREA

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

// P0-8.2 (2026-09-15): 多格式导出 (PS 同款 Save For Web 风格)
//   ExportDialog 让 user 选 format + quality + resize + ICC
//   ImageProcessor::saveImage 走 QImageWriter 支持完整 quality/compression
void ImageIOController::onExport()
{
    if (!m_host) return;
    // 主流做法: 保存前先扁平化所有文字图层
    if (m_host->textOverlay()) m_host->textOverlay()->flattenText();

    const QString fp = QString(m_host->filePath());
    const QString suggest = fp.isEmpty()
        ? (QDir::homePath() + QStringLiteral("/untitled.png"))
        : fp;

    dialogs::ExportDialog dlg(m_host);
    dlg.setSourceSize(m_host->currentImage().cols, m_host->currentImage().rows);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto opts = dlg.options();
    // 推断默认扩展名
    QString path = QFileDialog::getSaveFileName(
        m_host, tr("导出为..."), suggest,
        tr("PNG (*.png);; JPEG (*.jpg *.jpeg);; TIFF (*.tif *.tiff);; WebP (*.webp);; BMP (*.bmp);; GIF (*.gif);; 全部文件 (*)"));
    if (path.isEmpty()) return;

    // P0-8.2: resize (按 percent)
    cv::Mat img = m_host->currentImage();
    if (opts.resizePercent > 0 && opts.resizePercent < 100) {
        const double scale = opts.resizePercent / 100.0;
        cv::Mat resized;
        cv::resize(img, resized, cv::Size(), scale, scale, cv::INTER_AREA);
        img = resized;
    }

    QString err;
    ImageProcessor::SaveOptions saveOpts;
    saveOpts.quality         = opts.jpegQuality;   // JPEG 跟 WebP 都用 quality
    saveOpts.format          = opts.format == dialogs::ExportDialog::Format::JPEG ? QStringLiteral("JPEG")
                              : opts.format == dialogs::ExportDialog::Format::TIFF ? QStringLiteral("TIFF")
                              : opts.format == dialogs::ExportDialog::Format::WebP ? QStringLiteral("WEBP")
                              : opts.format == dialogs::ExportDialog::Format::BMP  ? QStringLiteral("BMP")
                              : opts.format == dialogs::ExportDialog::Format::GIF  ? QStringLiteral("GIF")
                              : QStringLiteral("PNG");
    saveOpts.pngCompression  = opts.pngCompression;
    saveOpts.tiffCompression = opts.tiffCompression;
    // JPEG/WebP 用 quality, PNG/BMP/GIF 用 pngCompression (BMP/GIF 通常忽略)
    if (opts.format == dialogs::ExportDialog::Format::JPEG
        || opts.format == dialogs::ExportDialog::Format::WebP) {
        saveOpts.quality = opts.format == dialogs::ExportDialog::Format::WebP
                              ? opts.webpQuality : opts.jpegQuality;
    }
    if (!ImageProcessor::saveImage(img, path, saveOpts, &err)) {
        QMessageBox::warning(m_host, tr("导出失败"), err);
        return;
    }

    // P0-8.3 (2026-09-15): 嵌入 ICC profile (PS 同款 Save For Web)
    //   - opts.embedIcc + iccProfilePath 有效
    //   - format 是 PNG -> embedIccToPng (iCCP chunk)
    //   - format 是 JPEG -> embedIccToJpeg (APP2 marker)
    //   - TIFF embed 是 TODO (P0-8.4 follow-up)
    if (opts.embedIcc && !opts.iccProfilePath.isEmpty()) {
        QSharedPointer<media::icc::Profile> profile;
        if (opts.iccProfilePath.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) == 0
            || opts.iccProfilePath.compare(QStringLiteral("builtin-srgb"), Qt::CaseInsensitive) == 0) {
            profile = media::icc::Profile::createSRgb();
        } else if (opts.iccProfilePath.compare(QStringLiteral("AdobeRGB"), Qt::CaseInsensitive) == 0
                   || opts.iccProfilePath.compare(QStringLiteral("builtin-adobergb"), Qt::CaseInsensitive) == 0) {
            profile = media::icc::Profile::createAdobeRgb();
        } else {
            profile = media::icc::Profile::load(opts.iccProfilePath, &err);
        }
        if (!profile) {
            LOG_WARN("[ImageIO] ICC profile load failed: {}", err.toStdString());
        } else {
            const QByteArray iccData = profile->toByteArray();
            if (iccData.isEmpty()) {
                LOG_WARN("[ImageIO] ICC profile toByteArray empty");
            } else if (opts.format == dialogs::ExportDialog::Format::PNG) {
                QString iccName = QFileInfo(opts.iccProfilePath).baseName();
                if (iccName.isEmpty()) iccName = QStringLiteral("ICC Profile");
                if (!media::icc::embedIccToPng(path, iccData, iccName, &err)) {
                    LOG_WARN("[ImageIO] embedIccToPng failed: {}", err.toStdString());
                }
            } else if (opts.format == dialogs::ExportDialog::Format::JPEG) {
                if (!media::icc::embedIccToJpeg(path, iccData, &err)) {
                    LOG_WARN("[ImageIO] embedIccToJpeg failed: {}", err.toStdString());
                }
            } else if (opts.format == dialogs::ExportDialog::Format::TIFF) {
                // P0-8.4 (2026-09-15): TIFF ICCProfile tag embedder
                if (!media::icc::embedIccToTiff(path, iccData, &err)) {
                    LOG_WARN("[ImageIO] embedIccToTiff failed: {}", err.toStdString());
                } else {
                    LOG_INFO("[ImageIO] embedded ICC profile into TIFF");
                }
            }
        }
    }

    // P0-8.2: 导出 = 新路径 (跟 saveAs 一样), 但不 markSaved (导出不影响源文件 save 状态)
    //   user 可以选 "导出后更新源路径" (PS 默认不改)
    LOG_INFO("[ImageIO] export OK: {}", path.toStdString());
}

// 薄包装: 实际逻辑在 ImageWindow::loadFile (P0-1.0 已实装, 不搬)
//   这里只转发, 让 ImageIOController 持 loadFile 入口 (跟 P0-1.1 接口签名一致)
bool ImageIOController::loadFile(const QString& path, QString* err)
{
    if (!m_host) return false;
    // P3.5 (2026-09-23): RAW 拦截 — 调 CameraRawLoader 解码, 不走 cv::imread.
    //   P3.5 follow-up (2026-09-23): 弹 CameraRawDialog 让用户调 exposure/wb
    //   等参数, 然后调 camera_raw::decodeRaw(path, settings). 真 libraw 解码结果
    //   走 cv::imwrite -> temp file -> m_host->loadFile (跟正常打开场景统一路径).
    if (!camera_raw::supportedRawExtensions().contains(
            QFileInfo(path).suffix().prepend(".").toLower())) {
        return m_host->loadFile(path, err);
    }
    if (!camera_raw::hasLibRawSupport()) {
        if (err) *err = QStringLiteral(
            "Camera Raw 解码需要 libraw. "
            "请运行 vcpkg install libraw:x64-windows 并重新配置 CMake.");
        return false;
    }
    // 弹 PS-style Camera Raw dialog 模态等用户 OK.
    camera_raw::CameraRawDialog dlg;
    if (dlg.exec() != QDialog::Accepted) {
        return false;  // user cancelled
    }
    camera_raw::CameraRawSettings settings = dlg.settings();
    camera_raw::CameraRawResult res = camera_raw::decodeRaw(path, settings);
    if (!res.ok()) {
        if (err) *err = res.errorMsg.isEmpty()
                          ? QStringLiteral("Camera Raw decode failed")
                          : res.errorMsg;
        return false;
    }
    // 写临时 PNG, 让 m_host->loadFile 走正常路径 (LayerStack/selection/zoom 初始化).
    //   cv::imwrite 走 cv::Mat -> disk; loadFile 内部 cv::imread + 16 bit -> 8U.
    QString tempPath = QDir::tempPath() + QStringLiteral("/multidoc_raw_")
                       + QFileInfo(path).completeBaseName()
                       + QStringLiteral("_%1.png")
                            .arg(QCoreApplication::applicationPid());
    if (!cv::imwrite(tempPath.toLocal8Bit().toStdString(), res.image)) {
        if (err) *err = QStringLiteral("cv::imwrite temp failed for RAW");
        return false;
    }
    return m_host->loadFile(tempPath, err);
}

// ---- 以下是 P0-1.1 拍板的占位接口, 实际未使用, 保持空实现 ----
//   P0-1.4 (2026-09-07): 实际 IO 逻辑都在 onOpen / onSave / onSaveAs / onClose / loadFile 里
//   saveAs / save 不暴露给外部用, 这里保留签名以兼容 P0-1.1 接口
QString ImageIOController::saveAs()  { return QString(); }
bool    ImageIOController::save()    { return false; }
QString ImageIOController::promptForSavePath() const { return QString(); }
QString ImageIOController::promptForOpenPath() const { return QString(); }
bool    ImageIOController::writeImage(const QString& /*path*/) { return false; }
