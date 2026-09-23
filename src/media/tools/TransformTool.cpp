// SPDX-License-Identifier: MIT
//
// TransformTool implementation - P0-6.2 (2026-09-14)
//
// 4 mode 矩阵数学 (P0-6.3 TransformMath) 完整版在 P0-6.3 实装, 本类基础版
//   只实装 Scale 完整 + 其他 3 mode 接口, 4 mode 跟 TransformBox 配合
//
#include "TransformTool.h"

#include "../imagewindow.h"
#include "../selection/SelectionModel.h"
#include "../mediators/ToolMediator.h"
#include "../transform/TransformBox.h"
#include "../transform/TransformMath.h"
#include "../transform/TransformCommand.h"
#include "../imageprocessor.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>

namespace tools {

TransformTool::TransformTool() = default;

void TransformTool::onEnter(ImageWindow* host)
{
    m_host = host;
    m_box = std::make_unique<transform::TransformBox>();
    // 初始化 box 用选区 bbox (无选区用 image 整图, host=nullptr 也 fallback)
    QRectF initRect;
    if (host) {
        if (auto* sel = host->selectionModel()) {
            if (!sel && !sel->isEmpty()) {
                initRect = sel->boundingRect();
            }
        }
    }
    if (initRect.isEmpty()) {
        // 没选区 / host=nullptr: fallback 800x600
        // P0-6.6 集成后, 通过 host->imageRect() 拿
        initRect = QRectF(0, 0, 800, 600);
    }
    m_box->setRect(initRect);
    m_activeHandle = transform::TransformBox::Handle::None;
    m_pending = false;
}

void TransformTool::onExit(ImageWindow* /*host*/)
{
    // 如果有未提交的 transform, 丢弃 (用户切走工具 = 取消操作)
    m_activeHandle = transform::TransformBox::Handle::None;
    m_pending = false;
    m_box.reset();
}

void TransformTool::onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_box) return;
    m_activeHandle = m_box->hitTest(scenePos);
    m_lastScenePos = scenePos;
    m_pending = (m_activeHandle != transform::TransformBox::Handle::None);
}

void TransformTool::onMouseMove(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e);
    if (!m_box) return;
    if (m_activeHandle == transform::TransformBox::Handle::None) return;

    // Shift 修饰键 — Scale 模式 Shift = 等比, Skew 模式 Shift = 约束 1 方向
    if (m_shiftHeld) {
        // P0-6.3 完整 Shift 约束数学实现
        // 当前基础版: 直接传 newPos (完整约束留给 P0-6.3)
    }

    m_box->dragHandle(m_activeHandle, scenePos);
    m_lastScenePos = scenePos;
    // P0-6.6 集成后, 通知 host 重绘 canvas (emit boxChanged)
    if (host) host->update();
    // P0-6.12: rotation 模式拖动 → callback → ImageWindow 同步 propsDock
    if (m_box && m_mode == transform::TransformBox::Mode::Rotate) {
        if (m_rotationCb) m_rotationCb(m_box->rotation());
    }
}

void TransformTool::onMouseRelease(QMouseEvent* e, ImageWindow* host, const QPointF& scenePos)
{
    Q_UNUSED(e); Q_UNUSED(scenePos);
    if (!m_box) return;
    if (m_pending && host) {
        // P0-6.4 TransformCommand 完整实装后, 调 commitTransform
        commitTransform(host);
    }
    m_activeHandle = transform::TransformBox::Handle::None;
    m_pending = false;
}

void TransformTool::onKeyPress(QKeyEvent* e, ImageWindow* /*host*/)
{
    if (e->key() == Qt::Key_Shift) m_shiftHeld = true;
}

void TransformTool::onKeyRelease(QKeyEvent* e, ImageWindow* /*host*/)
{
    if (e->key() == Qt::Key_Shift) m_shiftHeld = false;
}

QCursor TransformTool::cursor() const
{
    if (!m_box) return Qt::CrossCursor;
    // 按 active handle 返回不同 cursor
    switch (m_activeHandle) {
        case transform::TransformBox::Handle::TopLeft:
        case transform::TransformBox::Handle::BottomRight:
            return Qt::SizeFDiagCursor;
        case transform::TransformBox::Handle::TopRight:
        case transform::TransformBox::Handle::BottomLeft:
            return Qt::SizeBDiagCursor;
        case transform::TransformBox::Handle::Top:
        case transform::TransformBox::Handle::Bottom:
            return Qt::SizeVerCursor;
        case transform::TransformBox::Handle::Left:
        case transform::TransformBox::Handle::Right:
            return Qt::SizeHorCursor;
        case transform::TransformBox::Handle::Center:
            return Qt::SizeAllCursor;
        case transform::TransformBox::Handle::Rotation:
            return Qt::CrossCursor;
        default:
            return Qt::CrossCursor;
    }
}

QWidget* TransformTool::optionPage(QWidget* parent)
{
    // P0-6.11 (2026-09-14): ImageOptionBar 装 4 mode combo (Scale/Rotate/Skew/Distort)
    //   + Shift = 等比 toggle
    //   每次 tool enter 都新建 widget (P0-6.7 简化: 不缓存)
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(8);

    auto* modeLabel = new QLabel(QStringLiteral("变换模式:"), page);
    layout->addWidget(modeLabel);

    auto* combo = new QComboBox(page);
    combo->addItem(QStringLiteral("缩放"),     static_cast<int>(transform::TransformBox::Mode::Scale));
    combo->addItem(QStringLiteral("旋转"),     static_cast<int>(transform::TransformBox::Mode::Rotate));
    combo->addItem(QStringLiteral("斜切"),     static_cast<int>(transform::TransformBox::Mode::Skew));
    combo->addItem(QStringLiteral("扭曲"),     static_cast<int>(transform::TransformBox::Mode::Distort));
    combo->setCurrentIndex(static_cast<int>(m_mode));
    layout->addWidget(combo);

    auto* shiftCheck = new QCheckBox(QStringLiteral("Shift (等比)"), page);
    shiftCheck->setChecked(m_shiftHeld);
    layout->addWidget(shiftCheck);

    layout->addStretch(1);

    // P0-6.11: combo 切 mode → TransformTool::setMode (4 mode 切换)
    //   combo 跟 TransformTool 双向 sync: tool 状态变 → combo 跟; combo 选 → tool 切
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     page, [this, combo](int idx) {
        if (!m_box) return;
        const auto mode = static_cast<transform::TransformBox::Mode>(combo->itemData(idx).toInt());
        m_mode = mode;
        m_box->setMode(mode);
    });
    // Shift checkbox → m_shiftHeld
    QObject::connect(shiftCheck, &QCheckBox::toggled,
                     page, [this](bool checked) { m_shiftHeld = checked; });
    return page;
}

void TransformTool::setMode(transform::TransformBox::Mode m)
{
    m_mode = m;
    if (m_box) m_box->setMode(m);
}

void TransformTool::commitTransform(ImageWindow* host)
{
    // P3.2.2 (2026-09-22): 真正 push TransformCommand (之前 P0-6.2 简化留的 no-op).
    //   流程:
    //     1) 计算最终矩阵 (Distort 用 perspective, 其他 3 mode 用 affine)
    //        - Scale/Skew 需要 (handle, newPos) — newPos 从 m_lastScenePos 拿
    //        - Rotate/Distort 不需要 newPos (rotation/corners 已存到 box)
    //     2) cv::Mat before = m_current.clone()  (snapshot)
    //     3) cv::Mat after = warpAffine 或 warpPerspective (应用)
    //     4) push TransformCommand(before, after, text)
    if (!host || !m_box) return;

    // Scale/Skew 必须有 (handle, newPos); 没记录 = 没真拖动, 不 commit
    const bool needsHandleNewPos = (m_box->mode() == transform::TransformBox::Mode::Scale
                                 || m_box->mode() == transform::TransformBox::Mode::Skew);
    if (needsHandleNewPos && m_activeHandle == transform::TransformBox::Handle::None) return;

    QString text;
    switch (m_box->mode()) {
        case transform::TransformBox::Mode::Scale:   text = QStringLiteral("缩放"); break;
        case transform::TransformBox::Mode::Rotate:  text = QStringLiteral("旋转"); break;
        case transform::TransformBox::Mode::Skew:    text = QStringLiteral("斜切"); break;
        case transform::TransformBox::Mode::Distort: text = QStringLiteral("扭曲"); break;
    }

    cv::Mat before;
    if (!host->currentImage().empty()) before = host->currentImage().clone();
    if (before.empty()) {
        qWarning("TransformTool::commitTransform: empty current image, skip");
        return;
    }
    cv::Mat after;

    if (m_box->mode() == transform::TransformBox::Mode::Distort) {
        // P3.2.3 (2026-09-22): Distort 走 4 corner 透视变换 (3x3 矩阵),
        //   因为 QTransform (2x3 affine) 不能表达非平行四边形映射.
        QPointF srcQuad[4];
        srcQuad[0] = m_box->rect().topLeft();
        srcQuad[1] = m_box->rect().topRight();
        srcQuad[2] = m_box->rect().bottomRight();
        srcQuad[3] = m_box->rect().bottomLeft();
        const QPointF* dstQuad = m_box->cornersArray();
        // 跳过没变化的 (4 角没动, src == dst)
        bool allSame = true;
        for (int i = 0; i < 4; ++i) {
            if (srcQuad[i] != dstQuad[i]) { allSame = false; break; }
        }
        if (allSame) return;
        ImageProcessor::warpPerspective(before, after, srcQuad, dstQuad);
    } else {
        // Scale / Rotate / Skew 走 affine (2x3 矩阵).
        //   P3.2.5 (2026-09-23): Scale/Skew 用 box.origRect() (拖动前 rect) 算非平凡矩阵.
        //   旧实现传 m_box->rect() (dragHandle 已更新到 newPos 后状态) 退化 identity.
        //   现在 TransformBox::dragHandle 第一次进入时 snapshot 到 m_dragOrigRect,
        //   commit 时 box.origRect() 返 pre-drag state, scaleMatrix 用它算 sx/sy 正确.
        //   Rotate 不需要 origRect (用 rotation angle).
        //   注: 加字段到 TransformBox 安全 — TransformBox 不是 Q_OBJECT 派生,
        //   不进 QString d-pointer 共享路径 (跟 P3.1.3 root cause 无关).
        QTransform qxform = transform::TransformMath::computeTransformWithOrigin(
            *m_box, m_activeHandle, m_lastScenePos, m_shiftHeld,
            m_box->origRect());
        if (qxform.isIdentity() && m_box->mode() != transform::TransformBox::Mode::Rotate) {
            // identity = 真没动, 不 push
            return;
        }
        if (!qxform.isAffine()) {
            qWarning("TransformTool::commitTransform: non-affine transform, skip");
            return;
        }
        cv::Mat M = ImageProcessor::qTransformToAffine(qxform);
        ImageProcessor::warpAffine(before, after, M, before.size());
    }

    // P3.2.5: commit 完 (push 或 skip) 后清 dragging 标志.
    m_box->resetDrag();

    if (auto *stack = host->undoStack()) {
        stack->push(new transform::TransformCommand(host, before, after, text));
    } else {
        host->setCurrentImage(after);
    }
}

}  // namespace tools
