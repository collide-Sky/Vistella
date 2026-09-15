// SPDX-License-Identifier: MIT
//
// tst_RightPanelStack - F-G (2026-09-09) + F-H (2026-09-09)
//
// 验证:
//   1) RightPanelStack 构造后含 3 dock widget (颜色/属性/图层)
//   2) attach(wsMed) 后, 同步初始 dockVisible
//   3) wsMed->switchWorkspace(id) 触发 onWorkspaceChanged, dock 显隐跟随
//   4) F-H: ColorDock 含 2 swatch (fg/bg) + 12 色板 cells + setForegroundColor 发信号
//
#include <QTest>
#include <QSignalSpy>
#include <QTabWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QColor>
#include <QFormLayout>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCoreApplication>

#include "../src/media/docks/RightPanelStack.h"
#include "../src/media/docks/ColorDock.h"
#include "../src/media/docks/PropertiesDock.h"
#include "../src/media/docks/LayersDock.h"
#include "../src/media/mediators/WorkspaceMediator.h"

using namespace docks;
using namespace mediators;

class tst_RightPanelStack : public QObject
{
    Q_OBJECT
private slots:
    void ctor_have3Docks();
    void attach_initialSyncFromMediator();
    void switchWorkspace_dockVisibilityUpdates();
    void colorDock_haveFGBGSwatch();
    void colorDock_setForegroundEmitsSignal();
    void propertiesDock_haveScrollArea();
    void propertiesDock_setImageInfo();
};

void tst_RightPanelStack::ctor_have3Docks()
{
    RightPanelStack stack;
    ColorDock* c = stack.findChild<ColorDock*>();
    PropertiesDock* p = stack.findChild<PropertiesDock*>();
    LayersDock* l = stack.findChild<LayersDock*>();
    QVERIFY(c != nullptr);
    QVERIFY(p != nullptr);
    QVERIFY(l != nullptr);
    // F-G.2 (2026-09-09): LayersDock 现在含 QTabWidget 2 tab
    QVERIFY(l->tabs() != nullptr);
    QCOMPARE(l->tabs()->count(), 2);
}

void tst_RightPanelStack::attach_initialSyncFromMediator()
{
    RightPanelStack stack;
    WorkspaceMediator med;
    // 默认 Basic: 3 dock 全部 visible
    stack.attach(&med);
    QVERIFY(stack.findChild<ColorDock*>()->isVisibleTo(&stack));
    QVERIFY(stack.findChild<PropertiesDock*>()->isVisibleTo(&stack));
    QVERIFY(stack.findChild<LayersDock*>()->isVisibleTo(&stack));
}

void tst_RightPanelStack::switchWorkspace_dockVisibilityUpdates()
{
    RightPanelStack stack;
    WorkspaceMediator med;
    stack.attach(&med);

    // 切到 Photo: dock 2 (LayersDock) 隐藏
    med.switchWorkspace(WorkspaceId::Photo);
    QVERIFY(stack.findChild<ColorDock*>()->isVisibleTo(&stack));
    QVERIFY(stack.findChild<PropertiesDock*>()->isVisibleTo(&stack));
    QVERIFY(!stack.findChild<LayersDock*>()->isVisibleTo(&stack));

    // 切回 Basic: 全显示
    med.switchWorkspace(WorkspaceId::Basic);
    QVERIFY(stack.findChild<ColorDock*>()->isVisibleTo(&stack));
    QVERIFY(stack.findChild<PropertiesDock*>()->isVisibleTo(&stack));
    QVERIFY(stack.findChild<LayersDock*>()->isVisibleTo(&stack));

    // 切到 Paint: dock 2 隐藏 (跟 Photo 一样)
    med.switchWorkspace(WorkspaceId::Paint);
    QVERIFY(!stack.findChild<LayersDock*>()->isVisibleTo(&stack));
}

// F-H (2026-09-09): ColorDock UI 验证
void tst_RightPanelStack::colorDock_haveFGBGSwatch()
{
    ColorDock color;
    // 默认 fg=黑 bg=白
    QCOMPARE(color.foregroundColor(), QColor(Qt::black));
    QCOMPARE(color.backgroundColor(), QColor(Qt::white));
    // 12 色色板 (查找所有 QFrame, 排除 fg/bg swatch)
    int cellCount = 0;
    for (QFrame* f : color.findChildren<QFrame*>()) {
        if (f->property("paletteIndex").isValid()) ++cellCount;
    }
    QCOMPARE(cellCount, 12);
    // pick button 存在
    QVERIFY(color.findChild<QPushButton*>() != nullptr);
}

void tst_RightPanelStack::colorDock_setForegroundEmitsSignal()
{
    ColorDock color;
    QSignalSpy spy(&color, &ColorDock::foregroundColorChanged);
    color.setForegroundColor(QColor("#FF8800"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(color.foregroundColor(), QColor("#FF8800"));
    // 同色不 emit
    color.setForegroundColor(QColor("#FF8800"));
    QCOMPARE(spy.count(), 1);
}

// F-I (2026-09-09): PropertiesDock QScrollArea + maxHeight (修 1316 拉高 bug)
void tst_RightPanelStack::propertiesDock_haveScrollArea()
{
    PropertiesDock props;
    // 必有 QScrollArea
    QScrollArea* scroll = props.findChild<QScrollArea*>();
    QVERIFY(scroll != nullptr);
    // 关键: maxHeight 限制 (修 1316 bug)
    QCOMPARE(scroll->maximumHeight(), 150);
    // 必有 QFormLayout 装 4+1+4 行 (路径/尺寸/格式/DPI + separator + 选区 x/y/w/h)
    QFormLayout* form = props.findChild<QFormLayout*>();
    QVERIFY(form != nullptr);
    QCOMPARE(form->rowCount(), 17);  // P0-4.9 (2026-09-10): +1 separator + 4 selection bbox
                                  //   P0-6.12 (2026-09-14): +1 transform rotation row
                                  //   P0-7.4 (2026-09-14): +1 separator + 6 text properties rows (字体/字号/颜色/Bold/Italic/位置)
}

void tst_RightPanelStack::propertiesDock_setImageInfo()
{
    PropertiesDock props;
    PropertiesDock::ImageInfo info;
    info.filePath = "C:/test.png";
    info.width = 1920;
    info.height = 1080;
    info.format = "PNG";
    info.dpi = 96;
    props.setImageInfo(info);
    // F-O (2026-09-10): setImageInfo is throttled (200ms trailing edge). Pump
    // events so the throttled apply slot fires before we inspect the labels.
    QElapsedTimer throttleWait;
    throttleWait.start();
    while (throttleWait.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    // 验证 4 个 label 内容
    QList<QLabel*> labels = props.findChildren<QLabel*>();
    QVERIFY(labels.size() >= 4);
    // 找一个含 "1920 x 1080" 的 label
    bool foundSize = false;
    for (QLabel* l : labels) {
        if (l->text().contains("1920 x 1080")) { foundSize = true; break; }
    }
    QVERIFY(foundSize);
    // clear 后恢复 "(无)"
    props.clear();
    bool foundNone = false;
    for (QLabel* l : labels) {
        if (l->text() == "(无)") { foundNone = true; break; }
    }
    QVERIFY(foundNone);
}

QTEST_MAIN(tst_RightPanelStack)
#include "tst_RightPanelStack.moc"
