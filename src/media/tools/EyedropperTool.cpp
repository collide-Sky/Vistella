// SPDX-License-Identifier: MIT
//
// EyedropperTool implementation
//
#include "EyedropperTool.h"

#include "../imagewindow.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QWidget>
#include <opencv2/core.hpp>

namespace tools {

void EyedropperTool::onEnter(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Eyedropper (I) - click on canvas to sample color"), 5000);
    }
}

void EyedropperTool::onExit(ImageWindow* host)
{
    if (host && host->statusBar()) {
        host->statusBar()->clearMessage();
    }
}

void EyedropperTool::onMousePress(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    // F-C stage: sample from currentImage directly
    //   Full version (F-K): DialogMediator pushes to color panel + LayerCommand
    if (!host) return;
    const cv::Mat& img = host->currentImage();
    if (img.empty()) return;
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    if (x < 0 || y < 0 || x >= img.cols || y >= img.rows) return;
    // cv::Mat is BGR, Qt uses RGB
    const cv::Vec3b bgr = img.at<cv::Vec3b>(y, x);
    m_sampled = QColor(bgr[2], bgr[1], bgr[0]);
    // Status bar feedback
    if (host->statusBar()) {
        host->statusBar()->showMessage(
            QStringLiteral("Eyedropper: (%1,%2) = #%3%4%5")
                .arg(x).arg(y)
                .arg(m_sampled.red(),   2, 16, QChar('0'))
                .arg(m_sampled.green(), 2, 16, QChar('0'))
                .arg(m_sampled.blue(),  2, 16, QChar('0')),
            3000);
    }
}

// optionPage for ImageOptionBar page slot 7 (Eyedropper)
//   PS standard sample sizes: 1=Point, 3=3x3 average, 5=5x5 average,
//   11=11x11 average. Combo bound to m_sampleSize.
QWidget* EyedropperTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    auto* sampleCombo = new QComboBox(page);
    sampleCombo->addItem(QStringLiteral("Point Sample"),      1);
    sampleCombo->addItem(QStringLiteral("3x3 Average"),       3);
    sampleCombo->addItem(QStringLiteral("5x5 Average"),       5);
    sampleCombo->addItem(QStringLiteral("11x11 Average"),    11);
    {
        QSignalBlocker block(sampleCombo);
        int idx = sampleCombo->findData(m_sampleSize);
        if (idx < 0) idx = 0;
        sampleCombo->setCurrentIndex(idx);
    }
    QObject::connect(sampleCombo, &QComboBox::currentIndexChanged, page,
                     [this, sampleCombo](int /*idx*/) {
                         this->setSampleSize(sampleCombo->currentData().toInt());
                     });
    layout->addRow(QCoreApplication::translate("tools::Eyedropper", "Sample Size:"), sampleCombo);

    auto* hint = new QLabel(QCoreApplication::translate("tools::Eyedropper",
        "(click on canvas to sample color under cursor)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    return page;
}

} // namespace tools
