// =============================================================
// GraphicsTextItem - 方案 B 重构版
//   独立变量 m_pos / m_rotation / m_scaleX / m_scaleY
//   详见 graphicstextitem.h 头注释
// =============================================================

#include "graphicstextitem.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QGraphicsSceneMouseEvent>
#include <QFocusEvent>
#include <QStyleOptionGraphicsItem>
#include <QTextDocument>
#include <QFont>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================
// 构造 / 析构
// =============================================================

GraphicsTextItem::GraphicsTextItem(QGraphicsItem *parent)
    : QGraphicsTextItem(parent)
{
    setFlag(QGraphicsItem::ItemIsMovable, false);   // 移动通过自定义, 不用默认 ItemIsMovable
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setTextInteractionFlags(Qt::NoTextInteraction);

    setPlainText(QString());

    // 初始: m_pos = (0, 0), m_rotation = 0, m_scale = 1
    // applyTransform() 写整个 m_transform = T(c)*R*S*T(-c)
    // setPos(m_pos) 让 scenePos() 正确
    setPos(0, 0);
    applyTransform();
}

GraphicsTextItem::~GraphicsTextItem() = default;

// =============================================================
// 状态 getter / setter
// =============================================================

QRectF GraphicsTextItem::localRect() const
{
    // QGraphicsTextItem::boundingRect 返回不含 transform 的 local rect
    return QGraphicsTextItem::boundingRect();
}

void GraphicsTextItem::applyTransform()
{
    // 关键: 写整个 canonical m_transform, 不用 setTransformOriginPoint
    //   m_transform = T(c) * R(rot) * S(sx, sy) * T(-c)
    //   c = localRect().center()
    // 这样: scale / rotate 都围绕 localCenter, 互不污染
    // 不用 setTransformOriginPoint (Qt 6 onlyTransform 模式被忽略)
    setPos(m_pos);
    const QRectF r = localRect();
    const QPointF c = r.center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(m_rotation);
    t.scale(m_scaleX, m_scaleY);
    t.translate(-c.x(), -c.y());
    setTransform(t);
    prepareGeometryChange();
    update();
}

void GraphicsTextItem::setPosition(const QPointF &scenePos)
{
    if (m_pos == scenePos) return;
    m_pos = scenePos;
    applyTransform();
}

void GraphicsTextItem::setRotationDeg(qreal deg)
{
    // 归一化到 [-180, 180]
    while (deg >  180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    if (qFuzzyCompare(1.0 + m_rotation, 1.0 + deg)) return;
    m_rotation = deg;
    applyTransform();
}

void GraphicsTextItem::setScale(qreal sx, qreal sy)
{
    m_scaleX = qBound(0.01, sx, 100.0);
    m_scaleY = qBound(0.01, sy, 100.0);
    applyTransform();
}

// =============================================================
// 编辑模式
// =============================================================

void GraphicsTextItem::startEditing()
{
    if (m_editing) return;
    m_editing = true;
    m_editingStartText = toPlainText();
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setFocus(Qt::MouseFocusReason);
    update();
}

void GraphicsTextItem::endEditing()
{
    if (!m_editing) return;
    m_editing = false;
    setTextInteractionFlags(Qt::NoTextInteraction);
    clearFocus();
    // 主流做法: 退出编辑立即选中, 显示 handles, 用户可拖
    setSelected2(true);
    const QString newText = toPlainText();
    if (newText != m_editingStartText) {
        emit editingFinished(m_editingStartText, newText);
    }
}

// =============================================================
// 选中
// =============================================================

void GraphicsTextItem::setSelected2(bool on)
{
    if (m_selected == on) return;
    // 改变选中时 shape() 区域变化, 必须 prepareGeometryChange 通知 Qt
    prepareGeometryChange();
    m_selected = on;
    update();
}

// =============================================================
// shape() 命中区域
// =============================================================

QPainterPath GraphicsTextItem::shape() const
{
    QPainterPath path;
    const QRectF r = localRect();
    path.addRect(r);
    if (m_selected) {
        // 包含 8 个 handle + 1 个 rotate handle
        const qreal OUT = 10.0;
        const qreal HW  = 7.0;
        const QPointF centers[8] = {
            QPointF(r.left()   - OUT, r.top()    - OUT),  // TL
            QPointF(r.center().x(), r.top() - OUT),       // T
            QPointF(r.right()  + OUT, r.top()    - OUT),  // TR
            QPointF(r.left()   - OUT, r.center().y()),    // L
            QPointF(r.right()  + OUT, r.center().y()),    // R
            QPointF(r.left()   - OUT, r.bottom() + OUT),  // BL
            QPointF(r.center().x(), r.bottom() + OUT),    // B
            QPointF(r.right()  + OUT, r.bottom() + OUT)   // BR
        };
        for (int i = 0; i < 8; ++i) {
            path.addRect(QRectF(centers[i].x() - HW, centers[i].y() - HW, HW*2, HW*2));
        }
        // rotate handle (顶部中心, r=10 圆)
        path.addEllipse(QPointF(r.center().x(), r.top() - 32), 10, 10);
    }
    return path;
}

// =============================================================
// 命中检测
// =============================================================

GraphicsTextItem::Handle GraphicsTextItem::handleAt(const QPointF &localPos) const
{
    if (!m_selected) return None;
    const qreal HW  = 7.0;
    const qreal HR  = 10.0;
    const qreal OUT = 10.0;
    auto inBox = [&](const QPointF &c) {
        return qAbs(localPos.x() - c.x()) <= HW && qAbs(localPos.y() - c.y()) <= HW;
    };
    auto inCircle = [&](const QPointF &c) {
        const qreal dx = localPos.x() - c.x();
        const qreal dy = localPos.y() - c.y();
        return (dx*dx + dy*dy) <= HR*HR;
    };
    const QRectF r = localRect();
    if (inBox(QPointF(r.left()   - OUT, r.top()    - OUT))) return ResizeTL;
    if (inBox(QPointF(r.center().x(), r.top() - OUT)))      return ResizeT;
    if (inBox(QPointF(r.right()  + OUT, r.top()    - OUT))) return ResizeTR;
    if (inBox(QPointF(r.left()   - OUT, r.center().y())))   return ResizeL;
    if (inBox(QPointF(r.right()  + OUT, r.center().y())))   return ResizeR;
    if (inBox(QPointF(r.left()   - OUT, r.bottom() + OUT))) return ResizeBL;
    if (inBox(QPointF(r.center().x(), r.bottom() + OUT)))  return ResizeB;
    if (inBox(QPointF(r.right()  + OUT, r.bottom() + OUT))) return ResizeBR;
    if (inCircle(QPointF(r.center().x(), r.top() - 32)))    return Rotate;
    return None;
}

GraphicsTextItem *GraphicsTextItem::hitTestHandle(const QList<GraphicsTextItem *> &items,
                                                  const QPointF &scenePos,
                                                  Handle *outHandle)
{
    if (!outHandle) return nullptr;
    *outHandle = None;
    if (items.isEmpty()) return nullptr;
    for (auto *it : items) {
        if (!it || !it->isSelected2()) continue;
        const QPointF localPos = it->mapFromScene(scenePos);
        const Handle h = it->handleAt(localPos);
        if (h != None) {
            *outHandle = h;
            return it;
        }
    }
    return nullptr;
}

// =============================================================
// 拖动 helpers (Plan B View 层调用 / mousePressEvent 调用)
// =============================================================

void GraphicsTextItem::beginHandleDragFromView(Handle h, const QPointF &mouseScenePos)
{
    if (h == None) return;
    m_activeHandle = h;
    if (!m_selected) setSelected2(true);
    m_dragKind = DragHandle;
    // 关键: 只记 sceneDelta, 不用 m_dragStartTransform
    m_dragStartScenePos   = mouseScenePos;
    m_dragStartItemPos    = m_pos;
    m_dragStartRotation   = m_rotation;
    m_dragStartScaleX     = m_scaleX;
    m_dragStartScaleY     = m_scaleY;
}

void GraphicsTextItem::continueHandleDragFromView(const QPointF &scenePos)
{
    if (m_dragKind != DragHandle || m_activeHandle == None) return;
    // sceneDelta 在 scene 空间, 跟 m_pos / m_scaleX / m_scaleY / m_rotation 都无关
    const QPointF sceneDelta = scenePos - m_dragStartScenePos;
    if (m_activeHandle == Rotate) {
        // 旋转: 围绕 m_pos (视觉中心) 旋转
        // m_dragStartItemPos 是 m_pos 起始值, m_pos 不变 (旋转围绕 center)
        const QPointF v0 = m_dragStartScenePos - m_dragStartItemPos;
        const QPointF v1 = scenePos - m_dragStartItemPos;
        const qreal a0 = std::atan2(v0.y(), v0.x());
        const qreal a1 = std::atan2(v1.y(), v1.x());
        qreal deltaRad = a1 - a0;
        while (deltaRad >  M_PI) deltaRad -= 2.0 * M_PI;
        while (deltaRad < -M_PI) deltaRad += 2.0 * M_PI;
        const qreal deltaDeg = deltaRad * 180.0 / M_PI;
        setRotationDeg(m_dragStartRotation + deltaDeg);
    } else {
        // resize: 改 m_scaleX/Y + m_pos (让锚点视觉位置保持不动)
        const QRectF r = localRect();
        if (r.width() < 1 || r.height() < 1) return;
        // 视觉尺寸 (起始) = local 尺寸 * 起始 scale
        const qreal visualW = r.width()  * m_dragStartScaleX;
        const qreal visualH = r.height() * m_dragStartScaleY;
        qreal wRatio = 1.0, hRatio = 1.0;
        QPointF anchorLocal;
        switch (m_activeHandle) {
        case ResizeL:  anchorLocal = QPointF(r.right(), r.center().y()); wRatio = 1.0 - sceneDelta.x() / visualW; break;
        case ResizeR:  anchorLocal = QPointF(r.left(),  r.center().y()); wRatio = 1.0 + sceneDelta.x() / visualW; break;
        case ResizeT:  anchorLocal = QPointF(r.center().x(), r.bottom()); hRatio = 1.0 - sceneDelta.y() / visualH; break;
        case ResizeB:  anchorLocal = QPointF(r.center().x(), r.top());    hRatio = 1.0 + sceneDelta.y() / visualH; break;
        case ResizeTL: anchorLocal = r.bottomRight(); wRatio = 1.0 - sceneDelta.x() / visualW; hRatio = 1.0 - sceneDelta.y() / visualH; break;
        case ResizeTR: anchorLocal = r.bottomLeft();  wRatio = 1.0 + sceneDelta.x() / visualW; hRatio = 1.0 - sceneDelta.y() / visualH; break;
        case ResizeBL: anchorLocal = r.topRight();    wRatio = 1.0 - sceneDelta.x() / visualW; hRatio = 1.0 + sceneDelta.y() / visualH; break;
        case ResizeBR: anchorLocal = r.topLeft();     wRatio = 1.0 + sceneDelta.x() / visualW; hRatio = 1.0 + sceneDelta.y() / visualH; break;
        default: return;
        }
        wRatio = qBound(0.1, wRatio, 10.0);
        hRatio = qBound(0.1, hRatio, 10.0);
        const qreal newScaleX = m_dragStartScaleX * wRatio;
        const qreal newScaleY = m_dragStartScaleY * hRatio;
        // 锚点视觉位置不变: m_pos_new + R*S_new*(anchor - center) = m_pos_old + R*S_old*(anchor - center)
        //   => m_pos_new = m_pos_old - R(rot) * (S_new - S_old) * (anchor - center)
        const QPointF c = r.center();
        const qreal dsx = newScaleX - m_dragStartScaleX;
        const qreal dsy = newScaleY - m_dragStartScaleY;
        const qreal dxLocal = dsx * (anchorLocal.x() - c.x());
        const qreal dyLocal = dsy * (anchorLocal.y() - c.y());
        // 旋转: R 是 m_transform 内部, 但 m_pos 自身在 m_transform 外面
        //   m_pos_new = m_pos_old - R(rot) * (dxLocal, dyLocal)
        const qreal rad = m_dragStartRotation * M_PI / 180.0;
        const qreal cosA = std::cos(rad);
        const qreal sinA = std::sin(rad);
        const QPointF sceneShift(
            -(dxLocal * cosA - dyLocal * sinA),
            -(dxLocal * sinA + dyLocal * cosA)
        );
        m_scaleX = newScaleX;
        m_scaleY = newScaleY;
        m_pos = m_dragStartItemPos + sceneShift;
        applyTransform();
    }
}

void GraphicsTextItem::endHandleDragFromView(const QPointF &mouseScenePos)
{
    Q_UNUSED(mouseScenePos);
    if (m_dragKind != DragHandle) return;
    // emit transformFinished (用 m_pos 起始/结束值)
    const QPointF endPos = m_pos;
    const qreal endRot = m_rotation;
    const bool posChanged = !qFuzzyCompare(endPos.x(), m_dragStartItemPos.x())
                         || !qFuzzyCompare(endPos.y(), m_dragStartItemPos.y());
    const bool rotChanged = !qFuzzyCompare(1.0 + endRot, 1.0 + m_dragStartRotation);
    if (posChanged || rotChanged) {
        emit transformFinished(m_dragStartItemPos, endPos, m_dragStartRotation, endRot);
    }
    m_dragKind = DragNone;
    m_activeHandle = None;
}

// =============================================================
// 几何操作 (兼容旧 API, 给 ImageWindow 内部直接调用用)
// =============================================================

void GraphicsTextItem::resizeBy(Handle h, const QPointF &sceneDelta)
{
    // 简单开始: 走 beginHandleDragFromView + continueHandleDragFromView
    beginHandleDragFromView(h, QPointF(0, 0));
    continueHandleDragFromView(sceneDelta);
    // end 不在这里, 调用方自己 endHandleDragFromView
}

void GraphicsTextItem::rotateBy(qreal angleDeg)
{
    setRotationDeg(m_rotation + angleDeg);
}

// =============================================================
// paint
// =============================================================

void GraphicsTextItem::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w)
{
    // 关键: painter 已经在 sceneTransform 坐标系下 (Qt 自动 apply)
    // QGraphicsTextItem::paint 在 item local 坐标系画 document
    // 由于 m_transform = T(c)*R*S*T(-c), item local (0, 0) 视觉上 = m_pos + T(c)*R*S*T(-c)*(0,0) = m_pos + c - R*S*c
    // 即文字框左上角视觉上 = m_pos + c - R*S*c, 中心视觉上 = m_pos
    // QGraphicsTextItem::paint 期望 painter 已经在 item local 坐标系下, 内部画在 (0, 0)
    // 我们的 sceneTransform 已经处理了所有 transform, QGraphicsTextItem::paint 直接调 work
    QGraphicsTextItem::paint(p, opt, w);

    if (m_selected && !m_editing) {
        const QRectF r = localRect();
        // 1) 虚线边框
        QPen dashPen(QColor(76, 175, 128), 1.5, Qt::DashLine);
        p->setPen(dashPen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(r);
        // 2) 8 个 resize handles
        const qreal OUT = 10.0;
        const qreal HW  = 7.0;
        const QBrush hBrush(QColor(76, 175, 128));
        const QPen   hPen(QColor(255, 255, 255), 2);
        p->setPen(hPen);
        p->setBrush(hBrush);
        const QPointF centers[8] = {
            QPointF(r.left()   - OUT, r.top()    - OUT),
            QPointF(r.center().x(), r.top() - OUT),
            QPointF(r.right()  + OUT, r.top()    - OUT),
            QPointF(r.left()   - OUT, r.center().y()),
            QPointF(r.right()  + OUT, r.center().y()),
            QPointF(r.left()   - OUT, r.bottom() + OUT),
            QPointF(r.center().x(), r.bottom() + OUT),
            QPointF(r.right()  + OUT, r.bottom() + OUT)
        };
        for (int i = 0; i < 8; ++i) {
            p->drawRect(QRectF(centers[i].x() - HW, centers[i].y() - HW, HW*2, HW*2));
        }
        // 3) rotate handle
        p->setBrush(QColor(255, 152, 0));
        p->setPen(QPen(QColor(255, 255, 255), 2));
        p->drawEllipse(QPointF(r.center().x(), r.top() - 32), 10, 10);
    }
}

// =============================================================
// 鼠标事件
// =============================================================

void GraphicsTextItem::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    // 编辑模式: 让基类处理
    if (m_editing) {
        QGraphicsTextItem::mousePressEvent(e);
        return;
    }
    // 非编辑: 命中 handle -> beginHandleDragFromView
    m_activeHandle = handleAt(e->pos());
    if (m_activeHandle != None) {
        beginHandleDragFromView(m_activeHandle, e->scenePos());
        e->accept();
    } else {
        // 中心拖: 记录起始 m_pos, move 时直接 setPosition
        m_dragKind            = DragMove;
        m_dragStartScenePos   = e->scenePos();
        m_dragStartItemPos    = m_pos;
        m_dragStartRotation   = m_rotation;
        m_dragStartScaleX     = m_scaleX;
        m_dragStartScaleY     = m_scaleY;
        if (!m_selected) setSelected2(true);
        m_dragWasSelected = m_selected;
        // 拖动期间隐藏 handles (避免和拖动手势混淆, 释放时恢复)
        // 跟主流做法一致: 拖动时整个 item 半透明, handles 隐藏
        setSelected2(false);
        e->accept();
    }
}

void GraphicsTextItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_editing) {
        QGraphicsTextItem::mouseMoveEvent(e);
        return;
    }
    if (m_dragKind == DragNone) {
        QGraphicsTextItem::mouseMoveEvent(e);
        return;
    }
    if (m_dragKind == DragMove) {
        // 关键: m_pos 跟 m_scale/m_rotation 独立, 直接 m_pos_new = m_dragStartItemPos + sceneDelta
        //   不需要考虑 transformOriginPoint 偏移, 不需要考虑 localDelta 跨坐标系
        //   鼠标移动 sceneDelta, item 视觉中心跟手 sceneDelta
        const QPointF sceneDelta = e->scenePos() - m_dragStartScenePos;
        m_pos = m_dragStartItemPos + sceneDelta;
        applyTransform();
        e->accept();
        return;
    }
    // DragHandle
    continueHandleDragFromView(e->scenePos());
    e->accept();
}

void GraphicsTextItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_editing) {
        QGraphicsTextItem::mouseReleaseEvent(e);
        return;
    }
    if (m_dragKind == DragNone) {
        QGraphicsTextItem::mouseReleaseEvent(e);
        return;
    }
    if (m_dragKind == DragHandle) {
        endHandleDragFromView(e->scenePos());
    } else if (m_dragKind == DragMove) {
        // emit transformFinished (用 m_pos 起始/结束值)
        const QPointF endPos = m_pos;
        const bool posChanged = !qFuzzyCompare(endPos.x(), m_dragStartItemPos.x())
                             || !qFuzzyCompare(endPos.y(), m_dragStartItemPos.y());
        if (posChanged) {
            emit transformFinished(m_dragStartItemPos, endPos, m_dragStartRotation, m_rotation);
        }
        // 恢复选中
        if (m_dragWasSelected) {
            setSelected2(true);
        }
        m_dragWasSelected = false;
    }
    m_dragKind = DragNone;
    m_activeHandle = None;
    e->accept();
}

void GraphicsTextItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)
{
    // 关键: 马赛克模式下禁止进入文字编辑 (避免涂抹 + 文字输入冲突)
    // 仍可单击选中 / 拖动 / resize / rotate (这些走 mousePressEvent 不受 m_blockDoubleClickEdit 影响)
    if (m_blockDoubleClickEdit) {
        e->accept();
        return;
    }
    startEditing();
    e->accept();
}

// =============================================================
// 焦点
// =============================================================

void GraphicsTextItem::focusOutEvent(QFocusEvent *e)
{
    QGraphicsTextItem::focusOutEvent(e);
    if (m_editing) {
        endEditing();
    }
}

// =============================================================
// itemChange
// =============================================================

QVariant GraphicsTextItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    Q_UNUSED(change);
    Q_UNUSED(value);
    return QGraphicsTextItem::itemChange(change, value);
}
