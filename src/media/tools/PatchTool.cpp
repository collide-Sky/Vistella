// SPDX-License-Identifier: MIT
//
// PatchTool implementation - P0-9.3 (2026-09-15) + P0-9.4 (2026-09-15) + P2.2 (2026-09-22)
//
#include "PatchTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>     // P0-9.4 cv::rectangle + cv::Point
#include <opencv2/photo.hpp>      // P0-9.4 cv::seamlessClone

namespace tools {

PatchTool::PatchTool(QWidget* /*parent*/) : ToolState() {}

QString PatchTool::pageTitle() const
{
    return QCoreApplication::translate("tools::PatchTool", "Patch Tool");
}

void PatchTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_hasSource = false;
    m_selectingSource = false;
    LOG_DEBUG("[PatchTool] onEnter host={}", host ? "yes" : "null");
}

void PatchTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    m_hasSource = false;
    m_selectingSource = false;
    m_host = nullptr;
    LOG_DEBUG("[PatchTool] onExit");
}

void PatchTool::applyPatch(ImageWindow* host, const QPointF& dstA, const QPointF& dstB)
{
    if (!host || !m_hasSource) return;
    auto& img = host->currentImage();
    if (img.empty()) return;

    QPoint srcTl(static_cast<int>(m_sourceA.x()), static_cast<int>(m_sourceA.y()));
    QPoint srcBr(static_cast<int>(m_sourceB.x()), static_cast<int>(m_sourceB.y()));
    QRect srcRect(srcTl, srcBr);
    srcRect = srcRect.normalized();
    QPoint dstTl(static_cast<int>(dstA.x()), static_cast<int>(dstA.y()));
    QPoint dstBr(static_cast<int>(dstB.x()), static_cast<int>(dstB.y()));
    QRect dstRect(dstTl, dstBr);
    dstRect = dstRect.normalized();

    const QRect imgRect(0, 0, img.cols, img.rows);
    const QRect srcClip = srcRect.intersected(imgRect);
    const QRect dstClip = dstRect.intersected(imgRect);
    if (srcClip.isEmpty() || dstClip.isEmpty()) return;

    if (srcClip.width() != dstClip.width() || srcClip.height() != dstClip.height()) {
        // P0-9.4 简化: src/dst 不同尺寸不处理
        LOG_WARN("[PatchTool] src/dst size mismatch ({}x{} vs {}x{}), skipping",
                 srcClip.width(), srcClip.height(),
                 dstClip.width(), dstClip.height());
        return;
    }

    // P2.2: snapshot before apply for ImageEditCommand undo (MosaicTool pattern)
    m_backup = img.clone();
    m_strokeOpen = true;

    const cv::Rect srcCv(srcClip.x(), srcClip.y(), srcClip.width(), srcClip.height());
    cv::Mat sourceRoi = img(srcCv).clone();

    cv::Mat mask = cv::Mat::zeros(sourceRoi.size(), CV_8UC1);
    cv::rectangle(mask, cv::Rect(0, 0, sourceRoi.cols, sourceRoi.rows), cv::Scalar(255), cv::FILLED);

    // P2.2: PatchMode mapping to cv::seamlessClone flags
    //   Normal   -> NORMAL_CLONE (preserve src texture, blend edges)
    //   Mixed    -> MIXED_CLONE (preserve dst gradient, blend edges)
    //   MonochromeTransfer -> MONOCHROME_TRANSFER (apply src luminance to dst color)
    int cloneFlags = cv::NORMAL_CLONE;
    switch (m_patchMode) {
    case PatchMode::Normal:              cloneFlags = cv::NORMAL_CLONE; break;
    case PatchMode::Mixed:               cloneFlags = cv::MIXED_CLONE; break;
    case PatchMode::MonochromeTransfer:  cloneFlags = cv::MONOCHROME_TRANSFER; break;
    }

    const cv::Point center(dstClip.x() + dstClip.width() / 2,
                           dstClip.y() + dstClip.height() / 2);

    cv::Mat result;
    try {
        cv::seamlessClone(sourceRoi, img, mask, center, result, cloneFlags);
    } catch (const cv::Exception& e) {
        LOG_WARN("[PatchTool] cv::seamlessClone failed: {}, fallback to copy", e.what());
        result = img.clone();
        const cv::Rect dstCv(dstClip.x(), dstClip.y(), dstClip.width(), dstClip.height());
        sourceRoi.copyTo(result(dstCv));
    }
    host->setCurrentImage(result);

    // P2.2: commit undo command (single push per patch)
    if (auto* stack = host->undoStack()) {
        const QString modeName = [this]() {
            switch (m_patchMode) {
            case PatchMode::Normal:              return QStringLiteral("Normal");
            case PatchMode::Mixed:               return QStringLiteral("Mixed");
            case PatchMode::MonochromeTransfer:  return QStringLiteral("Monochrome Transfer");
            }
            return QStringLiteral("Normal");
        }();
        stack->push(new ImageEditCommand(host, m_backup, result,
            QCoreApplication::translate("tools::PatchTool", "Patch (%1)").arg(modeName)));
    }
    m_backup.release();
    m_strokeOpen = false;
    LOG_INFO("[PatchTool] applied seamlessClone patch ({}x{} region, mode={})",
             dstClip.width(), dstClip.height(), static_cast<int>(m_patchMode));
}

void PatchTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(host);
    if (!m_host || e->button() != Qt::LeftButton) return;
    m_pressScenePos = scenePos;
    m_lastScenePos = scenePos;
}

void PatchTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& scenePos)
{
    m_lastScenePos = scenePos;
}

void PatchTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_host || !host) return;

    if (!m_hasSource) {
        // 第一次拖: 选 source
        m_sourceA = m_pressScenePos;
        m_sourceB = scenePos;
        m_hasSource = true;
        LOG_DEBUG("[PatchTool] source selected: ({},{})..({},{})",
                  m_sourceA.x(), m_sourceA.y(),
                  m_sourceB.x(), m_sourceB.y());
        return;
    }
    // 第二次拖: 选 destination + apply
    applyPatch(host, m_pressScenePos, scenePos);
    m_hasSource = false;
}

QWidget* PatchTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // P2.2: Patch mode combo box (3 PS-standard modes)
    auto* modeCombo = new QComboBox(page);
    modeCombo->addItem(QCoreApplication::translate("tools::PatchTool", "Normal"),
                       static_cast<int>(PatchMode::Normal));
    modeCombo->addItem(QCoreApplication::translate("tools::PatchTool", "Mixed"),
                       static_cast<int>(PatchMode::Mixed));
    modeCombo->addItem(QCoreApplication::translate("tools::PatchTool", "Monochrome Transfer"),
                       static_cast<int>(PatchMode::MonochromeTransfer));
    modeCombo->setCurrentIndex(static_cast<int>(m_patchMode));
    QObject::connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                     [this](int idx) { m_patchMode = static_cast<PatchMode>(idx); });
    layout->addRow(QCoreApplication::translate("tools::PatchTool", "Mode:"), modeCombo);

    auto* hint = new QLabel(QCoreApplication::translate("tools::PatchTool",
        "(first drag selects source, second drag selects destination)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[PatchTool] optionPage created (P2.2 with Patch Mode combo)");
    return page;
}

} // namespace tools