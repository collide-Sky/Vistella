// SPDX-License-Identifier: MIT
//
// MosaicTool - P0-1.2 (2026-09-07) full impl
//   接管 imagewindow.cpp 中所有"涂抹"逻辑:
//     - m_mosaicMode / m_mosaicDragging / m_mosaicBackup (状态迁过来)
//     - 涂抹 enum MosaicType 改成 MosaicTool::Type
//     - onMosaicModeToggled -> setEnabled
//     - onMosaicSizeChanged -> setSize
//     - eventFilter 中的 applyMosaic (MouseMove 完整版 + MouseButtonPress 简化版) 搬过来
//   保持原代码语义, 仅**位置**移动, 不**逻辑**删除/简化.
#include "MosaicTool.h"
#include "ImageCanvas.h"
#include "TextOverlayController.h"
#include "../imagewindow.h"
#include "../graphicstextitem.h"
#include "../imageprocessor.h"

#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QPen>
#include <QColor>
#include <QStatusBar>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

MosaicTool::MosaicTool(QObject* parent) : QObject(parent) {}
MosaicTool::~MosaicTool() {
    // P0-1.2 fix (2026-09-08): dtor 显式 setHost(nullptr)
    //   跟 ImageWindow 析构顺序无关: QPointer 在 ImageWindow 析构时也会自动置 null
    //   显式置一次是为了让 owner 主动清, 便于静态分析 / 调试时一眼看到
    //   安全: m_host 析构后自己也会清, 这里不依赖它
    m_host = nullptr;
}

// setEnabled - 替代原 ImageWindow::onMosaicModeToggled
//   主流做法: 涂抹和文字编辑互斥
//     进马赛克: 锁所有文字 item 的双击编辑 (setBlockDoubleClickEdit=true)
//               退出当前文字 item 的编辑状态 (避免边涂抹边编辑)
//     退马赛克: 解锁所有文字 item 的双击编辑
//   拖动/resize/rotate 文字 item 在马赛克模式下仍可用 (主流做法: 滤镜激活时, 文字图层仍可 transform)
void MosaicTool::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    // 关键修复: 锁所有文字 item 的双击编辑, 避免涂抹和文字输入冲突
    if (m_host && m_host->textOverlay()) {
        m_host->textOverlay()->setMosaicMode(on);
    }
    // 退出当前正在编辑的文字 (避免涂抹期间改文字)
    if (m_host && m_host->textOverlay()) {
        auto* ti = m_host->textOverlay()->current();
        if (ti && on) {
            ti->endEditing();
            m_host->textOverlay()->setCurrent(nullptr);
        }
    }
    if (m_host) {
        // brush cursor visibility - 进入/退出时都先隐藏 (进入图区再显示)
        if (m_host->brushCursor()) m_host->brushCursor()->setVisible(false);
        if (m_host->statusBarRef()) {
            if (on) {
                m_host->statusBarRef()->showMessage(
                    QObject::tr("涂抹模式: 在图上按下左键拖动, 松开结束"), 5000);
            } else {
                m_host->statusBarRef()->clearMessage();
            }
        }
    }
    // 重置 stroke 状态
    m_strokeOpen = false;
    if (!on) m_backup.release();
    emit modeChanged(on);
}

void MosaicTool::setSize(int radius) {
    // P0-1.2 fix (2026-09-08) 三重防御 (用户报告 "程序启动时死机中断" 修复):
    //   1) m_size 永远先 set 成功 — 不依赖任何外部状态, 即使后续防御全部失败, 业务状态也正确
    //   2) sanity clamp: radius 限制在 [1, 200] (跟 .ui sliderMosaicSize 范围对齐)
    //      防止 -1 / 0 / 65535 这类野值导致 setRect 出负数 / 0 大小
    //   3) m_host QPointer 检查: 之前 raw pointer 可能在 ImageWindow 析构后野指针,
    //      现在 QPointer 自动置 null, 这里早 return
    //   4) brushCursor() != null 检查 (P2.5: 已升级为 QPointer<QGraphicsEllipseItem>,
    //      ImageCanvas scene 析构时 item 被 delete, QPointer 自动 null, 这里 bc.isNull()
    //      就是完整检查, 不再需要二次 bc->scene() 这种本身不安全的 dereference)
    //   不引入 QObject connect/disconnect 路径避免递归, 全部 early return
    m_size = radius;
    if (m_size < 1)   m_size = 1;
    if (m_size > 200) m_size = 200;
    if (m_host.isNull()) return;                       // 3) ImageWindow 已销毁
    QGraphicsEllipseItem* bc = m_host->brushCursor(); // returns QPointer::data()
    if (!bc) return;                                   // 4) brush cursor null 或 scene 已析构
    const int r = m_size / 2;
    bc->setRect(-r, -r, m_size, m_size);
}

// P0-1.2 (2026-09-07): 不需要 installBrushCursor - brush cursor 在 ImageWindow ctor 创建
//   保留这个接口给未来 P0-1.2 后续轮次用, 现在 stub
void MosaicTool::installBrushCursor() {
    // P0-1.2 后续轮次: m_brushCursor 组件自己创建 + 拥有
    // 现在 brush cursor 控件还在 ImageWindow 里 (P0-1.2 这一刻**不动** brush cursor 控件位置)
}

void MosaicTool::uninstallBrushCursor() {
    if (m_host && m_host->brushCursor()) m_host->brushCursor()->setVisible(false);
}

// onSceneClicked - 涂抹 stroke 起点 (来自 ImageWindow eventFilter 的 MouseButtonPress)
//   步骤:
//     1. snapshot m_current 作为 undo 基线 (m_backup)
//     2. 立即在按下点应用一次 (使用 MouseButtonPress 简化版 applyMosaic)
void MosaicTool::onSceneClicked(const QPointF& scenePos) {
    if (!m_enabled) return;
    if (!m_host) return;
    cv::Mat& current = m_host->currentImage();
    if (current.empty()) return;
    // 1. snapshot
    m_backup = current.clone();
    m_strokeOpen = true;
    // 2. 立即在按下点应用一次
    applyMosaicAtInitial(scenePos);
}

void MosaicTool::applyMosaicAtInitial(const QPointF& scenePos) {
    if (!m_host) return;
    cv::Mat& current = m_host->currentImage();
    cv::Mat& original = m_host->originalImage();
    if (current.empty()) return;
    const int radius = m_size / 2;
    const int x = int(scenePos.x()) - radius;
    const int y = int(scenePos.y()) - radius;
    int cx = std::max(0, x);
    int cy = std::max(0, y);
    int cw = std::min(m_size, current.cols - cx);
    int ch = std::min(m_size, current.rows - cy);
    if (cw <= 0 || ch <= 0) return;
    auto applyMosaic = [&](cv::Mat& target) {
        // 关键: 保持原 MouseButtonPress 简化版逻辑 (只用 ImageProcessor::mosaic)
        //   原代码: blk = (type == MosaicTiles) ? std::max(8, radius) : std::max(2, radius / 4)
        //   注意: Blur / Blackout 类型**也**走 mosaic 公式 (原代码的"不一致", P0-1.2 不修)
        const int blk = (m_type == MosaicTool::Tiles)
            ? std::max(8, radius)
            : std::max(2, radius / 4);
        ImageProcessor::mosaic(target, cx, cy, cw, ch, blk);
    };
    applyMosaic(current);
    if (!original.empty()) applyMosaic(original);
    m_host->renderToViewPublic();
}

// onSceneDragMove - 涂抹 stroke 持续移动 (来自 ImageWindow eventFilter 的 MouseMove)
//   使用 MouseMove 完整版 applyMosaic (4 种 type 全 switch)
void MosaicTool::onSceneDragMove(const QPointF& scenePos) {
    if (!m_enabled) return;
    if (!m_host) return;
    cv::Mat& current = m_host->currentImage();
    if (current.empty()) return;
    applyMosaicAt(scenePos);
}

void MosaicTool::onSceneDragEnd() {
    if (!m_enabled) return;
    if (!m_host) return;
    if (!m_strokeOpen) return;
    m_strokeOpen = false;
    cv::Mat& current = m_host->currentImage();
    if (current.empty() || m_backup.empty()) {
        m_backup.release();
        return;
    }
    // 关键: 同时改 m_original, 否则后续 slider 变化会重置 m_current
    //  (其实 onSceneClicked 已经改了 m_original, 这里主要是 push undo)
    if (auto* stack = m_host->undoStack()) {
        stack->push(new ImageEditCommand(m_host, m_backup, current,
                                         QObject::tr("马赛克涂抹")));
    }
    m_backup.release();  // 释放 Mat, 避免内存累积
}

void MosaicTool::applyMosaicAt(const QPointF& scenePos) {
    if (!m_host) return;
    cv::Mat& current = m_host->currentImage();
    cv::Mat& original = m_host->originalImage();
    if (current.empty()) return;
    const int radius = m_size / 2;
    const int x = int(scenePos.x()) - radius;
    const int y = int(scenePos.y()) - radius;
    const int w = m_size;
    const int h = w;
    int cx = std::max(0, x);
    int cy = std::max(0, y);
    int cw = std::min(w, current.cols - cx);
    int ch = std::min(h, current.rows - cy);
    if (cw <= 0 || ch <= 0) return;
    auto applyMosaic = [&](cv::Mat& target) {
        switch (m_type) {
        case MosaicTool::Pixelate: {
            const int blk = std::max(2, radius / 4);
            ImageProcessor::mosaic(target, cx, cy, cw, ch, blk);
            break;
        }
        case MosaicTool::Blur: {
            cv::Mat roi = target(cv::Rect(cx, cy, cw, ch)).clone();
            cv::Mat blurred;
            int k = std::min(15, std::max(3, radius / 2));
            if (k % 2 == 0) k++;
            cv::GaussianBlur(roi, blurred, cv::Size(k, k), 0);
            blurred.copyTo(target(cv::Rect(cx, cy, cw, ch)));
            break;
        }
        case MosaicTool::Blackout: {
            target(cv::Rect(cx, cy, cw, ch)).setTo(cv::Scalar::all(0));
            break;
        }
        case MosaicTool::Tiles: {
            const int blk = std::max(8, radius);
            ImageProcessor::mosaic(target, cx, cy, cw, ch, blk);
            break;
        }
        }
    };
    applyMosaic(current);
    // 关键: 同时改 m_original, 否则后续 slider 变化会重置 m_current
    if (!original.empty()) applyMosaic(original);
    m_host->renderToViewPublic();
}

// 占位: P0-1.2 后续轮次用, 暂不实现
void MosaicTool::ensureBackup() {
    // P0-1.2 当前: snapshot 在 onSceneClicked 里做, ensureBackup 留给 P0-1.2 后续
    if (m_backup.empty() && m_host && !m_host->currentImage().empty()) {
        m_backup = m_host->currentImage().clone();
    }
}
