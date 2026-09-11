// SPDX-License-Identifier: MIT
//
// tst_F_N_ToolCursor - F-N (2026-09-10)
//
// 验证:
//   1) MoveTool cursor = SizeAllCursor (PS OpenHand simplified)
//   2) Text cursor = IBeamCursor
//   3) EyedropperTool cursor = CrossCursor
//   4) ToolContext::onMousePress / Move / Release 转发到 current ToolState
//   5) 8 工具 cursor 都不等于 ArrowCursor (PS 风格全覆盖)
//
#include <QTest>
#include <QSignalSpy>
#include <QCursor>
#include <QMouseEvent>
#include <QPointF>

#include "../src/media/tools/MoveTool.h"
#include "../src/media/tools/RectSelect.h"
#include "../src/media/tools/Lasso.h"
#include "../src/media/tools/MagicWand.h"
#include "../src/media/tools/Crop.h"
#include "../src/media/tools/Text.h"
#include "../src/media/tools/Brush.h"
#include "../src/media/tools/EyedropperTool.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/ToolState.h"

using namespace tools;

class MockTool : public ToolState
{
public:
    int nPress = 0;
    int nMove  = 0;
    int nRelease = 0;
    QPointF lastScenePos;

    void onEnter(ImageWindow*) override {}
    void onExit(ImageWindow*)  override {}
    mediators::ToolId id() const override { return mediators::ToolId::None; }
    QString pageTitle() const override { return QString("Mock"); }
    QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }

    void onMousePress(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& sp) override {
        ++nPress; lastScenePos = sp;
    }
    void onMouseMove(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& sp) override {
        ++nMove; lastScenePos = sp;
    }
    void onMouseRelease(QMouseEvent* /*e*/, ImageWindow* /*host*/, const QPointF& sp) override {
        ++nRelease; lastScenePos = sp;
    }
};

class tst_F_N_ToolCursor : public QObject
{
    Q_OBJECT
private slots:
    void cursor_MoveToolIsSizeAll();
    void cursor_TextToolIsIBeam();
    void cursor_EyedropperIsCross();
    void cursor_All8ToolsNotArrow();
    void toolContext_ForwardsMouseToState();
};

// ===== F-N: 8 工具 cursor 验证 =====
void tst_F_N_ToolCursor::cursor_MoveToolIsSizeAll()
{
    MoveTool tool;
    QCOMPARE(tool.cursor().shape(), Qt::SizeAllCursor);
}

void tst_F_N_ToolCursor::cursor_TextToolIsIBeam()
{
    Text tool;
    QCOMPARE(tool.cursor().shape(), Qt::IBeamCursor);
}

void tst_F_N_ToolCursor::cursor_EyedropperIsCross()
{
    EyedropperTool tool;
    QCOMPARE(tool.cursor().shape(), Qt::CrossCursor);
}

void tst_F_N_ToolCursor::cursor_All8ToolsNotArrow()
{
    // 验证 8 工具 cursor 都不是 default ArrowCursor (PS 风格全覆盖)
    QCOMPARE(MoveTool().cursor().shape(),       Qt::SizeAllCursor);
    QCOMPARE(RectSelect().cursor().shape(),     Qt::CrossCursor);
    QCOMPARE(Lasso().cursor().shape(),          Qt::CrossCursor);
    QCOMPARE(MagicWand().cursor().shape(),      Qt::CrossCursor);
    QCOMPARE(Crop().cursor().shape(),           Qt::CrossCursor);
    QCOMPARE(Text().cursor().shape(),           Qt::IBeamCursor);
    QCOMPARE(Brush().cursor().shape(),          Qt::CrossCursor);
    QCOMPARE(EyedropperTool().cursor().shape(), Qt::CrossCursor);
}

// ===== F-N: ToolContext 事件转发 =====
void tst_F_N_ToolCursor::toolContext_ForwardsMouseToState()
{
    ToolContext ctx;
    auto* mock = new MockTool();   // owned by ctx via std::unique_ptr
    ctx.setState(std::unique_ptr<ToolState>(mock));

    QPointF scenePos(12.5, 34.5);
    QMouseEvent press(QEvent::MouseButtonPress, QPoint(10, 20), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent moveEv(QEvent::MouseMove, QPoint(11, 21), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPoint(12, 22), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

    ctx.onMousePress(&press, scenePos);
    QCOMPARE(mock->nPress, 1);
    QCOMPARE(mock->lastScenePos, scenePos);

    ctx.onMouseMove(&moveEv, scenePos);
    QCOMPARE(mock->nMove, 1);
    QCOMPARE(mock->lastScenePos, scenePos);

    ctx.onMouseRelease(&release, scenePos);
    QCOMPARE(mock->nRelease, 1);
    QCOMPARE(mock->lastScenePos, scenePos);
}

QTEST_MAIN(tst_F_N_ToolCursor)
#include "tst_F_N_ToolCursor.moc"
