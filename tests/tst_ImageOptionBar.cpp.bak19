// SPDX-License-Identifier: MIT
//
// tst_ImageOptionBar - F-E (2026-09-09)
//
// 验证:
//   1) 9 个 page (None + 8 tools) 都在 stackedWidget
//   2) setCurrentIndex(id) 切 page 正确
//   3) attach(ctx) 监听 ctx->toolChanged 自动切 page
//
#include <QTest>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QWidget>

#include "../src/media/tools/ImageOptionBar.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/mediators/ToolMediator.h"

using namespace tools;
using namespace mediators;

class tst_ImageOptionBar : public QObject
{
    Q_OBJECT
private slots:
    void ctor_have9Pages();
    void ctor_defaultPageIsNone();
    void attach_listenToolChanged();
    void setCurrentIndex_outOfRangeIgnored();
};

void tst_ImageOptionBar::ctor_have9Pages()
{
    ImageOptionBar bar;
    QStackedWidget* stack = bar.findChild<QStackedWidget*>("stackedWidget");
    QVERIFY(stack != nullptr);
    // 9 pages: None(0) + 8 tools
    QCOMPARE(stack->count(), 9);
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

QTEST_MAIN(tst_ImageOptionBar)
#include "tst_ImageOptionBar.moc"
