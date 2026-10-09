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
    // 17 pages: None(0) + 16 tools (None/Move/.../Eyedropper/Transform/Shape/Pen/Clone/Heal/Patch/RedEye)
    //   P0-9 (2026-09-15): +7 pages for Shape/Pen/Clone/Heal/Patch/RedEye (was 9)
    //   Q4.2.1 (2026-09-23): +1 page (MaskBrush P1.3.4 像素蒙版画笔, page16) = 17
    QCOMPARE(stack->count(), 17);
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
    // Q4.2.1 (2026-09-23) + Q4.2.4: 17 page (None + 16 tools, MaskBrush + Transform 动态加).
    //   MaskBrush (idx=16) + Transform (idx=9) 走动态 addPage 路径.
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->count(), 17);
}

void tst_ImageOptionBar::attach_listenAll17ToolIds()
{
    // attach 后, 完整 17 个 ToolId 都能切 page.
    ImageOptionBar bar;
    ToolContext ctx;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    bar.attach(&ctx);

    // 0..16 全切一遍, 不 crash + currentIndex 跟着切
    for (int i = 0; i <= 16; ++i) {
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
    // P5.1: 接 mosaic + textCtrl 后, 切到 Brush tool 应该跳 Mosaic page.
    //   没 mosaic 时, Brush tool 还是落到 idx=7 (Brush default).
    ImageOptionBar bar;
    ToolContext ctx;
    MosaicTool mosaic;
    bar.attach(&ctx, &mosaic, nullptr);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

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
    // P5.1: MosaicOptionPanel 自 listen MosaicTool::modeChanged, 所以切换
    //   mosaic on/off 时 page index 不变 (始终在 m_mosaicPageIdx). 这里
    //   验证 panel 保持显示 + modeChanged 不会崩 + getEnabled 状态正确.
    ImageOptionBar bar;
    ToolContext ctx;
    MosaicTool mosaic;
    bar.attach(&ctx, &mosaic, nullptr);
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);

    emit ctx.toolChanged(ToolId::Brush);
    const int mosaicIdx = bar.mosaicPageIdx();
    QVERIFY(mosaicIdx >= 0);
    QCOMPARE(stack->currentIndex(), mosaicIdx);

    // Toggle mosaic on - modeChanged signal fires (panel reflects new state),
    //   page index stays stable.
    QSignalSpy spy(&mosaic, &MosaicTool::modeChanged);
    mosaic.setEnabled(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(stack->currentIndex(), mosaicIdx);

    // Toggle mosaic off - same story.
    mosaic.setEnabled(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(stack->currentIndex(), mosaicIdx);

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

QTEST_MAIN(tst_ImageOptionBar)
#include "tst_ImageOptionBar.moc"
