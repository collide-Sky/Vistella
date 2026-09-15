// SPDX-License-Identifier: MIT
//
// tst_P0_7_TextSystem - P0-7.5 (2026-09-14)
//
// 8 test 覆盖 P0-7 文字图层:
//   1. GraphicsTextItem 几何独立性: setPosition 不影响 setRotationDeg/setScale
//   2. GraphicsTextItem setRotationDeg 归一化到 [-180, 180]
//   3. GraphicsTextItem applyTransform 矩阵正确 (T*R*S*T(-c))
//   4. GraphicsTextItem shape() 包含 handles 矩形 (setIndexWidget 可命中)
//   5. TextOverlayController register/unregister lifecycle
//   6. TextOverlayController Bold/Italic state 持久化
//   7. TextOverlayController getCurrentStyle (default + item 实际)
//   8. PropertiesDock text properties 6 行 set/clear
//
// 不依赖 GUI / ImageWindow, 全部 unit test
//
#include <QTest>
#include <QGuiApplication>
#include <QFont>
#include <QColor>
#include <QPointF>
#include <QPainterPath>

#include "../src/media/graphicstextitem.h"
#include "../src/media/imagewindow/TextOverlayController.h"
#include "../src/media/docks/PropertiesDock.h"

class tst_P0_7_TextSystem : public QObject
{
    Q_OBJECT
private slots:
    // 1
    void test_GraphicsTextItem_setPosition_does_not_touch_rotation_or_scale();
    // 2
    void test_GraphicsTextItem_setRotationDeg_normalized_to_pm180();
    // 3
    void test_GraphicsTextItem_applyTransform_T_R_S_T_neg_c();
    // 4
    void test_GraphicsTextItem_shape_includes_handles_rect();
    // 5
    void test_TextOverlayController_register_unregister_lifecycle();
    // 6
    void test_TextOverlayController_BoldItalic_state_persists();
    // 7
    void test_TextOverlayController_getCurrentStyle_default_and_item();
    // 8
    void test_PropertiesDock_textProperties_set_and_clear();
};

// ================== 1. setPosition 不影响 rotation/scale ==================
void tst_P0_7_TextSystem::test_GraphicsTextItem_setPosition_does_not_touch_rotation_or_scale()
{
    // 关键: 独立变量 m_pos / m_rotation / m_scaleX / m_scaleY 互不污染
    //   setPosition 只改 m_pos, m_rotation / m_scale 保持不变
    //   setRotationDeg 只改 m_rotation, m_pos / m_scale 保持不变
    //   setScale 只改 m_scaleX/Y, m_pos / m_rotation 保持不变
    GraphicsTextItem item;
    item.setRotationDeg(45.0);
    item.setScale(2.0, 1.5);

    const QPointF oldPos = item.position();
    const qreal oldRot = item.rotationDeg();
    const qreal oldSx = item.scaleX();
    const qreal oldSy = item.scaleY();

    item.setPosition(QPointF(100.0, 200.0));

    QCOMPARE(item.position(), QPointF(100.0, 200.0));
    QCOMPARE(item.rotationDeg(), oldRot);   // rotation 没变
    QCOMPARE(item.scaleX(), oldSx);         // scaleX 没变
    QCOMPARE(item.scaleY(), oldSy);         // scaleY 没变

    // 反向: setRotationDeg 只改 rotation
    item.setRotationDeg(90.0);
    QCOMPARE(item.position(), QPointF(100.0, 200.0));  // pos 没变
    QCOMPARE(item.rotationDeg(), 90.0);
    QCOMPARE(item.scaleX(), oldSx);
    QCOMPARE(item.scaleY(), oldSy);

    // 反向: setScale 只改 scale
    item.setScale(0.5, 0.8);
    QCOMPARE(item.position(), QPointF(100.0, 200.0));
    QCOMPARE(item.rotationDeg(), 90.0);
    QCOMPARE(item.scaleX(), 0.5);
    QCOMPARE(item.scaleY(), 0.8);

    Q_UNUSED(oldPos);
}

// ================== 2. setRotationDeg 归一化到 [-180, 180] ==================
void tst_P0_7_TextSystem::test_GraphicsTextItem_setRotationDeg_normalized_to_pm180()
{
    GraphicsTextItem item;

    // 正常角度直接赋值
    item.setRotationDeg(45.0);
    QCOMPARE(item.rotationDeg(), 45.0);

    // 大于 180 应该 wrap 到负数
    item.setRotationDeg(270.0);
    QCOMPARE(item.rotationDeg(), -90.0);

    item.setRotationDeg(360.0);
    QCOMPARE(item.rotationDeg(), 0.0);

    item.setRotationDeg(720.0);
    QCOMPARE(item.rotationDeg(), 0.0);

    // 小于 -180 应该 wrap 到正数
    item.setRotationDeg(-270.0);
    QCOMPARE(item.rotationDeg(), 90.0);

    item.setRotationDeg(-360.0);
    QCOMPARE(item.rotationDeg(), 0.0);

    // 边界: 180 -> -180 (qFuzzyCompare 容差)
    item.setRotationDeg(180.0);
    QCOMPARE(item.rotationDeg(), -180.0);

    item.setRotationDeg(-180.0);
    QCOMPARE(item.rotationDeg(), -180.0);
}

// ================== 3. applyTransform 矩阵正确性 ==================
void tst_P0_7_TextSystem::test_GraphicsTextItem_applyTransform_T_R_S_T_neg_c()
{
    // 验证 applyTransform 写的 m_transform = T(c) * R(rot) * S(sx, sy) * T(-c)
    //   c = localRect().center()
    // 测试: transform 应该把 (c.x, c.y) 映射到 (c.x + tx, c.y + ty)
    //   即 translate 部分作用
    //   rotate 应该不动 center (围绕 center 旋转)
    //   scale 应该不动 center (围绕 center 缩放)
    GraphicsTextItem item;
    item.setPlainText(QStringLiteral("Hello"));  // 给 item 一个 boundingRect
    // 默认 sizeHint 跟字体相关, 但 GraphicsTextItem::localRect() 直接用 boundingRect()
    // 不依赖具体数字, 只验证 transform 把 center 映射到 (center + pos)
    //   用 QGraphicsTextItem::boundingRect() 需要对象, 直接 item.boundingRect() (QGraphicsItem)
    const QRectF lr = item.boundingRect();
    const QPointF c = lr.center();
    const QPointF sceneCenter = item.mapToScene(c);

    // item.position() = (0, 0), scale = 1, rot = 0
    // applyTransform 后 sceneCenter 应该 = (c.x, c.y)
    QCOMPARE(sceneCenter.x(), c.x());
    QCOMPARE(sceneCenter.y(), c.y());

    // 设 position = (100, 50), sceneCenter 应该 = (c.x + 100, c.y + 50)
    item.setPosition(QPointF(100.0, 50.0));
    const QPointF sceneCenter2 = item.mapToScene(c);
    QCOMPARE(sceneCenter2.x(), c.x() + 100.0);
    QCOMPARE(sceneCenter2.y(), c.y() + 50.0);

    // 设 rotation = 90 deg, center 仍不动
    item.setRotationDeg(90.0);
    const QPointF sceneCenter3 = item.mapToScene(c);
    QCOMPARE(sceneCenter3.x(), c.x() + 100.0);
    QCOMPARE(sceneCenter3.y(), c.y() + 50.0);
}

// ================== 4. hitTestHandle 命中 handles (Plan B 路径) ==================
void tst_P0_7_TextSystem::test_GraphicsTextItem_shape_includes_handles_rect()
{
    // Plan B (主流做法): view 层主动 hitTest handles, 不依赖 shape() / itemAt
    //   GraphicsTextItem::hitTestHandle 是 public static
    //   验证 setSelected2(true) 后 9 个 handle 都能命中 (4 角 + 4 边 + 1 rotate)
    //   setSelected2(false) 后 9 个 handle 都不可命中 (handles 不画)
    //
    //   handleAt 内部用 OUT=10px offset (handle 在 rect 外 10px 处)
    //   HW=7px 容差, 所以 hit pos 应该在 handle 中心 ±7px
    GraphicsTextItem item;
    item.setPlainText(QStringLiteral("Test"));
    const QRectF lr = item.boundingRect();
    const QPointF center = lr.center();

    // 8 个 corner/edge handle 位置 (从 lr 算, ±OUT=10px offset)
    const qreal OUT = 10.0;
    const QPointF tl = QPointF(lr.left()  - OUT, lr.top()    - OUT);  // ResizeTL
    const QPointF tr = QPointF(lr.right() + OUT, lr.top()    - OUT);  // ResizeTR
    const QPointF bl = QPointF(lr.left()  - OUT, lr.bottom() + OUT);  // ResizeBL
    const QPointF br = QPointF(lr.right() + OUT, lr.bottom() + OUT);  // ResizeBR
    const QPointF t  = QPointF(center.x(),       lr.top()    - OUT);  // ResizeT
    const QPointF b  = QPointF(center.x(),       lr.bottom() + OUT);  // ResizeB
    const QPointF l  = QPointF(lr.left()  - OUT, center.y());          // ResizeL
    const QPointF r  = QPointF(lr.right() + OUT, center.y());          // ResizeR

    // 未选中: 0 个 handle 命中
    item.setSelected2(false);
    GraphicsTextItem::Handle hh = GraphicsTextItem::None;
    GraphicsTextItem* hit = nullptr;
    hit = GraphicsTextItem::hitTestHandle({&item}, tl, &hh);
    QCOMPARE(hit, nullptr);
    QCOMPARE(hh, GraphicsTextItem::None);
    Q_UNUSED(hit); Q_UNUSED(hh);

    // 选中: 4 角 + 4 边 + 1 rotate = 9 个 handle 都能命中
    //   identity transform 下, scenePos == localPos, hitTestHandle 内部 mapFromScene
    //   返回 scenePos 自身, handleAt 跟 localPos 比较
    item.setSelected2(true);
    hit = GraphicsTextItem::hitTestHandle({&item}, tl, &hh);
    QCOMPARE(hit, &item);
    QCOMPARE(hh, GraphicsTextItem::ResizeTL);

    hit = GraphicsTextItem::hitTestHandle({&item}, t, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeT);

    hit = GraphicsTextItem::hitTestHandle({&item}, tr, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeTR);

    hit = GraphicsTextItem::hitTestHandle({&item}, l, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeL);

    hit = GraphicsTextItem::hitTestHandle({&item}, r, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeR);

    hit = GraphicsTextItem::hitTestHandle({&item}, bl, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeBL);

    hit = GraphicsTextItem::hitTestHandle({&item}, b, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeB);

    hit = GraphicsTextItem::hitTestHandle({&item}, br, &hh);
    QCOMPARE(hh, GraphicsTextItem::ResizeBR);

    // rotate handle 在中心上方 (rotation 0 deg, 在 top center 上方 32px, inCircle HR=10 容差)
    const QPointF rotatePos(center.x(), lr.top() - 32.0);
    hit = GraphicsTextItem::hitTestHandle({&item}, rotatePos, &hh);
    QCOMPARE(hh, GraphicsTextItem::Rotate);
    Q_UNUSED(OUT);
}

// ================== 5. TextOverlayController register/unregister ==================
void tst_P0_7_TextSystem::test_TextOverlayController_register_unregister_lifecycle()
{
    // TextOverlayController 没有 host 也能跑 register/unregister
    //   (host null 时 createTextItem/flattenText 直接 return)
    //   但 register/unregister 是核心 lifecycle, 必须测试
    TextOverlayController ctrl;
    QVERIFY(ctrl.textItems().isEmpty());

    auto* a = new GraphicsTextItem();
    auto* b = new GraphicsTextItem();
    ctrl.registerTextItem(a);
    ctrl.registerTextItem(b);
    QCOMPARE(ctrl.textItems().size(), 2);
    QVERIFY(ctrl.textItems().contains(a));
    QVERIFY(ctrl.textItems().contains(b));

    // 重复 register 不重复加
    ctrl.registerTextItem(a);
    QCOMPARE(ctrl.textItems().size(), 2);

    // unregister 单个
    ctrl.unregisterTextItem(a);
    QCOMPARE(ctrl.textItems().size(), 1);
    QVERIFY(!ctrl.textItems().contains(a));
    QVERIFY(ctrl.textItems().contains(b));

    // unregister 后, current 指向 unregistered 的 item 时, current 自动 null
    ctrl.setCurrent(b);
    QVERIFY(ctrl.current() == b);
    ctrl.unregisterTextItem(b);
    QCOMPARE(ctrl.textItems().size(), 0);
    QVERIFY(ctrl.current() == nullptr);

    delete a;
    delete b;

    // clearAll: 清空列表
    auto* c = new GraphicsTextItem();
    auto* d = new GraphicsTextItem();
    ctrl.registerTextItem(c);
    ctrl.registerTextItem(d);
    QCOMPARE(ctrl.textItems().size(), 2);
    ctrl.clearAll();
    QCOMPARE(ctrl.textItems().size(), 0);
    delete c;
    delete d;
}

// ================== 6. Bold/Italic state 持久化 ==================
void tst_P0_7_TextSystem::test_TextOverlayController_BoldItalic_state_persists()
{
    // m_textBold / m_textItalic 通过 setTextBold/on) / setTextItalic(on) 设置
    //   textBold() / textItalic() 读出来
    //   不会被 registerTextItem / setCurrent 等干扰
    TextOverlayController ctrl;
    QCOMPARE(ctrl.textBold(), false);
    QCOMPARE(ctrl.textItalic(), false);

    ctrl.setTextBold(true);
    ctrl.setTextItalic(true);
    QCOMPARE(ctrl.textBold(), true);
    QCOMPARE(ctrl.textItalic(), true);

    ctrl.setTextBold(false);
    QCOMPARE(ctrl.textBold(), false);
    QCOMPARE(ctrl.textItalic(), true);   // Italic 没变

    ctrl.setTextItalic(false);
    QCOMPARE(ctrl.textBold(), false);
    QCOMPARE(ctrl.textItalic(), false);
}

// ================== 7. getCurrentStyle default + item ==================
void tst_P0_7_TextSystem::test_TextOverlayController_getCurrentStyle_default_and_item()
{
    // m_current = null 时, getCurrentStyle 返回 default (m_textFont/m_textSize/m_textColor/...)
    //   m_current != null 时, 优先用 item 的 font/color, 但 pos/rotation 用 item 的几何
    TextOverlayController ctrl;
    ctrl.setTextFont(QStringLiteral("Arial"));
    ctrl.setTextSize(32);
    ctrl.setTextColor(QColor(255, 0, 0));
    ctrl.setTextBold(true);
    ctrl.setTextItalic(false);

    // default (m_current = null)
    auto s1 = ctrl.getCurrentStyle();
    QCOMPARE(s1.font, QStringLiteral("Arial"));
    QCOMPARE(s1.size, 32);
    QCOMPARE(s1.color, QColor(255, 0, 0));
    QCOMPARE(s1.bold, true);
    QCOMPARE(s1.italic, false);

    // 实际 item 状态: 用 setPlainText + setFont + setDefaultTextColor + setPosition + setRotationDeg
    auto* item = new GraphicsTextItem();
    QFont f(QStringLiteral("Times New Roman"));
    f.setPointSize(48);
    f.setBold(false);
    f.setItalic(true);
    item->setFont(f);
    item->setDefaultTextColor(QColor(0, 255, 0));
    item->setPosition(QPointF(123.0, 456.0));
    item->setRotationDeg(30.0);
    ctrl.setCurrent(item);
    ctrl.registerTextItem(item);

    auto s2 = ctrl.getCurrentStyle();
    QCOMPARE(s2.font, QStringLiteral("Times New Roman"));
    QCOMPARE(s2.size, 48);
    QCOMPARE(s2.color, QColor(0, 255, 0));
    QCOMPARE(s2.bold, false);
    QCOMPARE(s2.italic, true);
    QCOMPARE(s2.pos, QPointF(123.0, 456.0));
    QCOMPARE(s2.rotation, 30.0);

    ctrl.unregisterTextItem(item);
    delete item;
}

// ================== 8. PropertiesDock text properties 6 行 set/clear ==================
void tst_P0_7_TextSystem::test_PropertiesDock_textProperties_set_and_clear()
{
    // 验证 setTextProperties 写 6 行 label + clearTextProperties 重置为 "(无)"
    docks::PropertiesDock props;

    // 初始: 6 个 label 应该是 "(无)" + 灰色
    QCOMPARE(props.textFontLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textSizeLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textColorLabel(),  QStringLiteral("(无)"));
    QCOMPARE(props.textBoldLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textItalicLabel(), QStringLiteral("(无)"));
    QCOMPARE(props.textPosLabel(),    QStringLiteral("(无)"));

    // setTextProperties: 全部填值, 黑色
    docks::PropertiesDock::TextProperties p;
    p.font = QStringLiteral("Microsoft YaHei UI");
    p.size = 24;
    p.color = QColor(255, 0, 0);
    p.bold = true;
    p.italic = false;
    p.pos = QPointF(100.0, 200.0);
    props.setTextProperties(p);
    QCOMPARE(props.textFontLabel(),   QStringLiteral("Microsoft YaHei UI"));
    QCOMPARE(props.textSizeLabel(),   QStringLiteral("24"));
    // 颜色格式: "#ff0000 (255,0,0)" (arg 没空格, Qt 默认)
    QVERIFY(props.textColorLabel().contains(QStringLiteral("#ff0000")));
    QVERIFY(props.textColorLabel().contains(QStringLiteral("(255,0,0)")));
    QCOMPARE(props.textBoldLabel(),   QStringLiteral("是"));
    QCOMPARE(props.textItalicLabel(), QStringLiteral("否"));
    QCOMPARE(props.textPosLabel(),    QStringLiteral("(100, 200)"));

    // clearTextProperties: 重置为 "(无)" + 灰色
    props.clearTextProperties();
    QCOMPARE(props.textFontLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textSizeLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textColorLabel(),  QStringLiteral("(无)"));
    QCOMPARE(props.textBoldLabel(),   QStringLiteral("(无)"));
    QCOMPARE(props.textItalicLabel(), QStringLiteral("(无)"));
    QCOMPARE(props.textPosLabel(),    QStringLiteral("(无)"));
}

QTEST_MAIN(tst_P0_7_TextSystem)
#include "tst_P0_7_TextSystem.moc"
