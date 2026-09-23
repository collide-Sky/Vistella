// =============================================================================
//  tst_LeftToolBar - F-D (2026-09-09) LeftToolBar 单测
//              + F-L (2026-09-10) 8 工具 (加 6 个: RectSelect/Lasso/MagicWand/Crop/Text/Brush)
//
//  验证:
//    1) 默认构造 8 工具按钮 (Move / RectSelect / Lasso / MagicWand / Crop / Text / Brush / Eyedropper)
//    2) attach 后, Mediator 切工具 emit toolSwitched → button 高亮
//    3) button click → Mediator.switchTool
//    4) 切到 None 状态时所有 button uncheck
//    5) F-L: 8 个 button click 都能 switchTool 正确 id
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QToolButton>

#include "../src/media/tools/LeftToolBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/mediators/ToolMediator.h"

using namespace tools;
using namespace mediators;

class tst_LeftToolBar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_default_8_buttons();      // F-L (2026-09-10): 8 工具按钮
    void test_button_letter_codes();    // F-L: 8 个 button text 唯一
    void test_attach_creates_MoveTool();
    void test_button_click_triggers_Mediator();
    void test_Mediator_signal_highlights_button();
    void test_setToolEnabled();
    void test_None_unchecks_all();
    void test_all_8_tools_clickable();   // F-L: 8 工具 button click → Mediator switchTool
};

void tst_LeftToolBar::initTestCase()
{
    qRegisterMetaType<ToolId>("ToolId");
}

void tst_LeftToolBar::cleanupTestCase() {}

void tst_LeftToolBar::test_default_8_buttons()
{
    LeftToolBar bar;
    // F-L (2026-09-10): 8 按钮 (Move / RectSelect / Lasso / MagicWand / Crop / Text / Brush / Eyedropper)
    //   P0-9.4 (2026-09-15): +6 按钮 (Shape / Pen / Clone / Heal / Patch / RedEye) = 14
    //   Q4.2.1 (2026-09-23): +1 按钮 (MaskBrush P1.3.4 像素蒙版画笔) = 15
    auto btns = bar.findChildren<QToolButton*>();
    QCOMPARE(btns.size(), 15);
}

void tst_LeftToolBar::test_button_letter_codes()
{
    LeftToolBar bar;
    // 8 工具 button text (PS 风格字母 placeholder, F-N 换 SVG icon)
    QStringList expected = {"M", "R", "L", "W", "C", "T", "B", "I"};
    QStringList actual;
    for (auto* b : bar.findChildren<QToolButton*>()) actual << b->text();
    for (const auto& t : expected) QVERIFY2(actual.contains(t), qPrintable("missing button text: " + t));
}

void tst_LeftToolBar::test_attach_creates_MoveTool()
{
    LeftToolBar bar;
    ToolMediator med;
    ToolContext ctx;
    bar.attach(&med, &ctx);

    // 默认 None 状态
    QCOMPARE(ctx.currentToolId(), ToolId::None);

    // Mediator 切到 Move
    med.switchTool(ToolId::Move);
    QTest::qWait(0);  // 让信号队列消化
    QCOMPARE(ctx.currentToolId(), ToolId::Move);
}

void tst_LeftToolBar::test_button_click_triggers_Mediator()
{
    LeftToolBar bar;
    ToolMediator med;
    ToolContext ctx;
    bar.attach(&med, &ctx);

    QSignalSpy spy(&med, &ToolMediator::toolSwitched);
    // 找到 Move 按钮
    QToolButton* moveBtn = nullptr;
    for (auto* b : bar.findChildren<QToolButton*>()) {
        if (b->text() == "M") { moveBtn = b; break; }
    }
    QVERIFY(moveBtn);
    moveBtn->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(0).at(0)), ToolId::Move);
}

void tst_LeftToolBar::test_Mediator_signal_highlights_button()
{
    LeftToolBar bar;
    ToolMediator med;
    ToolContext ctx;
    bar.attach(&med, &ctx);

    med.switchTool(ToolId::Move);
    QTest::qWait(0);
    // 找 Move 按钮, 应 checked
    QToolButton* moveBtn = nullptr;
    for (auto* b : bar.findChildren<QToolButton*>()) {
        if (b->text() == "M") { moveBtn = b; break; }
    }
    QVERIFY(moveBtn);
    QVERIFY(moveBtn->isChecked());
    // Eyedropper 按钮应 unchecked
    QToolButton* eyeBtn = nullptr;
    for (auto* b : bar.findChildren<QToolButton*>()) {
        if (b->text() == "I") { eyeBtn = b; break; }
    }
    QVERIFY(eyeBtn);
    QVERIFY(!eyeBtn->isChecked());
}

void tst_LeftToolBar::test_setToolEnabled()
{
    LeftToolBar bar;
    bar.setToolEnabled(ToolId::Move, false);
    QToolButton* moveBtn = nullptr;
    for (auto* b : bar.findChildren<QToolButton*>()) {
        if (b->text() == "M") { moveBtn = b; break; }
    }
    QVERIFY(moveBtn);
    QVERIFY(!moveBtn->isEnabled());

    bar.setToolEnabled(ToolId::Move, true);
    QVERIFY(moveBtn->isEnabled());
}

void tst_LeftToolBar::test_None_unchecks_all()
{
    LeftToolBar bar;
    ToolMediator med;
    ToolContext ctx;
    bar.attach(&med, &ctx);

    // 切到 Move (button 亮)
    med.switchTool(ToolId::Move);
    QTest::qWait(0);
    // 切回 None (button 全 uncheck)
    med.switchTool(ToolId::None);
    QTest::qWait(0);
    for (auto* b : bar.findChildren<QToolButton*>()) {
        QVERIFY(!b->isChecked());
    }
}

void tst_LeftToolBar::test_all_8_tools_clickable()
{
    // F-L (2026-09-10): 8 工具 button click 都能 switchTool 到对应 id
    LeftToolBar bar;
    ToolMediator med;
    ToolContext ctx;
    bar.attach(&med, &ctx);

    // 8 工具 id 跟 button text 映射 (PS 风格)
    QHash<QString, ToolId> mapping = {
        {"M", ToolId::Move},       {"R", ToolId::RectSelect},
        {"L", ToolId::Lasso},      {"W", ToolId::MagicWand},
        {"C", ToolId::Crop},       {"T", ToolId::Text},
        {"B", ToolId::Brush},      {"I", ToolId::Eyedropper}
    };

    QSignalSpy spy(&med, &ToolMediator::toolSwitched);
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        const QString text = it.key();
        const ToolId expectedId = it.value();
        // 找 button
        QToolButton* btn = nullptr;
        for (auto* b : bar.findChildren<QToolButton*>()) {
            if (b->text() == text) { btn = b; break; }
        }
        QVERIFY2(btn != nullptr, qPrintable("button not found: " + text));
        btn->click();
        QCOMPARE(qvariant_cast<ToolId>(spy.at(spy.count()-1).at(0)), expectedId);
    }
}

QTEST_MAIN(tst_LeftToolBar)
#include "tst_LeftToolBar.moc"
