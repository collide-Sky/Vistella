#ifndef GRAPHICSTEXTITEM_H
#define GRAPHICSTEXTITEM_H

// =============================================================
// GraphicsTextItem — 主流做法的可编辑文字对象 (方案 B 重构版)
//
// 核心改进: 抛弃 QGraphicsItem transform 矩阵累加, 改用 3 个独立变量
//   m_pos / m_rotation / m_scaleX / m_scaleY
// 每次 applyTransform() 写整个 canonical form m_transform:
//   m_transform = T(localCenter) * R(m_rotation) * S(sx, sy) * T(-localCenter)
//   setPos(m_pos)   // scene pos 单独管理
//
// 优势:
//   - resize 只改 m_scaleX/Y, 不影响 m_rotation (长宽比保持)
//   - rotate  只改 m_rotation, 不影响 m_scaleX/Y (不扭曲)
//   - move    只改 m_pos,     不影响 m_transform
//   - 三者互不污染, 多次操作不累积误差
//   - 不依赖 setTransformOriginPoint (Qt 6 onlyTransform 模式被忽略, 已知坑)
//
// 其他保持:
//   - 继承 QGraphicsTextItem (自带 QTextDocument)
//   - 8 resize + 1 rotate handles 在 paint() 里画, 不做 child item
//   - shape() override 包含 handles 矩形
//   - Plan B: View 层主动 hitTest + 手动路由 handles
// =============================================================

#include <QGraphicsTextItem>

QT_BEGIN_NAMESPACE
class QGraphicsSceneMouseEvent;
class QFocusEvent;
class QStyleOptionGraphicsItem;
class QPainter;
QT_END_NAMESPACE

class GraphicsTextItem : public QGraphicsTextItem
{
    Q_OBJECT
public:
    enum Handle {
        None = 0,
        ResizeTL, ResizeT, ResizeTR,
        ResizeL,          ResizeR,
        ResizeBL, ResizeB, ResizeBR,
        Rotate
    };
    Q_ENUM(Handle)

    explicit GraphicsTextItem(QGraphicsItem *parent = nullptr);
    ~GraphicsTextItem() override;

    // ----- 编辑模式 -----
    void startEditing();
    void endEditing();
    bool isEditing() const { return m_editing; }

    // ----- 选中状态 -----
    void setSelected2(bool on);
    bool isSelected2() const { return m_selected; }

    // ----- 锁定编辑 (马赛克模式下禁止双击进入文字编辑) -----
    // 主流做法 (Photoshop/Word): 滤镜激活时, 文字图层仍可拖动/transform, 但不可编辑文字内容
    //   blockDoubleClickEdit = true 时, mouseDoubleClickEvent 直接 accept 不进入编辑
    //   单击选中 / 拖动 / resize / rotate 仍正常 (因为这些走 m_selected / m_dragKind 路径)
    void setBlockDoubleClickEdit(bool on) { m_blockDoubleClickEdit = on; }
    bool blockDoubleClickEdit() const { return m_blockDoubleClickEdit; }

    // ----- 几何状态 (新 API, 独立变量) -----
    QPointF position()    const { return m_pos; }
    qreal   rotationDeg() const { return m_rotation; }
    qreal   scaleX()      const { return m_scaleX; }
    qreal   scaleY()      const { return m_scaleY; }
    void    setPosition(const QPointF &scenePos);
    void    setRotationDeg(qreal deg);
    void    setScale(qreal sx, qreal sy);

    // ----- 几何操作 (兼容旧 API) -----
    //   resizeBy: sceneDelta 是 scene 空间 (鼠标位移), h 是哪个 handle
    //   rotateBy: 围绕 localCenter 旋转, m_pos 不需要调整
    void resizeBy(Handle h, const QPointF &sceneDelta);
    void rotateBy(qreal angleDeg);

    // ----- 命中检测 -----
    Handle handleAt(const QPointF &localPos) const;

    // ----- Plan B: View 层主动 hitTest + 路由 -----
    static GraphicsTextItem *hitTestHandle(const QList<GraphicsTextItem *> &items,
                                           const QPointF &scenePos,
                                           Handle *outHandle);
    void beginHandleDragFromView(Handle h, const QPointF &mouseScenePos);
    void continueHandleDragFromView(const QPointF &scenePos);
    void endHandleDragFromView(const QPointF &mouseScenePos);
    bool isDraggingHandle() const { return m_dragKind == DragHandle; }

    // ----- 序列化 (撤销栈用) -----
    QString serializedText() const { return toPlainText(); }
    void    restoreFromText(const QString &s) { setPlainText(s); }

    // 旧 API 兼容 (供 ImageWindow 现有代码用)
    void showHandles(bool on) { Q_UNUSED(on); }
    QString text2() const { return toPlainText(); }

signals:
    void editingFinished(const QString &oldText, const QString &newText);
    void transformFinished(const QPointF &oldPos, const QPointF &newPos,
                           qreal oldRot, qreal newRot);
    void debugMsg(const QString &text);

protected:
    // 绘制
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *opt, QWidget *w = nullptr) override;
    // 关键: shape() 决定 itemAt 命中区域, 必须包含 handles 矩形 (在 boundingRect 外侧)
    QPainterPath shape() const override;

    // 鼠标事件
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;

    // 焦点事件
    void focusOutEvent(QFocusEvent *e) override;

    // 几何变化
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    // ----- 内部状态 (独立变量, 互不污染) -----
    QPointF m_pos;            // scene 空间中心
    qreal   m_rotation = 0.0; // 度数, 顺时针正
    qreal   m_scaleX   = 1.0;
    qreal   m_scaleY   = 1.0;

    // 拖动模式
    enum DragKind { DragNone, DragMove, DragHandle };
    DragKind m_dragKind = DragNone;
    Handle   m_activeHandle = None;
    bool     m_dragWasSelected = false;

    // 拖动记录 (所有 drag 都用 sceneDelta 算, 不用 localDelta 跨坐标系)
    QPointF m_dragStartScenePos;   // 鼠标起始 scene pos
    QPointF m_dragStartItemPos;    // m_pos 起始值
    qreal   m_dragStartRotation = 0.0;
    qreal   m_dragStartScaleX   = 1.0;
    qreal   m_dragStartScaleY   = 1.0;

    // 编辑/选中
    bool m_editing  = false;
    bool m_selected = false;
    bool m_blockDoubleClickEdit = false;   // 马赛克模式时为 true
    QString m_editingStartText;

    // ----- helpers -----
    QRectF  localRect() const;     // 不含 transform 的 local rect
    void    applyTransform();      // 重写整个 m_transform = T(c)*R*S*T(-c)
    void    layoutHandles() {}     // 兼容 API, no-op (handles 在 paint 里画)
};

#endif // GRAPHICSTEXTITEM_H
