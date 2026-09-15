// SPDX-License-Identifier: MIT
//
// ExportDialog - P0-8.2 (2026-09-15)
//
// PS 同款 "Save For Web" 风格导出对话框:
//   - 格式: PNG / JPEG / TIFF / WebP / BMP / GIF
//   - JPEG quality slider 0-100 (默认 92)
//   - PNG compression 0-9 (默认 6)
//   - TIFF compression: None / LZW / Deflate
//   - WebP quality slider 0-100 (默认 90)
//   - resize: 100% / 75% / 50% / 25% / 自定义
//   - 嵌入 ICC profile (P0-8.3 阶段 3 集成)
//
// 集成:
//   - ImageIOController::onExport() 替代 onSaveAs
//   - user 选 OK -> ExportDialog::options() -> ImageProcessor::saveImage(..., options)
//
#pragma once

#include <QDialog>

class QComboBox;
class QSlider;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QFormLayout;

namespace dialogs {

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Format {
        PNG = 0,
        JPEG = 1,
        TIFF = 2,
        WebP = 3,
        BMP = 4,
        GIF = 5,
    };
    Q_ENUM(Format)

    struct Options {
        Format  format           = Format::PNG;
        int     jpegQuality      = 92;       // 0-100
        int     pngCompression   = 6;        // 0-9
        QString tiffCompression  = QStringLiteral("LZW");
        int     webpQuality      = 90;       // 0-100
        int     resizePercent    = 100;      // 25 / 50 / 75 / 100 / custom
        int     customWidth      = 0;        // 0 = use resizePercent
        int     customHeight     = 0;
        bool    embedIcc         = false;
        QString iccProfilePath;              // ICC profile .icc 文件路径
    };

    explicit ExportDialog(QWidget* parent = nullptr);
    ~ExportDialog() override;

    Options options() const { return m_opts; }

    // 给 ImageIOController 设置原图尺寸 (用于 resize preview)
    void setSourceSize(int width, int height);

    // P0-8.5 (2026-09-15) test accessors
    QString formatName() const;
    int     jpegQuality() const;
    int     pngCompression() const;
    QString tiffCompression() const;
    int     webpQuality() const;
    int     resizePercent() const;
    bool    embedIcc() const;
    QString iccProfilePath() const;

private slots:
    void onFormatChanged(int idx);
    void onJpegQualityChanged(int v);
    void onWebpQualityChanged(int v);
    void onResizeChanged(int idx);
    void onBrowseIcc();
    void onIccToggled(bool on);
    void onAccept();

private:
    void setupUI();
    void updateFormatPage();

    Options m_opts;

    // format selector
    QComboBox*  m_formatCombo = nullptr;

    // format-specific option pages (QStackedWidget)
    QStackedWidget* m_formatPages = nullptr;

    // JPEG page (quality slider + spinbox 联动)
    QSlider*    m_jpegSlider = nullptr;
    QSpinBox*   m_jpegSpin   = nullptr;
    QLabel*     m_jpegPreviewLabel = nullptr;

    // PNG page (compression spinbox)
    QSpinBox*   m_pngSpin = nullptr;

    // TIFF page (compression combo)
    QComboBox*  m_tiffCombo = nullptr;

    // WebP page
    QSlider*    m_webpSlider = nullptr;
    QSpinBox*   m_webpSpin   = nullptr;
    QLabel*     m_webpPreviewLabel = nullptr;

    // resize combo (通用, 不分格式)
    QComboBox*  m_resizeCombo = nullptr;
    QLabel*     m_resizePreviewLabel = nullptr;

    // ICC (P0-8.3 集成)
    QCheckBox*  m_iccCheck = nullptr;
    QLineEdit*  m_iccEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;

    // buttons
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    int m_sourceWidth  = 0;
    int m_sourceHeight = 0;
};

} // namespace dialogs