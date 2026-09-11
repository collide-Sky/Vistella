#include "imageinfopanel.h"
#include "ui_imageinfopanel.h"

#include "imageprocessor.h"
#include "../core/ThemeManager.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QMouseEvent>

ImageInfoPanel::ImageInfoPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ImageInfoPanel)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("ImageInfoPanel"));
    setMinimumWidth(220);

    // 监听主题变化
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ImageInfoPanel::applyTheme);
    applyTheme();
}

ImageInfoPanel::~ImageInfoPanel()
{
    delete ui;
}

void ImageInfoPanel::applyTheme()
{
    const auto &p = ThemeManager::instance().palette();
    const QString css = QString(
        "QWidget#ImageInfoPanel { background: %1; }"
        "QLabel#titleLabel    { color: %2; border-bottom: 1px solid %3; }"
        "QLabel#sectionLabel  { color: %4; }"
        "QLabel#infoKey       { color: %4; }"
        "QLabel#infoVal       { color: %5; }"
        "QFrame#infoSep       { background: %3; max-height: 1px; border: 0; }"
        "QLabel#histLabel     { background: %6; color: %4; border: 1px solid %3;"
        "  border-radius: 4px; padding: 2px; }"
    )
    .arg(p.dockBg.name())           // %1
    .arg(p.text.name())             // %2
    .arg(p.menuBorder.name())       // %3
    .arg(p.textSubtle.name())       // %4
    .arg(p.text.name())             // %5
    .arg(p.alternateBase.name())    // %6
    ;
    setStyleSheet(css);
}

void ImageInfoPanel::updateInfo(const cv::Mat &img, const QString &filePath)
{
    m_currentImg = img.clone();

    // 文件名 / 路径
    if (filePath.isEmpty()) {
        ui->valFileName->setText(tr("未命名"));
        ui->valPath->setText(QStringLiteral("-"));
        ui->valPath->setToolTip(QString());
    } else {
        const QFileInfo fi(filePath);
        ui->valFileName->setText(fi.fileName());
        const QString abs = fi.absoluteFilePath();
        ui->valPath->setText(abs);
        ui->valPath->setToolTip(abs);
        QFontMetrics fm(ui->valPath->font());
        const int maxW = ui->valPath->width();
        if (maxW > 0 && fm.horizontalAdvance(abs) > maxW) {
            const QString shortPath = abs.left(3) + QStringLiteral("...\\") + fi.fileName();
            ui->valPath->setText(shortPath);
        }
    }

    // 图像信息
    const auto info = ImageProcessor::describe(img, filePath);
    ui->lblSize   ->setText(QStringLiteral("%1 × %2").arg(info.width).arg(info.height));
    ui->lblCh     ->setText(QString::number(info.channels));
    ui->lblType   ->setText(info.typeName);
    if (info.fileSize > 0) {
        const double kb = info.fileSize / 1024.0;
        if (kb < 1024)
            ui->lblFileSize->setText(QStringLiteral("%1 KB").arg(kb, 0, 'f', 1));
        else
            ui->lblFileSize->setText(QStringLiteral("%1 MB").arg(kb / 1024.0, 0, 'f', 2));
    } else {
        ui->lblFileSize->setText(QStringLiteral("-"));
    }

    // 总像素
    if (!img.empty()) {
        const qint64 total = qint64(img.cols) * qint64(img.rows);
        ui->lblTotal->setText(QStringLiteral("%1 (%2 万)").arg(total).arg(total / 10000));
    } else {
        ui->lblTotal->setText(QStringLiteral("-"));
    }

    if (img.empty()) {
        // 清空所有可视化
        ui->lblMean->setText("-");
        ui->lblStd->setText("-");
        ui->lblMinMax->setText("-");
        ui->lblPos->setText("-");
        ui->lblRGB->setText("-");
        ui->lblHSV->setText("-");
        ui->lblGray->setText("-");
        ui->histLabel->clear();
        ui->histLabel->setText(tr("无图像"));
        ui->rgbHistLabel->clear();
        ui->rgbHistLabel->setText(tr("无图像"));
        ui->hsvHistLabel->clear();
        ui->hsvHistLabel->setText(tr("无图像"));
        return;
    }

    // 统计
    const auto stats = ImageProcessor::computeStats(img);
    ui->lblMean->setText(QString::number(stats.luminance.mean, 'f', 1));
    ui->lblStd ->setText(QString::number(stats.luminance.std,  'f', 1));
    ui->lblMinMax->setText(QStringLiteral("%1 / %2")
        .arg(int(stats.luminance.min)).arg(int(stats.luminance.max)));

    // 像素信息清空 (等用户 hover)
    ui->lblPos ->setText("-");
    ui->lblRGB ->setText("-");
    ui->lblHSV ->setText("-");
    ui->lblGray->setText("-");

    // 灰度直方图
    cv::Mat gray;
    ImageProcessor::toGray(img, gray);
    const QVector<int> hist = ImageProcessor::computeHistogram(gray);
    ui->histLabel->setPixmap(QPixmap::fromImage(ImageProcessor::renderHistogram(hist, 256, 90)));
    ui->histLabel->setText(QString());

    // RGB 直方图
    const auto rgbH = ImageProcessor::computeRGBHistogram(img);
    ui->rgbHistLabel->setPixmap(QPixmap::fromImage(ImageProcessor::renderRGBHistogram(rgbH, 256, 90)));
    ui->rgbHistLabel->setText(QString());

    // HSV 直方图
    const auto hsvH = ImageProcessor::computeHSVHistogram(img);
    ui->hsvHistLabel->setPixmap(QPixmap::fromImage(ImageProcessor::renderHSVHistogram(hsvH, 256, 90)));
    ui->hsvHistLabel->setText(QString());
}

void ImageInfoPanel::updatePixel(const cv::Mat &img, const QPoint &pos)
{
    if (img.empty()) return;
    const auto pi = ImageProcessor::pixelAt(img, pos.x(), pos.y());
    if (!pi.valid) return;
    ui->lblPos ->setText(QStringLiteral("(%1, %2)").arg(pi.pos.x()).arg(pi.pos.y()));
    ui->lblRGB ->setText(QStringLiteral("%1 / %2 / %3").arg(pi.b).arg(pi.g).arg(pi.r));
    ui->lblHSV ->setText(QStringLiteral("%1 / %2 / %3").arg(pi.h).arg(pi.s).arg(pi.v));
    ui->lblGray->setText(QString::number(pi.gray));
}
