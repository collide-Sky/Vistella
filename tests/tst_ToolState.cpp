// =============================================================================
//  tst_ToolState - F-C (2026-09-09) State 模式单测
//
//  验证 4 件事:
//    1) MoveTool / EyedropperTool 基础属性 (id / cursor / pageTitle)
//    2) ToolContext 切换 tool 时 onEnter/onExit 被调
//    3) 切工具 emit toolChanged 信号
//    4) EyedropperTool onMousePress 采样 image 像素 (用 mock QImage 验证)
// =============================================================================

#include <QTest>
#include <QSignalSpy>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "../src/media/imagewindow.h"
#include "../src/media/tools/ToolState.h"
#include "../src/media/tools/ToolContext.h"
#include "../src/media/tools/MoveTool.h"
#include "../src/media/tools/EyedropperTool.h"
#include "../src/media/mediators/ToolMediator.h"

#include <memory>

using namespace tools;
using namespace mediators;

class tst_ToolState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_moveTool_identity();
    void test_eyedropperTool_identity();
    void test_context_initial_none();
    void test_context_setState_calls_onEnter();
    void test_context_setState_calls_onExit();
    void test_context_setState_emits_toolChanged();
    void test_context_event_forward_to_current();
    void test_eyedropper_onMousePress_empty_image();
    void test_eyedropper_onMousePress_4ChannelBgra();
};

void tst_ToolState::initTestCase()
{
    qRegisterMetaType<ToolId>("ToolId");
}

void tst_ToolState::cleanupTestCase() {}

void tst_ToolState::test_moveTool_identity()
{
    MoveTool t;
    QCOMPARE(t.id(), ToolId::Move);
    QCOMPARE(t.cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(t.pageTitle(), QStringLiteral("Move"));
}

void tst_ToolState::test_eyedropperTool_identity()
{
    EyedropperTool t;
    QCOMPARE(t.id(), ToolId::Eyedropper);
    QCOMPARE(t.cursor().shape(), Qt::CrossCursor);
    QCOMPARE(t.pageTitle(), QStringLiteral("Eyedropper"));
    QVERIFY(!t.sampledColor().isValid());  // initial: not sampled
}

void tst_ToolState::test_context_initial_none()
{
    ToolContext ctx;
    QCOMPARE(ctx.currentToolId(), ToolId::None);
    QCOMPARE(ctx.currentState(), nullptr);
}

void tst_ToolState::test_context_setState_calls_onEnter()
{
    ToolContext ctx;
    int enterCount = 0;
    struct Probe : public ToolState {
        int* enterCount;
        Probe(int* c) : enterCount(c) {}
        void onEnter(ImageWindow*) override { ++(*enterCount); }
        void onExit(ImageWindow*) override {}
        ToolId id() const override { return ToolId::None; }
        QString pageTitle() const override { return QString(); }
        QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }
    };
    ctx.setState(std::make_unique<Probe>(&enterCount));
    QCOMPARE(enterCount, 1);

    // 第二次 setState (不同 tool) → 旧 onExit + 新 onEnter
    ctx.setState(std::make_unique<MoveTool>());
    // 第一次 Probe 已经 onEnter, 切到 MoveTool 时 Probe onExit (不计数) + MoveTool onEnter (不计数)
    // 验证 enterCount 还是 1 (Probe 不会再次 onEnter)
    QCOMPARE(enterCount, 1);
}

void tst_ToolState::test_context_setState_calls_onExit()
{
    ToolContext ctx;
    int exitCount = 0;
    struct Probe : public ToolState {
        int* exitCount;
        Probe(int* c) : exitCount(c) {}
        void onEnter(ImageWindow*) override {}
        void onExit(ImageWindow*)  override { ++(*exitCount); }
        ToolId id() const override { return ToolId::None; }
        QString pageTitle() const override { return QString(); }
        QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }
    };
    ctx.setState(std::make_unique<Probe>(&exitCount));
    QCOMPARE(exitCount, 0);
    ctx.setState(std::make_unique<MoveTool>());
    QCOMPARE(exitCount, 1);  // Probe onExit 被调
}

void tst_ToolState::test_context_setState_emits_toolChanged()
{
    ToolContext ctx;
    QSignalSpy spy(&ctx, &ToolContext::toolChanged);
    QVERIFY(spy.isValid());

    ctx.setState(std::make_unique<MoveTool>());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(0).at(0)), ToolId::Move);

    ctx.setState(std::make_unique<EyedropperTool>());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(qvariant_cast<ToolId>(spy.at(1).at(0)), ToolId::Eyedropper);
}

void tst_ToolState::test_context_event_forward_to_current()
{
    // 用 Probe 验证事件转发路径
    ToolContext ctx;
    int pressCount = 0;
    struct Probe : public ToolState {
        int* pressCount;
        Probe(int* c) : pressCount(c) {}
        void onEnter(ImageWindow*) override {}
        void onExit(ImageWindow*) override {}
        void onMousePress(QMouseEvent* e, ImageWindow* host, const QPointF& p) override {
            Q_UNUSED(e); Q_UNUSED(host); Q_UNUSED(p);
            ++(*pressCount);
        }
        ToolId id() const override { return ToolId::None; }
        QString pageTitle() const override { return QString(); }
        QCursor cursor() const override { return QCursor(Qt::ArrowCursor); }
    };
    ctx.setState(std::make_unique<Probe>(&pressCount));
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    ctx.onMousePress(&ev, QPointF(10, 20));
    QCOMPARE(pressCount, 1);

    // 切到 MoveTool, MoveTool.onMousePress 是 no-op
    ctx.setState(std::make_unique<MoveTool>());
    QMouseEvent ev2(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    ctx.onMousePress(&ev2, QPointF(10, 20));
    // pressCount 仍 1 (MoveTool 没接事件)
    QCOMPARE(pressCount, 1);
}

void tst_ToolState::test_eyedropper_onMousePress_empty_image()
{
    // F-C 阶段 image 为空时 eyedropper 应安全 no-op, 不崩
    //   完整 version 需要 ImageWindow 真实环境, 这里只验证 nullptr host 安全
    EyedropperTool t;
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    t.onMousePress(&ev, nullptr, QPointF(10, 20));
    QVERIFY(!t.sampledColor().isValid());  // 没采到
}

// The composite is CV_8UC4 BGRA (the pixel contract in
// docs/CONTRACTS.md 7.6.1). EyedropperTool used to read the pixel as
// cv::Vec3b, which asserts in a debug build and reads the wrong bytes in a
// release one. The colour channels are BGR in the 4-channel case too, and
// the alpha channel is not part of the sampled colour.
void tst_ToolState::test_eyedropper_onMousePress_4ChannelBgra()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("eyedropper.png");

    // The image must VARY with position.
    //
    //   Mat::at<Vec3b>(y,x) addresses the pixel as
    //       data + y*step + x*elemSize()      (elemSize == 3)
    //   while the real pixel of a 4-channel image lives at
    //       data + y*step + x*4
    //   so the read drifts by x bytes. On a uniform image every byte is
    //   the same and the drift is invisible -- the test would pass with
    //   the bug still in place. A horizontal ramp makes the drift
    //   observable: sampling (x,y) must return the colour of (x,y).
    {
        QImage img(64, 64, QImage::Format_RGB888);
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                // Pure blue channel ramp: B = x, G = y, R = 0.
                img.setPixel(x, y, qRgb(0, y, x));
            }
        }
        QVERIFY(img.save(png));
    }

    ImageWindow* w = new ImageWindow();
    QString err;
    QVERIFY2(w->loadFile(png, &err), qPrintable(err));

    // The render pipeline produces the 4-channel composite. If it did not,
    // this test would not be testing what it claims to test.
    QCOMPARE(w->currentImage().channels(), 4);

    EyedropperTool t;
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    t.onMousePress(&ev, w, QPointF(32, 32));

    QVERIFY(t.sampledColor().isValid());
    // The image is a ramp: B = x, G = y, R = 0. Sampling (32, 32) must
    // give exactly that -- a drifted read gives a neighbouring pixel.
    QCOMPARE(t.sampledColor().red(), 0);
    QCOMPARE(t.sampledColor().green(), 32);
    QCOMPARE(t.sampledColor().blue(), 32);

    // Sample a second position: the drift is x bytes, so a second x
    // value pins the behaviour down from both sides.
    EyedropperTool t2;
    QMouseEvent ev2(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
    t2.onMousePress(&ev2, w, QPointF(10, 20));
    QVERIFY(t2.sampledColor().isValid());
    QCOMPARE(t2.sampledColor().red(), 0);
    QCOMPARE(t2.sampledColor().green(), 20);
    QCOMPARE(t2.sampledColor().blue(), 10);

    delete w;
}

QTEST_MAIN(tst_ToolState)
#include "tst_ToolState.moc"
