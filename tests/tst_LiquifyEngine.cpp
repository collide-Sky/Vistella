// LiquifyBackingStore + LiquifyEngine unit tests.
//
// Coverage:
//  1.  bilinearSample at integer coords returns that pixel.
//  2.  bilinearSample at half-pixel blends 4 neighbors.
//  3.  bilinearSample clamps out-of-bounds coordinates to the edge.
//  4.  BackingStore::setSnapshot initializes mesh + freeze mask + clears dirty.
//  5.  BackingStore::rebuildMesh changes resolution.
//  6.  BackingStore::isFrozen reads from freeze mask.
//  7.  BackingStore::markDirty accumulates and clamps to image bounds.
//  8.  BackingStore::resetMesh clears mesh + dirty but keeps snapshot/mask.
//  9.  Engine::renderFull returns image of correct size and format.
// 10.  Engine::renderFull with zero mesh == snapshot (per-pixel copy).
// 11.  Engine::renderFull with mesh displacement warps pixels correctly.
// 12.  Engine::renderFull respects freeze mask (frozen pixels unchanged).
// 13.  Engine::renderDirty returns null when there is no dirty region.
// 14.  Engine::renderDirty returns the dirty region (not full image).
// 15.  Engine::renderDirty grows dirty rect by max displacement padding.
// 16.  Engine::samplePixel returns snapshot pixel for frozen points.
// 17.  Engine::samplePixel returns warped pixel for non-frozen points.
// 18.  Engine::applyToolStamp modifies mesh and marks dirty.

#include <QtTest>
#include <QtMath>

#include "filters/liquify/LiquifyBackingStore.h"
#include "filters/liquify/LiquifyEngine.h"
#include "filters/liquify/LiquifyMesh.h"

using namespace filters::liquify;

class TestLiquifyEngine : public QObject {
    Q_OBJECT

private:
    // Build a 4x4 ARGB image with distinct pixel values at each coordinate
    // so we can verify warping precisely. Pixel (x, y) = (R=x, G=y, B=x+y, A=255).
    static QImage makeCheckerImage(int w, int h) {
        QImage img(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                img.setPixelColor(x, y,
                                  QColor((x * 7) & 0xff, (y * 11) & 0xff,
                                         ((x + y) * 13) & 0xff, 255));
            }
        }
        return img;
    }

    static bool approxColor(const QColor& a, const QColor& b, int tol = 2) {
        return std::abs(a.red()   - b.red())   <= tol
            && std::abs(a.green() - b.green()) <= tol
            && std::abs(a.blue()  - b.blue())  <= tol
            && std::abs(a.alpha() - b.alpha()) <= tol;
    }

private slots:
    void bilinearAtInteger();
    void bilinearAtHalf();
    void bilinearClampsToEdge();
    void backingStoreSetSnapshot();
    void backingStoreRebuildMesh();
    void backingStoreIsFrozen();
    void backingStoreMarkDirty();
    void backingStoreResetMesh();
    void renderFullSize();
    void renderFullIdentity();
    void renderFullWarp();
    void renderFullFreeze();
    void renderDirtyEmpty();
    void renderDirtyReturnsRegion();
    void renderDirtyPadding();
    void samplePixelFrozen();
    void samplePixelWarped();
    void applyToolStampUpdatesStore();
};

void TestLiquifyEngine::bilinearAtInteger() {
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(0,   0,   0,   255));
    img.setPixelColor(1, 0, QColor(255, 0,   0,   255));
    img.setPixelColor(0, 1, QColor(0,   255, 0,   255));
    img.setPixelColor(1, 1, QColor(0,   0,   255, 255));

    QCOMPARE(LiquifyEngine::bilinearSample(img, 0.0, 0.0), QColor(0, 0, 0, 255));
    QCOMPARE(LiquifyEngine::bilinearSample(img, 1.0, 0.0), QColor(255, 0, 0, 255));
    QCOMPARE(LiquifyEngine::bilinearSample(img, 0.0, 1.0), QColor(0, 255, 0, 255));
    QCOMPARE(LiquifyEngine::bilinearSample(img, 1.0, 1.0), QColor(0, 0, 255, 255));
}

void TestLiquifyEngine::bilinearAtHalf() {
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(0,   0,   0,   255));
    img.setPixelColor(1, 0, QColor(100, 0,   0,   255));
    img.setPixelColor(0, 1, QColor(0,   100, 0,   255));
    img.setPixelColor(1, 1, QColor(0,   0,   100, 255));

    const QColor mid = LiquifyEngine::bilinearSample(img, 0.5, 0.5);
    // Each channel bilinear-blends: only one of the four corners holds the
    // 100 value, so each channel midpoint is 100 * 0.5 * 0.5 = 25.
    QVERIFY(approxColor(mid, QColor(25, 25, 25, 255), 1));
}

void TestLiquifyEngine::bilinearClampsToEdge() {
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(10, 20, 30, 255));
    img.setPixelColor(1, 0, QColor(40, 50, 60, 255));
    img.setPixelColor(0, 1, QColor(70, 80, 90, 255));
    img.setPixelColor(1, 1, QColor(100, 110, 120, 255));

    QCOMPARE(LiquifyEngine::bilinearSample(img, -5.0, -5.0), QColor(10, 20, 30, 255));
    QCOMPARE(LiquifyEngine::bilinearSample(img, 100.0, 100.0),
             QColor(100, 110, 120, 255));
}

void TestLiquifyEngine::backingStoreSetSnapshot() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    QCOMPARE(store.size(), QSize(50, 50));
    QCOMPARE(store.bounds(), QRect(0, 0, 50, 50));
    QVERIFY(!store.freezeMask().isNull());
    QVERIFY(store.freezeMask().rect() == QRect(0, 0, 50, 50));
    QCOMPARE(store.mesh().rows(), 5);
    QCOMPARE(store.mesh().cols(), 5);
    QVERIFY(!store.hasDirty());
}

void TestLiquifyEngine::backingStoreRebuildMesh() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(100, 100));
    store.rebuildMesh(8);
    QCOMPARE(store.mesh().rows(), 8);
    QCOMPARE(store.mesh().cols(), 8);
}

void TestLiquifyEngine::backingStoreIsFrozen() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(20, 20));
    QVERIFY(!store.isFrozen(5, 5));
    // setPixelColor uses the format's native interpretation; for grayscale
    // a white pixel maps to gray=255 regardless of int packing quirks.
    store.freezeMask().setPixelColor(5, 5, QColor(255, 255, 255));
    QVERIFY(store.isFrozen(5, 5));
    QVERIFY(!store.isFrozen(6, 6));
    // Out-of-bounds is treated as not frozen.
    QVERIFY(!store.isFrozen(-1, 5));
    QVERIFY(!store.isFrozen(5, 100));
}

void TestLiquifyEngine::backingStoreMarkDirty() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(20, 20));
    store.markDirty(QRect(0, 0, 5, 5));
    QCOMPARE(store.dirtyRect(), QRect(0, 0, 5, 5));
    store.markDirty(QRect(3, 3, 5, 5));
    QCOMPARE(store.dirtyRect(), QRect(0, 0, 8, 8));
    // Out-of-bounds clamps.
    store.markDirty(QRect(15, 15, 100, 100));
    QCOMPARE(store.dirtyRect(), QRect(0, 0, 20, 20));
    store.clearDirty();
    QVERIFY(!store.hasDirty());
}

void TestLiquifyEngine::backingStoreResetMesh() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(20, 20));
    store.mesh().setVertexDisplacement(1, 1, QPointF(5, 0));
    store.markDirty(QRect(0, 0, 4, 4));
    QVERIFY(store.hasDirty());

    store.resetMesh();
    QVERIFY(approxColor(QColor(store.mesh().vertexDisplacement(1, 1).x(),
                                store.mesh().vertexDisplacement(1, 1).y(), 0, 255),
                          QColor(0, 0, 0, 255), 0));
    QVERIFY(!store.hasDirty());
}

void TestLiquifyEngine::renderFullSize() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(40, 30));
    LiquifyEngine eng;
    eng.setStore(&store);
    const QImage out = eng.renderFull();
    QCOMPARE(out.size(), QSize(40, 30));
    QCOMPARE(out.format(), QImage::Format_ARGB32);
}

void TestLiquifyEngine::renderFullIdentity() {
    // Zero mesh displacement: renderFull should equal the snapshot.
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(20, 20));
    LiquifyEngine eng;
    eng.setStore(&store);
    const QImage out = eng.renderFull();
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            QVERIFY(approxColor(out.pixelColor(x, y),
                                store.snapshot().pixelColor(x, y)));
        }
    }
}

void TestLiquifyEngine::renderFullWarp() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    LiquifyEngine eng;
    eng.setStore(&store);
    // Set a uniform +5 x displacement on every vertex so the inverse warp
    // is a precise (10, 25) -> (5, 25) shift at every pixel.
    for (int i = 0; i < store.mesh().rows(); ++i) {
        for (int j = 0; j < store.mesh().cols(); ++j) {
            store.mesh().setVertexDisplacement(i, j, QPointF(5, 0));
        }
    }
    const QImage out = eng.renderFull();
    const QColor expected = store.snapshot().pixelColor(5, 25);
    QVERIFY(approxColor(out.pixelColor(10, 25), expected, 2));
}

void TestLiquifyEngine::renderFullFreeze() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    LiquifyEngine eng;
    eng.setStore(&store);
    // Set a uniform displacement everywhere.
    for (int i = 0; i < store.mesh().rows(); ++i) {
        for (int j = 0; j < store.mesh().cols(); ++j) {
            store.mesh().setVertexDisplacement(i, j, QPointF(5, 0));
        }
    }
    // Freeze the central 10x10 area.
    for (int y = 20; y < 30; ++y) {
        for (int x = 20; x < 30; ++x) {
            store.freezeMask().setPixelColor(x, y, QColor(255, 255, 255));
        }
    }
    const QImage out = eng.renderFull();
    for (int y = 20; y < 30; ++y) {
        for (int x = 20; x < 30; ++x) {
            QVERIFY(approxColor(out.pixelColor(x, y),
                                store.snapshot().pixelColor(x, y)));
        }
    }
}

void TestLiquifyEngine::renderDirtyEmpty() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(20, 20));
    LiquifyEngine eng;
    eng.setStore(&store);
    QRect r;
    const QImage out = eng.renderDirty(r);
    QVERIFY(out.isNull());
    QVERIFY(r.isEmpty());
}

void TestLiquifyEngine::renderDirtyReturnsRegion() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    LiquifyEngine eng;
    eng.setStore(&store);
    eng.applyToolStamp(QPointF(25, 25), 10.0, 1.0,
                       QPointF(3, 0), LiquifyToolMode::ForwardWarp);
    QRect r;
    const QImage out = eng.renderDirty(r);
    QVERIFY(!out.isNull());
    QVERIFY(!r.isEmpty());
    // Returned image should be smaller than the full snapshot.
    QVERIFY(out.width() < 50);
    QVERIFY(out.height() < 50);
    // Returned image size == dirty rect size (modulo padding handled internally).
    QCOMPARE(out.size(), r.size());
}

void TestLiquifyEngine::renderDirtyPadding() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(100, 100));
    LiquifyEngine eng;
    eng.setStore(&store);
    // Apply a stroke that creates a large displacement.
    eng.applyToolStamp(QPointF(50, 50), 20.0, 1.0,
                       QPointF(20, 0), LiquifyToolMode::ForwardWarp);
    QRect r;
    const QImage out = eng.renderDirty(r);
    QVERIFY(!out.isNull());
    // The returned rect should grow beyond the raw brush radius (20 px)
    // to include the displacement padding.
    const int raw = 40;  // 2 * radius
    QVERIFY(r.width()  > raw);
    QVERIFY(r.height() > raw);
}

void TestLiquifyEngine::samplePixelFrozen() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    store.freezeMask().setPixel(10, 10, 255);
    LiquifyEngine eng;
    eng.setStore(&store);
    const QColor c = eng.samplePixel(10, 10);
    QCOMPARE(c, store.snapshot().pixelColor(10, 10));
}

void TestLiquifyEngine::samplePixelWarped() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    for (int i = 0; i < store.mesh().rows(); ++i) {
        for (int j = 0; j < store.mesh().cols(); ++j) {
            store.mesh().setVertexDisplacement(i, j, QPointF(5, 0));
        }
    }
    LiquifyEngine eng;
    eng.setStore(&store);
    // Output (10, 25) samples from (5, 25) under the warp.
    const QColor c = eng.samplePixel(10, 25);
    QCOMPARE(c, store.snapshot().pixelColor(5, 25));
}

void TestLiquifyEngine::applyToolStampUpdatesStore() {
    LiquifyBackingStore store;
    store.setSnapshot(makeCheckerImage(50, 50));
    LiquifyEngine eng;
    eng.setStore(&store);
    QVERIFY(!store.hasDirty());

    eng.applyToolStamp(QPointF(25, 25), 10.0, 1.0,
                       QPointF(2, 0), LiquifyToolMode::ForwardWarp);
    QVERIFY(store.hasDirty());
    QVERIFY(!approxColor(QColor(store.mesh().vertexDisplacement(2, 2).x(),
                                store.mesh().vertexDisplacement(2, 2).y(), 0, 255),
                          QColor(0, 0, 0, 255), 1));
}

QTEST_MAIN(TestLiquifyEngine)
#include "tst_LiquifyEngine.moc"
