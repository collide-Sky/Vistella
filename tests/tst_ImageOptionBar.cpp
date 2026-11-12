// SPDX-License-Identifier: MIT
//
// tst_ImageOptionBar - F-E (2026-09-09) + Q4.2.4 (2026-10-08) + P5.1
//
// 验证:
//   F-E:   1) 9 个 page (None + 8 tools) 都在 stackedWidget
//          2) setCurrentIndex(id) 切 page 正确
//          3) attach(ctx) 监听 ctx->toolChanged 自动切 page
//   Q4.2.4:
//          4) page 数量 17 (None + 16 tools, MaskBrush + Transform 动态加 page)
//          5) setCurrentIndex 越界忽略
//          6) attach 后 listen 完整 16 tool id (包含 MaskBrush + Transform)
//          7) 切到 MaskBrush 触发动态 addPage, 切到 None 不减少 page
//   P5.1:
//          8) attach(ctx, mosaic, textCtrl) 时, Brush tool 路由到 Mosaic page
//             (idx=17), Text tool 路由到 Text page (idx=18)
//          9) MosaicTool modeChanged (on Brush tool) 切换 page idx
//             17 ↔ 7 (跟 BrushOptionPanel toggle)
//         10) attach 不传 mosaic/textCtrl 时, 不创建 Mosaic/Text page (graceful)
//
#include <QTest>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QWidget>

// P5.1 (2026-10-08): include imagewindow.h BEFORE ImageOptionBar.h
//   so QPointer<ImageWindow> in MosaicTool.h finds the full QObject-derived
//   type when instantiated.
#include "../src/media/imagewindow.h"
#include "../src/media/tools/ImageOptionBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/BrushOptionsPanel.h"
#include "../src/media/mediators/ToolMediator.h"
#include "../src/media/imagewindow/MosaicTool.h"

using namespace tools;
using namespace mediators;

class tst_ImageOptionBar : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void ctor_have9Pages();
    void ctor_defaultPageIsNone();
    void attach_listenToolChanged();
    void setCurrentIndex_outOfRangeIgnored();

    // Q4.2.4 (2026-10-08) 扩展
    void ctor_have17Pages();
    void attach_listenAll17ToolIds();
    void attach_maskBrush_dynamicAddPage();

    // P5.1 (2026-10-08) 扩展
    void attach_brushTool_routesToMosaicPage();
    void attach_textTool_routesToTextPage();
    void attach_mosaicModeToggle_switchesBetweenBrushAndMosaic();
    void attach_noMosaicNoText_gracefulNoPage();

    // P8.3.2 (2026-10-26) 扩展
    void installBrushOptionsPage_putsPanelInBrushSlot();
    void installBrushOptionsPage_keepsPageCount();
    void installBrushOptionsPage_isIdempotent();
};

void tst_ImageOptionBar::initTestCase()
{
    qRegisterMetaType<ToolId>("ToolId");
}

void tst_ImageOptionBar::cleanupTestCase() {}

void tst_ImageOptionBar::ctor_have9Pages()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    // 25 pages: None(0) + 24 tools.
    //   P0-9 (2026-09-15): +7 pages for Shape/Pen/Clone/Heal/Patch/RedEye (was 9)
    //   Q4.2.1 (2026-09-23): +1 page (MaskBrush P1.3.4 像素蒙版画笔, page16) = 17
    //   M2-4: +8 pages for the tools that were missing (Eraser / Gradient /
    //     PaintBucket / DodgeBurn / BlurSharpenSmudge / Hand / Zoom / Rotate)
    //     = 25.
    //   M2 尾巴: +4 pages (PatternStamp 25 / HistoryBrush 26 /
    //     EllipseSelect 27 / PolygonSelect 28) = 29.
    //
    //   The ToolId VALUE is the page index, so the page count has to
    //   grow with every new tool -- otherwise insertWidget() would push
    //   the pages behind it out of the way.
    QCOMPARE(stack->count(), 29);
}

void tst_ImageOptionBar::ctor_defaultPageIsNone()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));
}

void tst_ImageOptionBar::attach_listenToolChanged()
{
    ImageOptionBar bar;
    ToolContext ctx;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    bar.attach(&ctx);
    // attach 同步初始状态 (None)
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));

    // 直接 emit ToolContext::toolChanged 测切换
    emit ctx.toolChanged(ToolId::Eyedropper);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Eyedropper));

    emit ctx.toolChanged(ToolId::Lasso);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Lasso));

    emit ctx.toolChanged(ToolId::Move);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Move));

    emit ctx.toolChanged(ToolId::None);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));

    bar.detach();
}

void tst_ImageOptionBar::setCurrentIndex_outOfRangeIgnored()
{
    ImageOptionBar bar;
    ToolContext ctx;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    bar.attach(&ctx);
    // id=99 越界, should be ignored (LOG_WARN, no crash)
    emit ctx.toolChanged(static_cast<ToolId>(99));
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));
}

// =============================================================================
//  Q4.2.4 (2026-10-08) 扩展
// =============================================================================

void tst_ImageOptionBar::ctor_have17Pages()
{
    // 29 pages (None + 28 tools; MaskBrush + Transform 走动态 addPage 路径).
    //   MaskBrush (idx=16) + Transform (idx=9) 走动态 addPage 路径.
    //   M2-4: +8 个新增工具的占位页 = 25.
    //   M2 尾巴: +4 个新增工具的占位页 (PatternStamp 25 / HistoryBrush 26 /
    //     EllipseSelect 27 / PolygonSelect 28) = 29.
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->count(), 29);
}

void tst_ImageOptionBar::attach_listenAll17ToolIds()
{
    // attach 后, 完整的 29 个 ToolId 都能切 page.
    ImageOptionBar bar;
    ToolContext ctx;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    bar.attach(&ctx);

    // 0..28 全切一遍, 不 crash + currentIndex 跟着切
    //
    //   M2-4: 上限从 16 提到 24 -- 新增的 8 个工具 (Eraser / Gradient /
    //   PaintBucket / DodgeBurn / BlurSharpenSmudge / Hand / Zoom /
    //   Rotate) 也必须有自己的一页, 否则 insertWidget 会把后面的页挤走.
    //
    //   M2 尾巴: 上限再提到 28 -- PatternStamp / HistoryBrush /
    //   EllipseSelect / PolygonSelect 同样各占一页.
    for (int i = 0; i <= 28; ++i) {
        emit ctx.toolChanged(static_cast<ToolId>(i));
        QCOMPARE(stack->currentIndex(), i);
    }
    bar.detach();
}

void tst_ImageOptionBar::attach_maskBrush_dynamicAddPage()
{
    // 切到 MaskBrush 应该触发动态 addPage (MaskOptionsPanel)
    ImageOptionBar bar;
    ToolContext ctx;
    bar.attach(&ctx);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    const int beforeCount = stack->count();

    // 切到 MaskBrush (idx=16)
    emit ctx.toolChanged(ToolId::MaskBrush);
    // dynamic addPage 触发, page 数量不变 (MaskBrush slot 16 是预留在 ui 里的, addWidget 在末尾再加)
    //   Q4.2.1 注释: ui 预留 page16 占位, 动态 addPage 装 MaskOptionsPanel
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::MaskBrush));

    // 切回 None (不应减少 page - MaskBrush slot 仍在 ui)
    emit ctx.toolChanged(ToolId::None);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));
    QVERIFY(stack->count() >= beforeCount);
}

// =============================================================================
//  P5.1 (2026-10-08) 扩展 — Mosaic + Text 接 ImageOptionBar 路由
// =============================================================================

void tst_ImageOptionBar::attach_brushTool_routesToMosaicPage()
{
    // M2-4: the brush routes to the mosaic page ONLY when the mosaic
    //   mode is actually ON.
    //
    //   Before, the condition was just "the brush tool is selected and
    //   the mosaic tool exists" -- and the mosaic tool always exists.
    //   So the brush ALWAYS showed the mosaic page, the branch that
    //   shows the real brush options was unreachable, and the user saw
    //   no brush options at all (docs/DEBT.md#A-BRUSHOPTIONS).
    ImageOptionBar bar;
    ToolContext ctx;
    MosaicTool mosaic;
    bar.attach(&ctx, &mosaic, nullptr);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    // The mosaic mode is off -> the brush shows its OWN page (idx 7).
    emit ctx.toolChanged(ToolId::Brush);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Brush));

    // The mosaic mode is on -> the brush shows the mosaic page.
    mosaic.setEnabled(true);
    emit ctx.toolChanged(ToolId::Brush);
    QVERIFY(bar.mosaicPageIdx() >= 0);
    QCOMPARE(stack->currentIndex(), bar.mosaicPageIdx());
    QVERIFY(stack->count() >= bar.mosaicPageIdx() + 1);

    bar.detach();
}

void tst_ImageOptionBar::attach_textTool_routesToTextPage()
{
    ImageOptionBar bar;
    ToolContext ctx;
    MosaicTool mosaic;
    TextOverlayController textCtrl;
    bar.attach(&ctx, &mosaic, &textCtrl);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    emit ctx.toolChanged(ToolId::Text);
    QVERIFY(bar.textPageIdx() >= 0);
    QCOMPARE(stack->currentIndex(), bar.textPageIdx());
    QVERIFY(stack->count() >= bar.textPageIdx() + 1);

    bar.detach();
}

void tst_ImageOptionBar::attach_mosaicModeToggle_switchesBetweenBrushAndMosaic()
{
    // M2-4: the mosaic page is shown only while the mosaic mode is ON.
    //
    //   The toggle is what decides which of the two pages the brush
    //   shows: the mosaic options while the mosaic is on, the brush
    //   options while it is off.
    ImageOptionBar bar;
    ToolContext ctx;
    MosaicTool mosaic;
    bar.attach(&ctx, &mosaic, nullptr);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    // The mosaic is off -> the brush page.
    emit ctx.toolChanged(ToolId::Brush);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Brush));

    // Toggle mosaic on -> the mosaic page.
    QSignalSpy spy(&mosaic, &MosaicTool::modeChanged);
    mosaic.setEnabled(true);
    QCOMPARE(spy.count(), 1);
    emit ctx.toolChanged(ToolId::Brush);
    const int mosaicIdx = bar.mosaicPageIdx();
    QVERIFY(mosaicIdx >= 0);
    QCOMPARE(stack->currentIndex(), mosaicIdx);

    // Toggle mosaic off -> the brush page again.
    mosaic.setEnabled(false);
    QCOMPARE(spy.count(), 2);
    emit ctx.toolChanged(ToolId::Brush);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Brush));

    bar.detach();
}

void tst_ImageOptionBar::attach_noMosaicNoText_gracefulNoPage()
{
    // P5.1: attach 不传 deps, 不要 addPage, 切换也不崩.
    ImageOptionBar bar;
    ToolContext ctx;
    bar.attach(&ctx);  // no deps
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    const int baseCount = stack->count();

    emit ctx.toolChanged(ToolId::Brush);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Brush));
    QVERIFY(stack->count() == baseCount);  // no addPage triggered

    emit ctx.toolChanged(ToolId::Text);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::Text));
    QVERIFY(stack->count() == baseCount);

    emit ctx.toolChanged(ToolId::None);
    QCOMPARE(stack->currentIndex(), static_cast<int>(ToolId::None));

    bar.detach();
}

// ---------------------------------------------------------------------------
// P8.3.2 (2026-10-26): the persistent brush panel must have a real home
// ---------------------------------------------------------------------------

void tst_ImageOptionBar::installBrushOptionsPage_putsPanelInBrushSlot()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    auto* panel = new BrushOptionsPanel();
    bar.installBrushOptionsPage(panel);

    const int brushIdx = static_cast<int>(ToolId::Brush);
    QCOMPARE(stack->widget(brushIdx), static_cast<QWidget*>(panel));
    // Owned by the stacked widget now, so it can no longer paint itself
    // over the main window as a stray, unmanaged child.
    QCOMPARE(panel->parentWidget(), static_cast<QWidget*>(stack));
}

void tst_ImageOptionBar::installBrushOptionsPage_keepsPageCount()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    const int before = stack->count();

    auto* panel = new BrushOptionsPanel();
    bar.installBrushOptionsPage(panel);

    // The placeholder is replaced, not appended: the ToolId -> page index
    // mapping must not shift.
    QCOMPARE(stack->count(), before);
    QCOMPARE(stack->widget(static_cast<int>(ToolId::MaskBrush))->objectName(),
             QStringLiteral("page16"));
}

void tst_ImageOptionBar::installBrushOptionsPage_isIdempotent()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    const int before = stack->count();

    auto* panel = new BrushOptionsPanel();
    bar.installBrushOptionsPage(panel);
    bar.installBrushOptionsPage(panel);

    QCOMPARE(stack->count(), before);
    QCOMPARE(stack->widget(static_cast<int>(ToolId::Brush)), static_cast<QWidget*>(panel));
}

QTEST_MAIN(tst_ImageOptionBar)
#include "tst_ImageOptionBar.moc"
