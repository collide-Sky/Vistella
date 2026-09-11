// SPDX-License-Identifier: MIT
//
// tst_F_F_MainWindowWorkspace - F-F (2026-09-09)
//
// 验证:
//   1) MainWindow 构造后 m_workspaceMed 存在, current workspace = Basic
//   2) switchWorkspace (via mediator API) 切换后 current 变
//   3) workspaceChanged 信号能 emit
//   4) QActionGroup 互斥逻辑 (4 action trigger 后只有一个 checked)
//
// F-F 集成是 mainwindow 级别, 不创建完整 mainwindow, 直接拿 mediator 测核心 API.
//
#include <QTest>
#include <QSignalSpy>
#include <QActionGroup>
#include <QAction>
#include <QMenu>

#include "../src/media/mediators/WorkspaceMediator.h"
#include "../src/media/mediators/ToolMediator.h"

using namespace mediators;

class tst_F_F_MainWindowWorkspace : public QObject
{
    Q_OBJECT
private slots:
    void ctor_workspaceMedExists();
    void switchWorkspace_emitsSignal();
    void qActionGroup_mutuallyExclusive();
    void allFourWorkspaces_haveConfig();
};

void tst_F_F_MainWindowWorkspace::ctor_workspaceMedExists()
{
    WorkspaceMediator med;
    QCOMPARE(med.currentWorkspace(), WorkspaceId::Basic);
    QCOMPARE(med.dockCount(), 3);
}

void tst_F_F_MainWindowWorkspace::switchWorkspace_emitsSignal()
{
    WorkspaceMediator med;
    QSignalSpy spy(&med, &WorkspaceMediator::workspaceChanged);
    med.switchWorkspace(WorkspaceId::Photo);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(med.currentWorkspace(), WorkspaceId::Photo);
    // 再切到 Paint
    med.switchWorkspace(WorkspaceId::Paint);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(med.currentWorkspace(), WorkspaceId::Paint);
    // 同 id 切不 emit
    med.switchWorkspace(WorkspaceId::Paint);
    QCOMPARE(spy.count(), 2);
}

void tst_F_F_MainWindowWorkspace::qActionGroup_mutuallyExclusive()
{
    // 模拟 mainwindow "视图 -> 工作区" submenu 的 4 QAction + QActionGroup 互斥
    QMenu menu;
    QActionGroup group(&menu);
    group.setExclusive(true);

    auto add = [&](const QString& label) {
        QAction *a = menu.addAction(label);
        a->setCheckable(true);
        group.addAction(a);
        return a;
    };
    QAction *aBasic = add("Basic");
    QAction *aPhoto = add("Photo");
    QAction *aPaint = add("Paint");
    QAction *aWeb   = add("Web");

    // 默认全 unchecked
    QCOMPARE(aBasic->isChecked(), false);

    // trigger aBasic
    aBasic->trigger();
    QCOMPARE(aBasic->isChecked(), true);
    QCOMPARE(aPhoto->isChecked(), false);
    QCOMPARE(aPaint->isChecked(), false);
    QCOMPARE(aWeb->isChecked(), false);

    // trigger aPhoto
    aPhoto->trigger();
    QCOMPARE(aBasic->isChecked(), false);
    QCOMPARE(aPhoto->isChecked(), true);
    QCOMPARE(aPaint->isChecked(), false);
    QCOMPARE(aWeb->isChecked(), false);
}

void tst_F_F_MainWindowWorkspace::allFourWorkspaces_haveConfig()
{
    WorkspaceMediator med;
    // 4 工作区都能查 dock 配置
    med.switchWorkspace(WorkspaceId::Basic);
    QCOMPARE(med.dockCount(), 3);
    QCOMPARE(med.dockTitle(0), QString("颜色"));

    med.switchWorkspace(WorkspaceId::Photo);
    QCOMPARE(med.dockCount(), 3);  // 3 dock 配置, dock 2 (title="") 隐藏
    QCOMPARE(med.dockTitle(0), QString("颜色"));
    // dock 2 隐藏
    QCOMPARE(med.isDockVisible(2), false);

    med.switchWorkspace(WorkspaceId::Paint);
    QCOMPARE(med.dockCount(), 3);
    QCOMPARE(med.dockTitle(0), QString("颜色"));

    med.switchWorkspace(WorkspaceId::Web);
    QCOMPARE(med.dockCount(), 3);
    QCOMPARE(med.dockTitle(0), QString("颜色"));
}

QTEST_MAIN(tst_F_F_MainWindowWorkspace)
#include "tst_F_F_MainWindowWorkspace.moc"
