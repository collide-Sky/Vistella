// SPDX-License-Identifier: MIT
//
// tst_ModeSelectorBar - Q4.2.4 (2026-10-08)
//
// 验证 5 tools (MagicWand/Lasso/Crop/Brush/Eyedropper) 二级模式选择栏:
//   1) ctor 创建 5 page, button 总数 15 (selection 4 + crop 4 + blend 3 + sample 4)
//   2) attach ToolContext 后, toolChanged 切到对应 page (QStackedWidget::currentIndex)
//   3) 按钮 click 调 tool.setXxx (setSelectionMode / setAspectRatio / setBlendMode / setSampleSize)
//   4) 切到工具时 refresh 从 tool.getter 同步按钮高亮状态
//   5) 不支持的工具 (Move/RectSelect/Text/...) 显示 empty page (idx=4)
//
// 注意事项:
//   - QStackedWidget 的 page 实际可见性依赖顶层 widget 已 show. 测试里用
//     bar.show() 让 widget 树可见, 然后用 QStackedWidget::currentIndex()
//     验证切到哪个 page (比 isVisible() 更可靠).
//   - 按钮 click 不依赖可见性 (QTest::mouseClick 也行), 但用 click() 更简单.
//
#include <QTest>
#include <QButtonGroup>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>

#include "../src/media/tools/ModeSelectorBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/MagicWand.h"
#include "../src/media/tools/Lasso.h"
#include "../src/media/tools/Crop.h"
#include "../src/media/tools/Brush.h"
#include "../src/media/tools/EyedropperTool.h"
#include "../src/media/mediators/ToolMediator.h"
#include "../src/media/selection/SelectionModel.h"

using namespace tools;
using namespace mediators;

namespace {
// Q4.2.4 fix: 顶层 widget show 后, QStackedWidget 的 page isVisibleTo(parent) 才正确.
//   bar 没 show 时整个 widget 树都不可见, currentIndex() 是 source of truth.
void showBar(ModeSelectorBar& bar)
{
    bar.show();
    QTest::qWait(0);
}

// QStackedWidget index:
//   0 = selection (MagicWand + Lasso 共享)
//   1 = crop
//   2 = brush
//   3 = sample (Eyedropper)
//   4 = empty (其他工具)
constexpr int kSelIdx   = 0;
constexpr int kCropIdx  = 1;
constexpr int kBrushIdx = 2;
constexpr int kSampIdx  = 3;
constexpr int kEmptyIdx = 4;
} // namespace

class tst_ModeSelectorBar : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_ctor_hasPages();
    void test_attach_syncInitial_nonePage();
    void test_toolChanged_magicWand_showsSelectionPage();
    void test_toolChanged_lasso_showsSelectionPage();
    void test_toolChanged_crop_showsCropPage();
    void test_toolChanged_brush_showsBlendPage();
    void test_toolChanged_eyedropper_showsSamplePage();
    void test_toolChanged_move_showsEmptyPage();
    void test_selectionModeBtn_click_setsToolMode();
    void test_aspectBtn_click_setsCropAspect();
    void test_blendBtn_click_setsBrushBlend();
    void test_sampleBtn_click_setsEyedropperSample();
    void test_refresh_syncsButtonsFromToolState();
};

void tst_ModeSelectorBar::initTestCase()
{
    qRegisterMetaType<ToolId>("ToolId");
    qRegisterMetaType<selection::SelectionModel::Mode>("SelectionMode");
}

void tst_ModeSelectorBar::cleanupTestCase() {}

void tst_ModeSelectorBar::test_ctor_hasPages()
{
    // Q4.2.4 (2026-10-08): ModeSelectorBar ctor 创建 5 pages.
    //   selection (4 btn) + crop (4 btn) + brush (3 btn) + sample (4 btn) = 15 buttons total.
    //   empty page 不贡献 button (只有 QLabel hint).
    ModeSelectorBar bar;
    QCOMPARE(bar.findChildren<QToolButton*>().size(), 15);
}

void tst_ModeSelectorBar::test_attach_syncInitial_nonePage()
{
    // attach 之前默认显示 empty page (idx=4).
    ModeSelectorBar bar;
    ToolContext ctx;
    bar.attach(&ctx);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kEmptyIdx);

    // empty page 的 QLabel hint 存在
    auto labels = bar.findChildren<QLabel*>();
    QVERIFY(!labels.isEmpty());
}

void tst_ModeSelectorBar::test_toolChanged_magicWand_showsSelectionPage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<MagicWand>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::MagicWand);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kSelIdx);
}

void tst_ModeSelectorBar::test_toolChanged_lasso_showsSelectionPage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<Lasso>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Lasso);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kSelIdx);
}

void tst_ModeSelectorBar::test_toolChanged_crop_showsCropPage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<Crop>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Crop);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kCropIdx);
}

void tst_ModeSelectorBar::test_toolChanged_brush_showsBlendPage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<Brush>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Brush);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kBrushIdx);
}

void tst_ModeSelectorBar::test_toolChanged_eyedropper_showsSamplePage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<EyedropperTool>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Eyedropper);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kSampIdx);
}

void tst_ModeSelectorBar::test_toolChanged_move_showsEmptyPage()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Move);
    showBar(bar);

    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    QCOMPARE(stacked->currentIndex(), kEmptyIdx);
}

void tst_ModeSelectorBar::test_selectionModeBtn_click_setsToolMode()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<MagicWand>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::MagicWand);
    showBar(bar);

    auto* mw = dynamic_cast<MagicWand*>(ctx.currentState());
    QVERIFY(mw);
    QCOMPARE(mw->selectionMode(), selection::SelectionModel::Mode::Replace);

    // 找 selection page 的 4 个 button (idx 0..3), click idx=2 (Subtract)
    auto buttons = bar.findChildren<QToolButton*>();
    // selection page button 顺序: 0=New, 1=Add, 2=Subtract, 3=Intersect
    //   (buttons 全在 bar.findChildren 里, 我们要找 active page 的)
    // 因为现在 pageSelection 在 stacked[0], 找 visible=true 的 button
    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QVERIFY(stacked);
    int clicked = 0;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (clicked == 2) {
                b->click();
                break;
            }
            ++clicked;
        }
    }
    QCOMPARE(mw->selectionMode(), selection::SelectionModel::Mode::Subtract);
}

void tst_ModeSelectorBar::test_aspectBtn_click_setsCropAspect()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<Crop>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Crop);
    showBar(bar);

    auto* cr = dynamic_cast<Crop*>(ctx.currentState());
    QVERIFY(cr);
    QCOMPARE(cr->aspectRatio(), Crop::AspectRatio::Free);

    // crop page button 顺序: 0=Free, 1=1:1, 2=4:3, 3=16:9
    auto buttons = bar.findChildren<QToolButton*>();
    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    int clicked = 0;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (clicked == 2) {
                b->click();
                break;
            }
            ++clicked;
        }
    }
    QCOMPARE(cr->aspectRatio(), Crop::AspectRatio::Ratio4_3);
}

void tst_ModeSelectorBar::test_blendBtn_click_setsBrushBlend()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<Brush>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Brush);
    showBar(bar);

    auto* br = dynamic_cast<Brush*>(ctx.currentState());
    QVERIFY(br);
    QCOMPARE(br->blendMode(), 0);

    // blend page button 顺序: 0=Normal, 1=Multiply, 2=Screen
    auto buttons = bar.findChildren<QToolButton*>();
    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    int clicked = 0;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (clicked == 1) {
                b->click();
                break;
            }
            ++clicked;
        }
    }
    QCOMPARE(br->blendMode(), 1);
}

void tst_ModeSelectorBar::test_sampleBtn_click_setsEyedropperSample()
{
    ModeSelectorBar bar;
    ToolContext ctx;
    ctx.setState(std::make_unique<EyedropperTool>());
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::Eyedropper);
    showBar(bar);

    auto* ed = dynamic_cast<EyedropperTool*>(ctx.currentState());
    QVERIFY(ed);
    QCOMPARE(ed->sampleSize(), 1);

    // sample page button 顺序: 0=Point, 1=3x3, 2=5x5, 3=11x11
    auto buttons = bar.findChildren<QToolButton*>();
    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    int clicked = 0;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (clicked == 2) {
                b->click();
                break;
            }
            ++clicked;
        }
    }
    QCOMPARE(ed->sampleSize(), 5);
}

void tst_ModeSelectorBar::test_refresh_syncsButtonsFromToolState()
{
    // 切到 MagicWand, 用 tool.setSelectionMode(Subtract) 改状态,
    //   应该被 refreshButtonsFromTool 同步到 button 高亮 (idx=2 checked).
    ModeSelectorBar bar;
    ToolContext ctx;
    auto* mw = new MagicWand();
    mw->setSelectionMode(selection::SelectionModel::Mode::Subtract);
    ctx.setState(std::unique_ptr<MagicWand>(mw));
    bar.attach(&ctx);
    emit ctx.toolChanged(ToolId::MagicWand);
    showBar(bar);

    // 找 idx=2 的 button (Subtract), 应该 checked
    auto buttons = bar.findChildren<QToolButton*>();
    QStackedWidget* stacked = bar.findChild<QStackedWidget*>();
    QToolButton* btnSubtract = nullptr;
    int idx = 0;
    for (auto* b : buttons) {
        if (b->isVisibleTo(stacked)) {
            if (idx == 2) btnSubtract = b;
            ++idx;
        }
    }
    QVERIFY(btnSubtract);
    QVERIFY(btnSubtract->isChecked());
}

QTEST_MAIN(tst_ModeSelectorBar)
#include "tst_ModeSelectorBar.moc"
