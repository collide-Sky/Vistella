// SPDX-License-Identifier: MIT
//
// CloneTool implementation
//
#include "CloneTool.h"
#include "../imagewindow.h"
#include "../imageprocessor.h"
#include "logger.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>

namespace tools {

CloneTool::CloneTool(QWidget* /*parent*/) : ToolState() {}

QString CloneTool::pageTitle() const
{
    return QCoreApplication::translate(translateContext().toUtf8().constData(),
                                       translateTitle().toUtf8().constData());
}

void CloneTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_hasSample = false;
    m_dragging = false;
    LOG_DEBUG("[CloneTool] onEnter host={}", host ? "yes" : "null");
}

void CloneTool::onExit(ImageWindow* host)
{
    Q_UNUSED(host);
    hideBrushCursor(host);
    m_hasSample = false;
    m_dragging = false;
    m_host = nullptr;
    LOG_DEBUG("[CloneTool] onExit");
}

void CloneTool::showBrushCursor(ImageWindow* host, const QPointF& pos)
{
    if (!host) return;
    auto* cur = host->brushCursor();
    if (!cur) return;
    // P2.2: QGraphicsEllipseItem uses setRect(x, y, w, h); centered at (-r,-r)
    // so the cursor circle is centered on the cursor position
    const int r = m_brushSize;
    cur->setRect(-r, -r, r * 2, r * 2);
    cur->setPos(pos);
    cur->setVisible(true);
}

void CloneTool::hideBrushCursor(ImageWindow* host)
{
    if (!host) return;
    if (auto* cur = host->brushCursor()) cur->setVisible(false);
}

void CloneTool::paintAt(cv::Mat& img, const QPointF& dst, const QPointF& src)
{
    // 从 src 周围 brushSize 矩形复制到 dst 周围
    //   坐标先落到 int 变量再构造 Rect —— 直接写 int(src.x()) 会被解析成函数声明
    const int half = m_brushSize / 2;
    const int sx = static_cast<int>(src.x()) - half;
    const int sy = static_cast<int>(src.y()) - half;
    const int dx = static_cast<int>(dst.x()) - half;
    const int dy = static_cast<int>(dst.y()) - half;
    //  cv::Rect(a, b, c, d) 写在声明位置会被解析成函数声明, 用 = cv::Rect(...) 形式
    const cv::Rect srcRect = cv::Rect(sx, sy, m_brushSize, m_brushSize);
    const cv::Rect dstRect = cv::Rect(dx, dy, m_brushSize, m_brushSize);
    // 边界保护
    const cv::Rect imgRect = cv::Rect(0, 0, img.cols, img.rows);
    const cv::Rect srcClip = srcRect & imgRect;
    const cv::Rect dstClip = dstRect & imgRect;
    if (srcClip.empty() || dstClip.empty()) return;
    //  cv::Rect 的 width/height 是字段, 不是函数
    const int w = std::min(srcClip.width, dstClip.width);
    const int h = std::min(srcClip.height, dstClip.height);
    if (w <= 0 || h <= 0) return;
    // 源和目标可能重叠, 先把源区域拷出来再贴
    cv::Mat srcRoi = img(cv::Rect(srcClip.x, srcClip.y, w, h)).clone();
    srcRoi.copyTo(img(cv::Rect(dstClip.x, dstClip.y, w, h)));
}

void CloneTool::commitStroke(const QString& text)
{
    if (!m_strokeOpen || !m_host || m_backup.empty()) {
        m_backup.release();
        m_strokeOpen = false;
        return;
    }
    m_strokeOpen = false;
    //  笔画画在活动图层上, 撤销命令也是图层级快照
    cv::Mat* target = m_host->activeLayerImage();
    if (target && m_layerIndex >= 0) {
        m_host->commitLayerEdit(m_layerIndex);
        m_host->pushLayerImageEdit(m_layerIndex, m_backup, *target, text);
    }
    m_backup.release();
}

void CloneTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    if (!m_host || !host) return;

    // Alt + 单击 = 取样 (no stroke opened)
    if (e->modifiers() & Qt::AltModifier && e->button() == Qt::LeftButton) {
        m_samplePoint = scenePos;
        m_hasSample = true;
        showBrushCursor(host, scenePos);
        LOG_DEBUG("[CloneTool] sampled at ({}, {})", scenePos.x(), scenePos.y());
        return;
    }

    if (e->button() != Qt::LeftButton) return;
    if (!m_hasSample) {
        LOG_WARN("[CloneTool] no sample, Alt+click first to set source");
        return;
    }

    // P2.2: snapshot for undo before first paint of the stroke
    //  笔画写进活动图层的像素, 不写合成缓存 —— 合成缓存下一次 render 就没了
    cv::Mat* target = host->activeLayerImage();
    if (!target) {
        LOG_WARN("[CloneTool] no writable bitmap layer, skip stroke");
        return;
    }
    m_layerIndex = host->activeLayerIndex();
    m_backup = target->clone();
    m_strokeOpen = true;

    m_lastDstPos = scenePos;
    m_dragging = true;

    // 立即在点击处绘制一次 (跟 PS 同款, click 后立刻 paint 一次)
    if (target->empty()) return;

    // source = sample, dst = scenePos (按 m_lastDstPos 算 offset)
    const QPointF offset = m_lastDstPos - scenePos;
    paintAt(*target, scenePos, m_samplePoint + offset);
    host->commitLayerEdit(m_layerIndex);
    showBrushCursor(host, scenePos);
}

void CloneTool::onMouseMove(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& scenePos)
{
    showBrushCursor(host, scenePos);
    if (!m_dragging || !m_hasSample || !host) return;

    // PS 同款: 拖动时连续 paint
    cv::Mat* target = host->activeLayerImage();
    if (!target) return;

    const QPointF offset = scenePos - m_lastDstPos;
    paintAt(*target, scenePos, m_samplePoint + offset);
    host->commitLayerEdit(m_layerIndex);
    m_lastDstPos = scenePos;
}

void CloneTool::onMouseRelease(QMouseEvent* /*e*/, ImageWindow* host, const QPointF& /*scenePos*/)
{
    m_dragging = false;
    commitStroke(QCoreApplication::translate("tools::CloneTool", "Clone Stamp"));
}

void CloneTool::onKeyPress(QKeyEvent* e, ImageWindow* /*host*/)
{
    if (e->key() == Qt::Key_Escape) {
        m_hasSample = false;
        LOG_DEBUG("[CloneTool] Escape: cleared sample");
    }
}

QWidget* CloneTool::optionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QFormLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setLabelAlignment(Qt::AlignRight);

    // P2.2: brush size slider + value label (P2.1 MagicWand-style consistency)
    auto* szRow  = new QWidget(page);
    auto* szLay  = new QHBoxLayout(szRow);
    szLay->setContentsMargins(0, 0, 0, 0);
    auto* szLbl  = new QLabel(QString::number(m_brushSize), szRow);
    auto* slider = new QSlider(Qt::Horizontal, szRow);
    slider->setRange(2, 200);
    slider->setValue(m_brushSize);
    slider->setTickInterval(20);
    slider->setTickPosition(QSlider::TicksBelow);
    szLay->addWidget(slider, 1);
    szLay->addWidget(szLbl);
    layout->addRow(QCoreApplication::translate("tools::CloneTool", "Brush Size:"), szRow);
    QObject::connect(slider, &QSlider::valueChanged, page,
                     [this, szLbl](int v) {
                         setBrushSize(v);
                         szLbl->setText(QString::number(v));
                     });

    auto* hint = new QLabel(QCoreApplication::translate("tools::CloneTool",
        "(Alt + click to sample source)"), page);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    layout->addRow(QString(), hint);

    LOG_DEBUG("[CloneTool] optionPage created (P2.2)");
    return page;
}

} // namespace tools