// SPDX-License-Identifier: MIT
//
// ImageIOController — QObject,接管 onOpen / onSave / onSaveAs / flatten
// (P0-1: 拆 imagewindow.cpp 上帝类的 6 大组件之一)
//
#pragma once

#include <QObject>
#include <QString>

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
    void onClose();

    // Read/write current image.  saveAs returns the path written or "" on
    // cancel/error.  err is optional, populated on failure.
    bool    loadFile(const QString& path, QString* err = nullptr);
    QString saveAs();   // prompts QFileDialog, returns chosen path
    bool    save();     // overwrites m_filePath

signals:
    // Mirrors ImageWindow's existing signals so MainWindow wiring doesn't
    // need to change.
    void filePathChanged(const QString& path);
    void closeRequested();
    void dirtyChanged(bool dirty);

private:
    QString promptForSavePath() const;
    QString promptForOpenPath() const;
    bool    writeImage(const QString& path);

    ImageWindow* m_host = nullptr;
};
