// SPDX-License-Identifier: MIT
//
// ExportDialog implementation - P0-8.2 (2026-09-15)
//
// 详见 ExportDialog.h 头注释
//
#include "ExportDialog.h"
#include "logger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>     // P0-8.4: ExportDialog 持久化
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace dialogs {

ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent)
{
    // P0-8.4 (2026-09-15): 从 QSettings 加载最近 settings (格式/jpeg quality/png/tiff/webp/resize/ICC)
    QSettings settings;
    m_opts.format          = static_cast<Format>(settings.value(QStringLiteral("ExportDialog/format"),
                                                                  static_cast<int>(m_opts.format)).toInt());
    m_opts.jpegQuality     = settings.value(QStringLiteral("ExportDialog/jpegQuality"), m_opts.jpegQuality).toInt();
    m_opts.pngCompression  = settings.value(QStringLiteral("ExportDialog/pngCompression"), m_opts.pngCompression).toInt();
    m_opts.tiffCompression = settings.value(QStringLiteral("ExportDialog/tiffCompression"), m_opts.tiffCompression).toString();
    m_opts.webpQuality     = settings.value(QStringLiteral("ExportDialog/webpQuality"), m_opts.webpQuality).toInt();
    m_opts.resizePercent   = settings.value(QStringLiteral("ExportDialog/resizePercent"), m_opts.resizePercent).toInt();
    m_opts.embedIcc        = settings.value(QStringLiteral("ExportDialog/embedIcc"), m_opts.embedIcc).toBool();
    m_opts.iccProfilePath  = settings.value(QStringLiteral("ExportDialog/iccProfilePath"), m_opts.iccProfilePath).toString();

    setupUI();
    setWindowTitle(tr("导出为..."));
    resize(420, 380);

    // 同步 UI 到设置 (setupUI 之后)
    {
        QSignalBlocker block1(m_formatCombo);
        QSignalBlocker block2(m_jpegSlider);
        QSignalBlocker block3(m_jpegSpin);
        QSignalBlocker block4(m_pngSpin);
        QSignalBlocker block5(m_tiffCombo);
        QSignalBlocker block6(m_webpSlider);
        QSignalBlocker block7(m_webpSpin);
        QSignalBlocker block8(m_resizeCombo);
        QSignalBlocker block9(m_iccCheck);
        QSignalBlocker block10(m_iccEdit);

        m_formatCombo->setCurrentIndex(static_cast<int>(m_opts.format));
        m_jpegSlider->setValue(m_opts.jpegQuality);
        m_jpegSpin->setValue(m_opts.jpegQuality);
        m_pngSpin->setValue(m_opts.pngCompression);
        const int tiffIdx = m_tiffCombo->findText(m_opts.tiffCompression);
        if (tiffIdx >= 0) m_tiffCombo->setCurrentIndex(tiffIdx);
        m_webpSlider->setValue(m_opts.webpQuality);
        m_webpSpin->setValue(m_opts.webpQuality);
        const int resizeIdx = m_resizeCombo->findData(m_opts.resizePercent);
        if (resizeIdx >= 0) m_resizeCombo->setCurrentIndex(resizeIdx);
        m_iccCheck->setChecked(m_opts.embedIcc);
        m_iccEdit->setText(m_opts.iccProfilePath);
    }
}

ExportDialog::~ExportDialog() = default;

void ExportDialog::setupUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ===== Format combo =====
    auto* formatRow = new QHBoxLayout();
    formatRow->addWidget(new QLabel(tr("格式:"), this));
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(QStringLiteral("PNG"),  static_cast<int>(Format::PNG));
    m_formatCombo->addItem(QStringLiteral("JPEG"), static_cast<int>(Format::JPEG));
    m_formatCombo->addItem(QStringLiteral("TIFF"), static_cast<int>(Format::TIFF));
    m_formatCombo->addItem(QStringLiteral("WebP"), static_cast<int>(Format::WebP));
    m_formatCombo->addItem(QStringLiteral("BMP"),  static_cast<int>(Format::BMP));
    m_formatCombo->addItem(QStringLiteral("GIF"),  static_cast<int>(Format::GIF));
    m_formatCombo->setCurrentIndex(static_cast<int>(Format::PNG));
    formatRow->addWidget(m_formatCombo, 1);
    root->addLayout(formatRow);

    // ===== Format-specific option pages =====
    m_formatPages = new QStackedWidget(this);

    // PNG page: compression 0-9
    {
        auto* page = new QWidget(this);
        auto* layout = new QFormLayout(page);
        m_pngSpin = new QSpinBox(page);
        m_pngSpin->setObjectName(QStringLiteral("pngSpin"));
        m_pngSpin->setRange(0, 9);
        m_pngSpin->setValue(m_opts.pngCompression);
        layout->addRow(tr("压缩级别:"), m_pngSpin);
        auto* hint = new QLabel(tr("(0 = 最快, 9 = 最小文件)"), page);
        hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
        layout->addRow(QString(), hint);
        m_formatPages->addWidget(page);
    }
    // JPEG page: quality slider + spinbox
    {
        auto* page = new QWidget(this);
        page->setObjectName(QStringLiteral("jpegPage"));
        auto* layout = new QFormLayout(page);
        auto* sliderRow = new QHBoxLayout();
        m_jpegSlider = new QSlider(Qt::Horizontal, page);
        m_jpegSlider->setObjectName(QStringLiteral("jpegSlider"));
        m_jpegSlider->setRange(0, 100);
        m_jpegSlider->setValue(m_opts.jpegQuality);
        m_jpegSpin = new QSpinBox(page);
        m_jpegSpin->setRange(0, 100);
        m_jpegSpin->setValue(m_opts.jpegQuality);
        m_jpegSpin->setMinimumWidth(60);
        sliderRow->addWidget(m_jpegSlider, 1);
        sliderRow->addWidget(m_jpegSpin);
        layout->addRow(tr("质量:"), sliderRow);
        m_jpegPreviewLabel = new QLabel(tr("(92 = 推荐值, 平衡质量与文件大小)"), page);
        m_jpegPreviewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
        layout->addRow(QString(), m_jpegPreviewLabel);
        m_formatPages->addWidget(page);
    }
    // TIFF page: compression combo
    {
        auto* page = new QWidget(this);
        auto* layout = new QFormLayout(page);
        m_tiffCombo = new QComboBox(page);
        m_tiffCombo->addItem(QStringLiteral("None"));
        m_tiffCombo->addItem(QStringLiteral("LZW"));
        m_tiffCombo->addItem(QStringLiteral("Deflate"));
        m_tiffCombo->setCurrentText(m_opts.tiffCompression);
        layout->addRow(tr("压缩:"), m_tiffCombo);
        m_formatPages->addWidget(page);
    }
    // WebP page: quality slider
    {
        auto* page = new QWidget(this);
        auto* layout = new QFormLayout(page);
        auto* sliderRow = new QHBoxLayout();
        m_webpSlider = new QSlider(Qt::Horizontal, page);
        m_webpSlider->setRange(0, 100);
        m_webpSlider->setValue(m_opts.webpQuality);
        m_webpSpin = new QSpinBox(page);
        m_webpSpin->setRange(0, 100);
        m_webpSpin->setValue(m_opts.webpQuality);
        m_webpSpin->setMinimumWidth(60);
        sliderRow->addWidget(m_webpSlider, 1);
        sliderRow->addWidget(m_webpSpin);
        layout->addRow(tr("质量:"), sliderRow);
        m_webpPreviewLabel = new QLabel(tr("(90 = 推荐值)"), page);
        m_webpPreviewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
        layout->addRow(QString(), m_webpPreviewLabel);
        m_formatPages->addWidget(page);
    }
    // BMP page: no option (uncompressed)
    {
        auto* page = new QWidget(this);
        auto* layout = new QFormLayout(page);
        layout->addRow(new QLabel(tr("(BMP 无压缩选项, 文件最大)"), page));
        m_formatPages->addWidget(page);
    }
    // GIF page: no option
    {
        auto* page = new QWidget(this);
        auto* layout = new QFormLayout(page);
        layout->addRow(new QLabel(tr("(GIF 仅支持 256 色索引)"), page));
        m_formatPages->addWidget(page);
    }

    root->addWidget(m_formatPages);

    // ===== Resize (通用, 跨格式) =====
    auto* resizeRow = new QHBoxLayout();
    resizeRow->addWidget(new QLabel(tr("缩放:"), this));
    m_resizeCombo = new QComboBox(this);
    m_resizeCombo->addItem(QStringLiteral("100% (原图)"), 100);
    m_resizeCombo->addItem(QStringLiteral("75%"),          75);
    m_resizeCombo->addItem(QStringLiteral("50%"),          50);
    m_resizeCombo->addItem(QStringLiteral("25%"),          25);
    m_resizeCombo->setCurrentIndex(0);
    resizeRow->addWidget(m_resizeCombo, 1);
    root->addLayout(resizeRow);

    m_resizePreviewLabel = new QLabel(this);
    m_resizePreviewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    root->addWidget(m_resizePreviewLabel);

    // ===== ICC (P0-8.3 集成) =====
    //   user 选 embed + 写 sRGB/AdobeRGB (内置) 或 .icc 文件路径
    //   PNG -> embedIccToPng (iCCP chunk), JPEG -> embedIccToJpeg (APP2 marker)
    //   TIFF -> TODO (P0-8.4 follow-up)
    auto* iccRow = new QHBoxLayout();
    m_iccCheck = new QCheckBox(tr("嵌入 ICC profile"), this);
    m_iccCheck->setChecked(m_opts.embedIcc);
    iccRow->addWidget(m_iccCheck);

    m_iccEdit = new QLineEdit(this);
    m_iccEdit->setPlaceholderText(tr("sRGB / AdobeRGB (内置) 或 .icc 文件路径"));
    m_iccEdit->setText(QStringLiteral("sRGB"));   // 默认 sRGB
    m_iccEdit->setEnabled(m_opts.embedIcc);
    iccRow->addWidget(m_iccEdit, 1);

    m_browseBtn = new QPushButton(tr("浏览..."), this);
    m_browseBtn->setEnabled(m_opts.embedIcc);
    iccRow->addWidget(m_browseBtn);
    root->addLayout(iccRow);

    root->addStretch(1);

    // ===== OK / Cancel =====
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_okBtn = new QPushButton(tr("导出"), this);
    m_okBtn->setDefault(true);
    m_cancelBtn = new QPushButton(tr("取消"), this);
    btnRow->addWidget(m_okBtn);
    btnRow->addWidget(m_cancelBtn);
    root->addLayout(btnRow);

    // ===== Connect signals =====
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    connect(m_jpegSlider, &QSlider::valueChanged,
            this, &ExportDialog::onJpegQualityChanged);
    connect(m_webpSlider, &QSlider::valueChanged,
            this, &ExportDialog::onWebpQualityChanged);
    // P0-8.4 (2026-09-15): PNG/TIFF/WebP spin 直接 connect valueChanged -> opts 同步
    connect(m_pngSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { m_opts.pngCompression = v; });
    connect(m_tiffCombo, &QComboBox::currentTextChanged,
            this, [this](const QString& t) { m_opts.tiffCompression = t; });
    connect(m_resizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onResizeChanged);
    connect(m_iccCheck, &QCheckBox::toggled,
            this, &ExportDialog::onIccToggled);
    connect(m_browseBtn, &QPushButton::clicked,
            this, &ExportDialog::onBrowseIcc);
    connect(m_okBtn, &QPushButton::clicked,
            this, &ExportDialog::onAccept);
    connect(m_cancelBtn, &QPushButton::clicked,
            this, &QDialog::reject);

    // 初始化 format page
    updateFormatPage();
    onResizeChanged(0);   // 触发 resize preview label
}

void ExportDialog::updateFormatPage()
{
    const int idx = m_formatCombo->currentIndex();
    m_formatPages->setCurrentIndex(idx);
}

void ExportDialog::onFormatChanged(int idx)
{
    Q_UNUSED(idx);
    updateFormatPage();
    m_opts.format = static_cast<Format>(m_formatCombo->currentData().toInt());
}

void ExportDialog::onJpegQualityChanged(int v)
{
    {
        QSignalBlocker block(m_jpegSpin);
        m_jpegSpin->setValue(v);
    }
    m_opts.jpegQuality = v;
    m_jpegPreviewLabel->setText(
        QStringLiteral("(质量 %1, 越高质量越好, 文件越大)").arg(v));
}

void ExportDialog::onWebpQualityChanged(int v)
{
    {
        QSignalBlocker block(m_webpSpin);
        m_webpSpin->setValue(v);
    }
    m_opts.webpQuality = v;
    m_webpPreviewLabel->setText(
        QStringLiteral("(质量 %1)").arg(v));
}

void ExportDialog::onResizeChanged(int idx)
{
    Q_UNUSED(idx);
    const int percent = m_resizeCombo->currentData().toInt();
    m_opts.resizePercent = percent;
    if (m_sourceWidth > 0 && m_sourceHeight > 0) {
        const int w = m_sourceWidth * percent / 100;
        const int h = m_sourceHeight * percent / 100;
        m_resizePreviewLabel->setText(
            QStringLiteral("(导出尺寸: %1 x %2 像素)").arg(w).arg(h));
    } else {
        m_resizePreviewLabel->setText(
            QStringLiteral("(导出尺寸: 跟随原图)"));
    }
}

void ExportDialog::onBrowseIcc()
{
    // P0-8.3 阶段启用: 让 user 选 .icc 文件
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 ICC profile"), QString(),
        tr("ICC profile (*.icc *.icm);; 全部文件 (*)"));
    if (!path.isEmpty()) {
        m_iccEdit->setText(path);
        m_opts.iccProfilePath = path;
    }
}

void ExportDialog::onIccToggled(bool on)
{
    m_opts.embedIcc = on;
    m_iccEdit->setEnabled(on);
    m_browseBtn->setEnabled(on);
}

void ExportDialog::onAccept()
{
    // 从 UI 同步到 m_opts
    m_opts.format          = static_cast<Format>(m_formatCombo->currentData().toInt());
    m_opts.jpegQuality     = m_jpegSpin->value();
    m_opts.pngCompression  = m_pngSpin->value();
    m_opts.tiffCompression = m_tiffCombo->currentText();
    m_opts.webpQuality     = m_webpSpin->value();
    m_opts.resizePercent   = m_resizeCombo->currentData().toInt();
    m_opts.embedIcc        = m_iccCheck->isChecked();
    m_opts.iccProfilePath  = m_iccEdit->text();

    // P0-8.4 (2026-09-15): 持久化 settings 到 QSettings (跨 dialog 调用)
    QSettings settings;
    settings.setValue(QStringLiteral("ExportDialog/format"),         static_cast<int>(m_opts.format));
    settings.setValue(QStringLiteral("ExportDialog/jpegQuality"),    m_opts.jpegQuality);
    settings.setValue(QStringLiteral("ExportDialog/pngCompression"), m_opts.pngCompression);
    settings.setValue(QStringLiteral("ExportDialog/tiffCompression"),m_opts.tiffCompression);
    settings.setValue(QStringLiteral("ExportDialog/webpQuality"),    m_opts.webpQuality);
    settings.setValue(QStringLiteral("ExportDialog/resizePercent"),  m_opts.resizePercent);
    settings.setValue(QStringLiteral("ExportDialog/embedIcc"),       m_opts.embedIcc);
    settings.setValue(QStringLiteral("ExportDialog/iccProfilePath"), m_opts.iccProfilePath);

    LOG_INFO("[ExportDialog] accepted: fmt={} jpeg={} png={} tiff={} webp={} resize={}% icc={}",
             formatName().toStdString(),
             m_opts.jpegQuality,
             m_opts.pngCompression,
             m_opts.tiffCompression.toStdString(),
             m_opts.webpQuality,
             m_opts.resizePercent,
             m_opts.embedIcc);
    accept();
}

void ExportDialog::setSourceSize(int width, int height)
{
    m_sourceWidth = width;
    m_sourceHeight = height;
    onResizeChanged(m_resizeCombo->currentIndex());   // 触发 preview 更新
}

// ===== test accessors =====
QString ExportDialog::formatName() const
{
    return m_formatCombo ? m_formatCombo->currentText() : QString();
}
int ExportDialog::jpegQuality() const { return m_opts.jpegQuality; }
int ExportDialog::pngCompression() const { return m_opts.pngCompression; }
QString ExportDialog::tiffCompression() const { return m_opts.tiffCompression; }
int ExportDialog::webpQuality() const { return m_opts.webpQuality; }
int ExportDialog::resizePercent() const { return m_opts.resizePercent; }
bool ExportDialog::embedIcc() const { return m_opts.embedIcc; }
QString ExportDialog::iccProfilePath() const { return m_opts.iccProfilePath; }

} // namespace dialogs