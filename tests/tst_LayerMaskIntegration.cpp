// LayerStack mask integration + LayerMaskCommand unit tests.
//
// Coverage:
//   1.  LayerStack::addPixelMask populates mask.kind/pixel/enabled.
//   2.  addVectorMask populates mask.vectorPaths.
//   3.  clearMaskFull resets the mask struct.
//   4.  setMaskEnabled flips enabled flag.
//   5.  setMaskDensity / setMaskFeather / setMaskInvert work.
//   6.  maskAt returns nullptr for invalid index.
//   7.  LayerMaskCommand undo restores before-state.
//   8.  LayerMaskCommand redo applies after-state.

#include <QtTest>
#include <QUndoStack>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerMask.h"
#include "../src/media/imageworker/layers/LayerMaskCommand.h"
#include "../src/media/imageworker/layers/LayerStack.h"

using namespace layers;

class TestLayerMaskIntegration : public QObject {
    Q_OBJECT

private:
    static LayerStack* makeStack() {
        auto* s = new LayerStack;
        // Add a Bitmap layer with a 20x20 solid BGR image.
        cv::Mat img(20, 20, CV_8UC3, cv::Scalar(100, 150, 200, 0));
        s->addLayer(Layer("base", img));
        return s;
    }

private slots:
    void addPixelMaskPopulatesStruct();
    void addVectorMaskPopulatesStruct();
    void clearMaskFullResetsStruct();
    void setMaskEnabledFlipsFlag();
    void densityFeatherInvertSettersWork();
    void maskAtReturnsNullForInvalidIndex();
    void commandUndoRestoresBefore();
    void commandRedoAppliesAfter();
};

void TestLayerMaskIntegration::addPixelMaskPopulatesStruct() {
    LayerStack* s = makeStack();
    cv::Mat mask(20, 20, CV_8UC1, cv::Scalar(255));
    QVERIFY(s->addPixelMask(0, mask));
    const LayerMask* m = s->maskAt(0);
    QVERIFY(m != nullptr);
    QCOMPARE(m->kind, LayerMask::Pixel);
    QVERIFY(m->enabled);
    QCOMPARE(m->pixel.size(), cv::Size(20, 20));
    delete s;
}

void TestLayerMaskIntegration::addVectorMaskPopulatesStruct() {
    LayerStack* s = makeStack();
    QVector<QPainterPath> paths;
    QPainterPath p;
    p.addRect(2, 2, 10, 10);
    paths.append(p);
    QVERIFY(s->addVectorMask(0, paths));
    const LayerMask* m = s->maskAt(0);
    QVERIFY(m != nullptr);
    QCOMPARE(m->kind, LayerMask::Vector);
    QVERIFY(m->enabled);
    QCOMPARE(m->vectorPaths.size(), 1);
    delete s;
}

void TestLayerMaskIntegration::clearMaskFullResetsStruct() {
    LayerStack* s = makeStack();
    cv::Mat mask(20, 20, CV_8UC1, cv::Scalar(128));
    s->addPixelMask(0, mask);
    QVERIFY(s->clearMaskFull(0));
    const LayerMask* m = s->maskAt(0);
    QCOMPARE(m->kind, LayerMask::None);
    QVERIFY(!m->enabled);
    QVERIFY(m->pixel.empty());
    delete s;
}

void TestLayerMaskIntegration::setMaskEnabledFlipsFlag() {
    LayerStack* s = makeStack();
    cv::Mat mask(20, 20, CV_8UC1, cv::Scalar(255));
    s->addPixelMask(0, mask);
    QVERIFY(s->setMaskEnabled(0, false));
    QCOMPARE(s->maskAt(0)->enabled, false);
    QVERIFY(s->setMaskEnabled(0, true));
    QCOMPARE(s->maskAt(0)->enabled, true);
    delete s;
}

void TestLayerMaskIntegration::densityFeatherInvertSettersWork() {
    LayerStack* s = makeStack();
    cv::Mat mask(20, 20, CV_8UC1, cv::Scalar(255));
    s->addPixelMask(0, mask);
    QVERIFY(s->setMaskDensity(0, 0.5));
    QCOMPARE(s->maskAt(0)->density, 0.5);
    QVERIFY(s->setMaskFeather(0, 2.5));
    QCOMPARE(s->maskAt(0)->feather, 2.5);
    QVERIFY(s->setMaskInvert(0, true));
    QCOMPARE(s->maskAt(0)->invert, true);
    // Out-of-range density clamps.
    s->setMaskDensity(0, 5.0);
    QCOMPARE(s->maskAt(0)->density, 1.0);
    delete s;
}

void TestLayerMaskIntegration::maskAtReturnsNullForInvalidIndex() {
    LayerStack* s = makeStack();
    QCOMPARE(s->maskAt(-1), nullptr);
    QCOMPARE(s->maskAt(99), nullptr);
    delete s;
}

void TestLayerMaskIntegration::commandUndoRestoresBefore() {
    LayerStack* s = makeStack();
    QUndoStack undo;
    cv::Mat beforeMask;
    cv::Mat afterMask(20, 20, CV_8UC1, cv::Scalar(255));

    // Apply mask first so the layer has something to "undo" to.
    s->addPixelMask(0, cv::Mat());
    beforeMask = s->maskAt(0)->pixel;  // empty before

    auto* cmd = new LayerMaskCommand(s, 0,
                                     *s->maskAt(0),  // before = empty
                                     LayerMask(),    // after = empty (no-op for this test)
                                     QStringLiteral("test"));
    (void)afterMask;
    undo.push(cmd);

    // Push a real change: addPixelMask with a non-empty mask, wrapped in a
    // command that captures the previous empty state.
    LayerMask prev = *s->maskAt(0);
    s->addPixelMask(0, cv::Mat(20, 20, CV_8UC1, cv::Scalar(255)));
    LayerMask next = *s->maskAt(0);
    auto* cmd2 = new LayerMaskCommand(s, 0, prev, next,
                                      QStringLiteral("add mask"));
    undo.push(cmd2);

    QCOMPARE(s->maskAt(0)->kind, LayerMask::Pixel);
    undo.undo();
    QCOMPARE(s->maskAt(0)->kind, LayerMask::None);
    undo.redo();
    QCOMPARE(s->maskAt(0)->kind, LayerMask::Pixel);
    delete s;
}

void TestLayerMaskIntegration::commandRedoAppliesAfter() {
    LayerStack* s = makeStack();
    QUndoStack undo;
    LayerMask prev = *s->maskAt(0);
    s->setMaskInvert(0, true);
    LayerMask next = *s->maskAt(0);
    auto* cmd = new LayerMaskCommand(s, 0, prev, next,
                                     QStringLiteral("invert"));
    undo.push(cmd);
    // After push+redo: invert should be true.
    QCOMPARE(s->maskAt(0)->invert, true);
    undo.undo();
    QCOMPARE(s->maskAt(0)->invert, false);
    undo.redo();
    QCOMPARE(s->maskAt(0)->invert, true);
    delete s;
}

QTEST_MAIN(TestLayerMaskIntegration)
#include "tst_LayerMaskIntegration.moc"
