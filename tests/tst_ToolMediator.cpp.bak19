// =============================================================================
//  tst_ToolMediator - F-B1 (2026-09-08) Mediator 模式单测
//
//  验证 4 件事:
//    1) 工厂: 实例化 + Q_OBJECT 注册 (qRegisterMetaType)
//    2) 切工具 emit toolSwitched (8 个 id 全覆盖)
//    3) 同 id 重复 switch 不重复 emit
//    4) applyToolConfig 存 config + emit toolConfigApplied
// =============================================================================

#include <QTest>
#include <QSignalSpy>

#include "../src/media/mediators/ToolMediator.h"

using namespace mediators;

class tst_ToolMediator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_construct();
    void test_switchTool_emits();
    void test_switchTool_sameId_noEmit();
    void test_applyToolConfig();
    void test_resetToolConfig();
    void test_currentConfig();
    void test_allToolIds();
};

void tst_ToolMediator::initTestCase()
{
    // Qt 跨线程 signal 必注册 metatype, 单元测试先调一次保险
    qRegisterMetaType<ToolId>("ToolId");
}

void tst_ToolMediator::cleanupTestCase() {}

void tst_ToolMediator::test_construct()
{
    ToolMediator med;
    QCOMPARE(med.currentTool(), ToolId::None);
    QVERIFY(med.currentConfig(ToolId::Move).isEmpty());
}

void tst_ToolMediator::test_switchTool_emits()
{
    ToolMediator med;
    QSignalSpy spy(&med, &ToolMediator::toolSwitched);
    QVERIFY(spy.isValid());

    med.switchTool(ToolId::Move);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(0).at(0)), ToolId::Move);
    QCOMPARE(med.currentTool(), ToolId::Move);

    med.switchTool(ToolId::RectSelect);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(1).at(0)), ToolId::RectSelect);
    QCOMPARE(med.currentTool(), ToolId::RectSelect);
}

void tst_ToolMediator::test_switchTool_sameId_noEmit()
{
    ToolMediator med;
    med.switchTool(ToolId::Brush);
    QSignalSpy spy(&med, &ToolMediator::toolSwitched);

    med.switchTool(ToolId::Brush);  // 重复, 不应 emit
    med.switchTool(ToolId::Brush);
    QCOMPARE(spy.count(), 0);
}

void tst_ToolMediator::test_applyToolConfig()
{
    ToolMediator med;
    QSignalSpy spy(&med, &ToolMediator::toolConfigApplied);

    QVariantMap cfg;
    cfg["feather"]  = 5;
    cfg["tolerance"] = 12;
    med.applyToolConfig(ToolId::RectSelect, cfg);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(0).at(0)), ToolId::RectSelect);
    QVariantMap got = qvariant_cast<QVariantMap>(spy.at(0).at(1));
    QCOMPARE(got["feather"].toInt(), 5);
    QCOMPARE(got["tolerance"].toInt(), 12);

    // 当前 config 已存
    QVariantMap stored = med.currentConfig(ToolId::RectSelect);
    QCOMPARE(stored["feather"].toInt(), 5);
    QCOMPARE(stored["tolerance"].toInt(), 12);
}

void tst_ToolMediator::test_resetToolConfig()
{
    ToolMediator med;
    QSignalSpy spyReset(&med, &ToolMediator::toolConfigReset);

    QVariantMap cfg;
    cfg["size"] = 30;
    med.applyToolConfig(ToolId::Brush, cfg);
    QVERIFY(!med.currentConfig(ToolId::Brush).isEmpty());

    med.resetToolConfig(ToolId::Brush);
    QCOMPARE(spyReset.count(), 1);
    QVERIFY(med.currentConfig(ToolId::Brush).isEmpty());
}

void tst_ToolMediator::test_currentConfig()
{
    ToolMediator med;
    // 没用过的 tool, config 应为空
    QVERIFY(med.currentConfig(ToolId::Lasso).isEmpty());

    // 设了别的 tool 不影响
    QVariantMap cfg;
    cfg["x"] = 1;
    med.applyToolConfig(ToolId::Move, cfg);
    QVERIFY(med.currentConfig(ToolId::Lasso).isEmpty());
    QCOMPARE(med.currentConfig(ToolId::Move)["x"].toInt(), 1);
}

void tst_ToolMediator::test_allToolIds()
{
    // 8 工具 id 全部走 switch 路径一遍, 确认无遗漏
    ToolMediator med;
    const ToolId all[] = {
        ToolId::Move, ToolId::RectSelect, ToolId::Lasso, ToolId::MagicWand,
        ToolId::Crop, ToolId::Text, ToolId::Brush, ToolId::Eyedropper
    };
    for (int i = 0; i < 8; ++i) {
        med.switchTool(all[i]);
        QCOMPARE(med.currentTool(), all[i]);
    }
}

QTEST_MAIN(tst_ToolMediator)
#include "tst_ToolMediator.moc"
