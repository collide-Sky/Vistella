// =============================================================================
//  tst_WorkspaceMediator - F-B2 (2026-09-08) Mediator 模式单测
//
//  验证 4 件事:
//    1) 4 默认工作区都建好 (Basic/Photo/Paint/Web)
//    2) 切工作区 emit workspaceChanged, dock 配置变
//    3) dock 显隐 setDockVisible 触发 dockVisibilityChanged
//    4) dock 配置正确 (dockCount / dockTitle / dockTabs)
// =============================================================================

#include <QTest>
#include <QSignalSpy>

#include "../src/media/mediators/WorkspaceMediator.h"

using namespace mediators;

class tst_WorkspaceMediator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_default_is_Basic();
    void test_4_workspaces_built();
    void test_Basic_3_docks();
    void test_Photo_2_docks();
    void test_Paint_2_docks();
    void test_Web_3_docks();
    void test_switchWorkspace_emits();
    void test_switchWorkspace_same_no_emit();
    void test_switchWorkspace_resets_dockVisible();
    void test_setDockVisible_emits();
    void test_setDockVisible_invalid_no_emit();
    void test_dockTitle_and_tabs();
};

void tst_WorkspaceMediator::initTestCase()
{
    qRegisterMetaType<WorkspaceId>("WorkspaceId");
}

void tst_WorkspaceMediator::cleanupTestCase() {}

void tst_WorkspaceMediator::test_default_is_Basic()
{
    WorkspaceMediator med;
    QCOMPARE(med.currentWorkspace(), WorkspaceId::Basic);
}

void tst_WorkspaceMediator::test_4_workspaces_built()
{
    WorkspaceMediator med;
    const WorkspaceId all[] = {
        WorkspaceId::Basic, WorkspaceId::Photo,
        WorkspaceId::Paint, WorkspaceId::Web
    };
    for (int i = 0; i < 4; ++i) {
        med.switchWorkspace(all[i]);
        QVERIFY(med.dockCount() >= 1);  // 至少 1 dock
        QCOMPARE(med.currentWorkspace(), all[i]);
    }
}

void tst_WorkspaceMediator::test_Basic_3_docks()
{
    WorkspaceMediator med;
    // 默认 Basic
    QCOMPARE(med.dockCount(), 3);
    QCOMPARE(med.dockTitle(0), QStringLiteral("颜色"));
    QCOMPARE(med.dockTitle(1), QStringLiteral("属性"));
    QCOMPARE(med.dockTitle(2), QStringLiteral("图层"));
    // dock 0 tabs: 颜色/色板/渐变/图案
    QStringList dock0 = med.dockTabs(0);
    QCOMPARE(dock0.size(), 4);
    QCOMPARE(dock0.at(0), QStringLiteral("颜色"));
    QCOMPARE(dock0.at(3), QStringLiteral("图案"));
    // dock 1 tabs: 属性/调整
    QStringList dock1 = med.dockTabs(1);
    QCOMPARE(dock1.size(), 2);
    // dock 2 tabs: 图层/通道/路径
    QStringList dock2 = med.dockTabs(2);
    QCOMPARE(dock2.size(), 3);
}

void tst_WorkspaceMediator::test_Photo_2_docks()
{
    WorkspaceMediator med;
    med.switchWorkspace(WorkspaceId::Photo);
    QCOMPARE(med.dockCount(), 3);  // 仍 3 dock 配置, dock 2 visible=false
    QCOMPARE(med.isDockVisible(2), false);
    // dock 1 应该是属性/直方图
    QStringList dock1 = med.dockTabs(1);
    QCOMPARE(dock1.at(1), QStringLiteral("直方图"));
}

void tst_WorkspaceMediator::test_Paint_2_docks()
{
    WorkspaceMediator med;
    med.switchWorkspace(WorkspaceId::Paint);
    QCOMPARE(med.isDockVisible(2), false);
    // dock 0 颜色/色板/画笔
    QStringList dock0 = med.dockTabs(0);
    QCOMPARE(dock0.at(2), QStringLiteral("画笔"));
}

void tst_WorkspaceMediator::test_Web_3_docks()
{
    WorkspaceMediator med;
    med.switchWorkspace(WorkspaceId::Web);
    QCOMPARE(med.dockCount(), 3);
    // dock 2 只有 图层 (1 个)
    QStringList dock2 = med.dockTabs(2);
    QCOMPARE(dock2.size(), 1);
    QCOMPARE(dock2.at(0), QStringLiteral("图层"));
}

void tst_WorkspaceMediator::test_switchWorkspace_emits()
{
    WorkspaceMediator med;
    QSignalSpy spy(&med, &WorkspaceMediator::workspaceChanged);
    QVERIFY(spy.isValid());

    med.switchWorkspace(WorkspaceId::Photo);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<WorkspaceId>(spy.at(0).at(0)), WorkspaceId::Photo);
    QCOMPARE(med.currentWorkspace(), WorkspaceId::Photo);
}

void tst_WorkspaceMediator::test_switchWorkspace_same_no_emit()
{
    WorkspaceMediator med;
    med.switchWorkspace(WorkspaceId::Paint);
    QSignalSpy spy(&med, &WorkspaceMediator::workspaceChanged);

    med.switchWorkspace(WorkspaceId::Paint);  // 重复, 不 emit
    med.switchWorkspace(WorkspaceId::Paint);
    QCOMPARE(spy.count(), 0);
}

void tst_WorkspaceMediator::test_switchWorkspace_resets_dockVisible()
{
    WorkspaceMediator med;
    // 把 dock 1 隐藏
    med.setDockVisible(1, false);
    QCOMPARE(med.isDockVisible(1), false);

    // 切工作区
    med.switchWorkspace(WorkspaceId::Photo);
    // 切回时, dock 显隐应重置 (Photo 默认 dock 1 visible, 但 dock 2 隐藏)
    QCOMPARE(med.isDockVisible(0), true);
    QCOMPARE(med.isDockVisible(1), true);
    QCOMPARE(med.isDockVisible(2), false);  // Photo 隐藏 dock 2
}

void tst_WorkspaceMediator::test_setDockVisible_emits()
{
    WorkspaceMediator med;
    QSignalSpy spy(&med, &WorkspaceMediator::dockVisibilityChanged);
    QVERIFY(spy.isValid());

    med.setDockVisible(0, false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QCOMPARE(med.isDockVisible(0), false);

    // 重复设 false, 不 emit
    med.setDockVisible(0, false);
    QCOMPARE(spy.count(), 1);

    // 设回 true
    med.setDockVisible(0, true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(med.isDockVisible(0), true);
}

void tst_WorkspaceMediator::test_setDockVisible_invalid_no_emit()
{
    WorkspaceMediator med;
    QSignalSpy spy(&med, &WorkspaceMediator::dockVisibilityChanged);

    med.setDockVisible(-1, false);
    med.setDockVisible(99, false);   // 越界
    QCOMPARE(spy.count(), 0);
}

void tst_WorkspaceMediator::test_dockTitle_and_tabs()
{
    WorkspaceMediator med;
    // 默认 Basic
    QCOMPARE(med.dockTitle(0), QStringLiteral("颜色"));
    QVERIFY(!med.dockTabs(0).isEmpty());

    // 越界
    QCOMPARE(med.dockTitle(-1), QString());
    QCOMPARE(med.dockTitle(99), QString());
    QVERIFY(med.dockTabs(-1).isEmpty());
    QVERIFY(med.dockTabs(99).isEmpty());
}

QTEST_MAIN(tst_WorkspaceMediator)
#include "tst_WorkspaceMediator.moc"
