// SPDX-License-Identifier: MIT
//
// ImageIOController — QObject,接管 onOpen / onSave / onSaveAs / flatten
// (P0-1: 拆 imagewindow.cpp 上帝类的 6 大组件之一)
//
#pragma once

#include <QObject>
#include <QString>

#include <opencv2/core.hpp>

class ImageWindow;

class ImageIOController : public QObject {
    Q_OBJECT
public:
    explicit ImageIOController(QObject* parent = nullptr);
    ~ImageIOController() override;

    void setHost(ImageWindow* w) { m_host = w; }

    // Toolbar actions.
    void onOpen();
    void onSave();
    void onSaveAs();
    // 多格式导出 (走 ExportDialog)
    void onExport();
    void onClose();

    // Read/write current image.  saveAs returns the path written or "" on
    // cancel/error.  err is optional, populated on failure.
    bool    loadFile(const QString& path, QString* err = nullptr);
    QString saveAs();   // prompts QFileDialog, returns chosen path
    bool    save();     // overwrites m_filePath

signals:
    // Mirrors ImageWindow's existing signals so MainWindow wiring doesn't
    // need to change.
    // TODO(P8.5): the three ImageIOController signals have no connect, so save failures stay silent -- see docs/DEBT.md#A-IMAGESAVESIGNAL
    void filePathChanged(const QString& path);
    void closeRequested();
    void dirtyChanged(bool dirty);

private:
    QString promptForSavePath() const;
    QString promptForOpenPath() const;
    bool    writeImage(const QString& path);

    // 写盘用的合成图 = 文档像素 + 文字 item.
    // 一次性渲染, 不改场景和撤销栈; 合成失败时退回原始像素.
    bool    buildSaveComposite(cv::Mat& out) const;

    ImageWindow* m_host = nullptr;
};
