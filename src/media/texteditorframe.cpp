// =============================================================
// TextEditorFrame - 文字输入框的外框 (QGraphicsRectItem 派生)
//
// 功能:
//   - 8 个 resize 手柄 (4 角 + 4 边), 拖动调宽高
//   - 顶部 1 个旋转手柄 (小圆点), 绕中心点旋转
//   - 框内嵌 QLineEdit, 字号按 box 高度自动缩放
//   - 边框虚线 + 浅绿手柄
//
// 注: TextEditorFrame 类直接定义在 imagewindow.h, 本文件只提供实现
// =============================================================

#include "imagewindow.h"   // TextEditorFrame 类定义在这里

#include <QPainter>
#include <QPen>
#include <QPalette>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsProxyWidget>
#include <QGraphicsEllipseItem>
#include <QFont>
#include <QLineEdit>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- 构造 / 析构 ----------

TextEditorFrame::TextEditorFrame(QLineEdit *editor, QGraphicsItem *parent)
    : QGraphicsRectItem(parent)
    , m_editor(editor)
{
    // 关键: 让 frame 优先收 mouse event
    // 否则子 item (proxy -> QLineEdit) 抢先 focus, 8 个手柄 + 1 旋转手柄永远点不到
    setFiltersChildEvents(true);

    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);   // 我们自己控制位置/大小

    // proxy 包 QLineEdit
    m_proxy = new QGraphicsProxyWidget(this);
    m_proxy->setWidget(m_editor);
    m_proxy->setFlag(QGraphicsItem::ItemIsMovable, false);
    m_proxy->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_proxy->setZValue(0);

    // 8 个 resize 手柄 (4 角 + 4 边, 8x8 cosmetic 方块)
    for (int i = 0; i < 8; ++i) {
        m_handles[i] = new QGraphicsRectItem(this);
        m_handles[i]->setRect(-4, -4, 8, 8);
        m_handles[i]->setBrush(QColor(76, 175, 128));
        m_handles[i]->setPen(QPen(QColor(255, 255, 255), 1));
        m_handles[i]->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        m_handles[i]->setAcceptedMouseButtons(Qt::NoButton);  // 鼠标事件给父
        m_handles[i]->setZValue(1);
    }

    // 顶部旋转手柄 (r=6 cosmetic 圆点)
    m_rotateHandle = new QGraphicsEllipseItem(this);
    m_rotateHandle->setRect(-6, -6, 12, 12);
    m_rotateHandle->setBrush(QColor(255, 152, 0));
    m_rotateHandle->setPen(QPen(QColor(255, 255, 255), 1));
    m_rotateHandle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    m_rotateHandle->setAcceptedMouseButtons(Qt::NoButton);
    m_rotateHandle->setZValue(2);

    // 初始 rect
    setRect(QRectF(0, 0, 200, 36));
}

// ---------- public API ----------

void TextEditorFrame::setFontFamily(const QString &family)
{
    m_fontFamily = family;
    syncEditorFont();
}

void TextEditorFrame::setFontSize(int pointSize)
{
    m_fontPointSize = std::max(6, pointSize);   // 防止 ≤0
    syncEditorFont();
}

void TextEditorFrame::setColor(const QColor &c)
{
    m_color = c;
    if (m_editor) {
        QPalette p = m_editor->palette();
        p.setColor(QPalette::Text, c);
        m_editor->setPalette(p);
    }
}

void TextEditorFrame::commitToImage()
{
    // 实际提交逻辑在 ImageWindow::onTextEditorCommit 里,
    // 这里保留接口以便将来扩展
}

// ---------- 内部 layout ----------

void TextEditorFrame::layoutChildren()
{
    if (!m_proxy) return;
    const QRectF r = rect();

    // QLineEdit 填满 rect 内部
    m_proxy->setGeometry(QRectF(0, 0, r.width(), r.height()));
    m_proxy->setPos(r.topLeft());

    // 8 个手柄位置
    m_handles[0]->setPos(r.topLeft());
    m_handles[1]->setPos(QPointF(r.center().x(), r.top()));
    m_handles[2]->setPos(r.topRight());
    m_handles[3]->setPos(QPointF(r.left(), r.center().y()));
    m_handles[4]->setPos(QPointF(r.right(), r.center().y()));
    m_handles[5]->setPos(r.bottomLeft());
    m_handles[6]->setPos(QPointF(r.center().x(), r.bottom()));
    m_handles[7]->setPos(r.bottomRight());

    // 旋转手柄: 顶部中点上方 20px
    m_rotateHandle->setPos(QPointF(r.center().x(), r.top() - 20));
}

void TextEditorFrame::syncEditorFont()
{
    if (!m_editor) return;
    QFont f;
    if (!m_fontFamily.isEmpty()) f.setFamily(m_fontFamily);
    // 直接用 point size (spinTextSize 设置的 pt), 不再基于 box 高度
    f.setPointSizeF(static_cast<qreal>(m_fontPointSize));
    m_editor->setFont(f);
}

// ---------- paint ----------

void TextEditorFrame::paint(QPainter *p,
                            const QStyleOptionGraphicsItem *opt,
                            QWidget *w)
{
    Q_UNUSED(opt);
    Q_UNUSED(w);
    p->setRenderHint(QPainter::Antialiasing, true);

    // 边框: 1.5px 虚线, 浅绿
    QRectF r = rect();
    QPen pen(QColor(76, 175, 128), 1.5, Qt::DashLine);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(r);
}

// ---------- handle 命中检测 ----------

TextEditorFrame::Handle TextEditorFrame::handleAt(const QPointF &local) const
{
    const qreal tol = 8.0;  // 像素 tolerance
    auto near = [&](const QPointF &p) {
        return qAbs(local.x() - p.x()) <= tol && qAbs(local.y() - p.y()) <= tol;
    };
    const QRectF r = rect();

    // 8 个 resize 手柄
    if (near(r.topLeft()))                         return ResizeTL;
    if (near(QPointF(r.center().x(), r.top())))    return ResizeT;
    if (near(r.topRight()))                        return ResizeTR;
    if (near(QPointF(r.left(), r.center().y())))   return ResizeL;
    if (near(QPointF(r.right(), r.center().y())))  return ResizeR;
    if (near(r.bottomLeft()))                      return ResizeBL;
    if (near(QPointF(r.center().x(), r.bottom()))) return ResizeB;
    if (near(r.bottomRight()))                     return ResizeBR;

    // 旋转手柄: 顶部中点上方 20px
    const QPointF rotPos(r.center().x(), r.top() - 20);
    if (near(rotPos)) return Rotate;

    return None;
}

// ---------- 鼠标事件 ----------

void TextEditorFrame::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    m_active = handleAt(e->pos());
    if (m_active != None) {
        m_dragStart     = e->pos();
        m_startRect     = rect();
        m_startRotation = rotation();
        m_startCenter   = m_startRect.center();
        if (m_active == Rotate) {
            m_startVec = e->scenePos() - m_startCenter;
        }
        e->accept();
    } else {
        QGraphicsRectItem::mousePressEvent(e);
    }
}

void TextEditorFrame::mouseMoveEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_active == None) {
        QGraphicsRectItem::mouseMoveEvent(e);
        return;
    }

    // 旋转
    if (m_active == Rotate) {
        const QPointF v1 = e->scenePos() - m_startCenter;
        const qreal a0 = std::atan2(m_startVec.y(), m_startVec.x());
        const qreal a1 = std::atan2(v1.y(), v1.x());
        const qreal deg = (a1 - a0) * 180.0 / M_PI;
        setRotation(m_startRotation + deg);
        e->accept();
        return;
    }

    // resize: item 局部 delta
    const QPointF delta = e->pos() - m_dragStart;
    QRectF nr = m_startRect;
    switch (m_active) {
    case ResizeTL: nr.setTopLeft(m_startRect.topLeft() + delta); break;
    case ResizeT:  nr.setTop(m_startRect.top() + delta.y()); break;
    case ResizeTR: nr.setTopRight(m_startRect.topRight() + delta); break;
    case ResizeL:  nr.setLeft(m_startRect.left() + delta.x()); break;
    case ResizeR:  nr.setRight(m_startRect.right() + delta.x()); break;
    case ResizeBL: nr.setBottomLeft(m_startRect.bottomLeft() + delta); break;
    case ResizeB:  nr.setBottom(m_startRect.bottom() + delta.y()); break;
    case ResizeBR: nr.setBottomRight(m_startRect.bottomRight() + delta); break;
    default: break;
    }
    // 最小尺寸
    if (nr.width() < 60)  nr.setWidth(60);
    if (nr.height() < 24) nr.setHeight(24);
    setRect(nr);   // 走重写版 -> 自动 layoutChildren + syncEditorFont
    e->accept();
}

void TextEditorFrame::mouseReleaseEvent(QGraphicsSceneMouseEvent *e)
{
    if (m_active != None) {
        m_active = None;
        e->accept();
    } else {
        QGraphicsRectItem::mouseReleaseEvent(e);
    }
}

QVariant TextEditorFrame::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged ||
        change == ItemRotationHasChanged ||
        change == ItemTransformHasChanged) {
        layoutChildren();
    }
    return QGraphicsRectItem::itemChange(change, value);
}
