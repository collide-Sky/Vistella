// SPDX-License-Identifier: MIT
//
// tst_M2_1_Project
//
// The M2-1 tests: the .vst project format and the auto-save.
//
//   The .vst format has to keep the WHOLE layer stack -- every layer,
//   its pixels, its mask, its text, its vector paths, its adjustment
//   parameters and its blend / opacity / visibility state. A save that
//   keeps only the pixels is a flat picture with a different extension,
//   and that is exactly what these tests are against.
//
//   The auto-save has to write a recovery copy that is NEWER than the
//   saved file (that is what makes it a recovery), and the recovery
//   copy has to be removed once the document is saved properly.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

#include <opencv2/core.hpp>

#include "../src/media/imageworker/layers/LayerStack.h"
#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerMask.h"
#include "../src/media/project/VstProject.h"
#include "../src/media/project/AutoSave.h"

using namespace layers;
using namespace project;

namespace {

// A ramp image: every pixel is different from its neighbours.
//
//   A SOLID colour would hide a channel mix-up or an offset bug --
//   the read-back would look the same either way. A ramp makes the
//   read-back exact.
cv::Mat makeRamp(int w, int h, int seed = 0)
{
    cv::Mat m(h, w, CV_8UC4);
    for (int y = 0; y < h; ++y) {
        uchar* row = m.ptr<uchar>(y);
        for (int x = 0; x < w; ++x) {
            uchar* p = row + x * 4;
            p[0] = static_cast<uchar>((x * 7 + seed) & 0xFF);      // B
            p[1] = static_cast<uchar>((y * 11 + seed) & 0xFF);     // G
            p[2] = static_cast<uchar>((x * 13 + y * 3) & 0xFF);   // R
            p[3] = static_cast<uchar>((x + y) & 0xFF);             // A
        }
    }
    return m;
}

bool matsEqual(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty()) return false;
    if (a.size() != b.size() || a.type() != b.type()) return false;
    return cv::norm(a, b, cv::NORM_INF) == 0.0;
}

} // namespace

class tst_M2_1_Project : public QObject
{
    Q_OBJECT

private slots:
    // ===== the format =====
    void savesAndLoadsAPlainBitmapLayer();
    void savesAndLoadsTheLayerOrder();
    void savesAndLoadsTheLayerMetadata();
    void savesAndLoadsAPixelMask();
    void savesAndLoadsATextLayer();
    void savesAndLoadsAVectorLayer();
    void savesAndLoadsAnAdjustmentLayer();
    void savesAndLoadsTheCanvasSize();

    // A save that keeps only the pixels is a flat picture.
    void theSaveKeepsMoreThanThePixels();

    // Two layers that share a stableId must not share a pixel blob.
    void layersThatShareAStableIdKeepTheirOwnPixels();

    // ===== the failure modes =====
    void rejectsAFileThatIsNotAProject();
    void rejectsATruncatedProject();
    void loadReplacesTheOldLayers();

    // ===== the auto-save =====
    void autoSaveWritesARecoveryCopy();
    void autoSaveDoesNothingWhenTheDocumentIsClean();
    void theRecoveryCopyIsRemovedAfterASave();
    void theRecoveryCopyIsOnlyOfferedWhenItIsNewer();
    void theRecoveryCopyHoldsTheWholeStack();
};

// ===== the format =====

void tst_M2_1_Project::savesAndLoadsAPlainBitmapLayer()
{
    LayerStack src;
    const cv::Mat img = makeRamp(40, 30, 3);
    src.setBaseLayer(img);
    src.setCanvasSize(img.size());

    QByteArray bytes;
    const VstResult r = saveVstToBytes(bytes, &src);
    QVERIFY2(r.ok, qPrintable(r.error));

    LayerStack dst;
    const VstResult r2 = loadVstFromBytes(bytes, &dst);
    QVERIFY2(r2.ok, qPrintable(r2.error));

    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QVERIFY2(matsEqual(l->image, img), "the layer pixels did not survive the round trip");
}

void tst_M2_1_Project::savesAndLoadsTheLayerOrder()
{
    LayerStack src;
    // Three layers, each with a DIFFERENT flat colour, so the order is
    // visible in the read-back and not just in the count.
    for (int i = 0; i < 3; ++i) {
        cv::Mat m(10, 10, CV_8UC4, cv::Scalar(i * 40, i * 20, i * 10, 255));
        src.addLayer(QStringLiteral("layer%1").arg(i), m);
    }

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);

    QCOMPARE(dst.count(), 3);
    for (int i = 0; i < 3; ++i) {
        LayerPtr l = dst.at(i);
        QVERIFY(l != nullptr);
        QCOMPARE(l->name, QStringLiteral("layer%1").arg(i));
        // The colour tells the order apart: a swapped pair would still
        // have the right COUNT.
        QCOMPARE(int(l->image.at<cv::Vec4b>(0, 0)[0]), i * 40);
    }
}

void tst_M2_1_Project::savesAndLoadsTheLayerMetadata()
{
    LayerStack src;
    cv::Mat m(10, 10, CV_8UC4, cv::Scalar(1, 2, 3, 255));
    const int idx = src.addLayer(QStringLiteral("meta"), m);
    src.setVisible(idx, false);
    src.setOpacity(idx, 0.42f);
    src.setLocked(idx, true);
    src.setLinked(idx, true);
    src.setBlend(idx, Layer::Multiply);
    src.rename(idx, QStringLiteral("renamed"));

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QCOMPARE(l->name, QStringLiteral("renamed"));
    QCOMPARE(l->visible, false);
    QVERIFY(qAbs(l->opacity - 0.42f) < 0.001f);
    QCOMPARE(l->locked, true);
    QCOMPARE(l->isLinked, true);
    QCOMPARE(int(l->blend), int(Layer::Multiply));
}

void tst_M2_1_Project::savesAndLoadsAPixelMask()
{
    LayerStack src;
    cv::Mat m(20, 20, CV_8UC4, cv::Scalar(10, 20, 30, 255));
    const int idx = src.addLayer(QStringLiteral("masked"), m);

    // A pixel mask with a real pattern in it.
    cv::Mat mask(20, 20, CV_8UC1);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            mask.at<uchar>(y, x) = static_cast<uchar>((x * 5 + y * 3) & 0xFF);
        }
    }
    QVERIFY(src.addPixelMask(idx, mask));
    // addPixelMask already enables the mask, so setMaskEnabled(true)
    // is a no-op and returns false (it reports a CHANGE). The mask is
    // on either way.
    src.setMaskEnabled(idx, true);
    QVERIFY(src.setMaskDensity(idx, 0.66));
    QVERIFY(src.setMaskFeather(idx, 4.0));
    QVERIFY(src.setMaskInvert(idx, true));

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QCOMPARE(int(l->mask.kind), int(LayerMask::Pixel));
    QCOMPARE(l->mask.enabled, true);
    QVERIFY(qAbs(l->mask.density - 0.66) < 0.001);
    QVERIFY(qAbs(l->mask.feather - 4.0) < 0.001);
    QCOMPARE(l->mask.invert, true);
    QVERIFY2(matsEqual(l->mask.pixel, mask), "the pixel mask did not survive the round trip");
}

void tst_M2_1_Project::savesAndLoadsATextLayer()
{
    LayerStack src;
    const int idx = src.addTextLayer(QStringLiteral("text"), QStringLiteral("Hello Vistella"), 36,
                                     QColor(10, 200, 30), QStringLiteral("Verdana"));
    Q_UNUSED(idx);

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QCOMPARE(int(l->kind), int(Layer::Text));
    QCOMPARE(l->text, QStringLiteral("Hello Vistella"));
    QCOMPARE(l->fontSize, 36);
    QCOMPARE(l->fontFamily, QStringLiteral("Verdana"));
    QCOMPARE(l->textColor.red(), 10);
    QCOMPARE(l->textColor.green(), 200);
    QCOMPARE(l->textColor.blue(), 30);
}

void tst_M2_1_Project::savesAndLoadsAVectorLayer()
{
    LayerStack src;
    QVector<QPainterPath> paths;
    QPainterPath p;
    p.moveTo(10, 10);
    p.lineTo(50, 10);
    p.lineTo(30, 40);
    p.closeSubpath();
    paths.append(p);
    QVector<QColor> fills;
    fills.append(QColor(1, 2, 3));
    const int idx = src.addVectorLayer(QStringLiteral("vector"), paths, fills);
    Q_UNUSED(idx);

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QCOMPARE(int(l->kind), int(Layer::Vector));
    QCOMPARE(l->vectorPaths.size(), 1);
    QCOMPARE(l->vectorFillColors.size(), 1);

    // The path's geometry has to survive, not just its existence.
    const QPainterPath& back = l->vectorPaths.first();
    QVERIFY2(!back.isEmpty(), "the vector path came back empty");
    QCOMPARE(back.boundingRect().left(), 10.0);
    QCOMPARE(back.boundingRect().top(), 10.0);
    QCOMPARE(back.boundingRect().width(), 40.0);
    QCOMPARE(back.boundingRect().height(), 30.0);
}

void tst_M2_1_Project::savesAndLoadsAnAdjustmentLayer()
{
    LayerStack src;
    const int idx = src.addAdjustmentLayer(QStringLiteral("adj"), QStringLiteral("curves"));
    Q_UNUSED(idx);

    // A LUT with a real shape in it.
    cv::Mat lut(256, 1, CV_8UC1);
    for (int i = 0; i < 256; ++i) lut.at<uchar>(i, 0) = static_cast<uchar>(i);
    QVERIFY(src.setAdjustmentLut(idx, lut));

    // And a per-pixel payload (the HSL one).
    QVERIFY(src.setAdjustmentType(idx, QStringLiteral("hueSat")));

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    LayerPtr l = dst.at(0);
    QVERIFY(l != nullptr);
    QCOMPARE(int(l->kind), int(Layer::Adjustment));
    QCOMPARE(l->adjustmentType, QStringLiteral("hueSat"));
    QVERIFY2(matsEqual(l->adjustmentLut, lut), "the adjustment LUT did not survive");
}

void tst_M2_1_Project::savesAndLoadsTheCanvasSize()
{
    LayerStack src;
    const cv::Mat img = makeRamp(17, 23, 5);
    src.setBaseLayer(img);
    // The DOCUMENT's canvas size is set EXPLICITLY, and it differs
    // from the base layer's size -- that is the state a project is in
    // after a crop or a resize.
    src.setCanvasSize(cv::Size(123, 456));

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.documentCanvasSize().width, 123);
    QCOMPARE(dst.documentCanvasSize().height, 456);
}

// Two layers that share a stableId must not share a pixel blob.
void tst_M2_1_Project::layersThatShareAStableIdKeepTheirOwnPixels()
{
    // The blob names used to be built from stableId. stableId comes from
    // a process-global counter and is restored verbatim from the file, so
    // two different layers can end up sharing one id -- and the blob
    // table is keyed by name, so one layer silently overwrote the other.
    //
    //   The collision is forced here: stableId is a public field.
    LayerStack src;
    const cv::Mat first  = makeRamp(24, 18, 11);
    const cv::Mat second = makeRamp(24, 18, 97);
    const int i0 = src.addLayer(QStringLiteral("first"), first);
    const int i1 = src.addLayer(QStringLiteral("second"), second);
    QVERIFY(i0 >= 0);
    QVERIFY(i1 >= 0);
    // Both layers now carry the SAME identity.
    QVERIFY(src.at(i0) != nullptr);
    QVERIFY(src.at(i1) != nullptr);
    src.at(i0)->stableId = 4242;
    src.at(i1)->stableId = 4242;
    QCOMPARE(src.at(i0)->stableId, src.at(i1)->stableId);

    QByteArray bytes;
    const VstResult r = saveVstToBytes(bytes, &src);
    QVERIFY2(r.ok, qPrintable(r.error));

    LayerStack dst;
    const VstResult r2 = loadVstFromBytes(bytes, &dst);
    QVERIFY2(r2.ok, qPrintable(r2.error));
    QCOMPARE(dst.count(), 2);
    LayerPtr l0 = dst.at(0);
    LayerPtr l1 = dst.at(1);
    QVERIFY(l0 != nullptr);
    QVERIFY(l1 != nullptr);
    // Each layer keeps its OWN pixels. With the old naming, one of them
    // would have come back holding the other's pixels -- the counts and
    // the layer count would still have looked right.
    QVERIFY2(matsEqual(l0->image, first),
             "the first layer came back with the wrong pixels");
    QVERIFY2(matsEqual(l1->image, second),
             "the second layer came back with the wrong pixels");
    // The two ramps are genuinely different, so the check above is not
    // vacuous.
    QVERIFY(!matsEqual(first, second));
}

// The whole point of the format: it keeps MORE than the pixels.
void tst_M2_1_Project::theSaveKeepsMoreThanThePixels()
{
    LayerStack src;
    cv::Mat m(20, 20, CV_8UC4, cv::Scalar(9, 9, 9, 255));
    const int bmp = src.addLayer(QStringLiteral("bitmap"), m);
    const int txt = src.addTextLayer(QStringLiteral("text"), QStringLiteral("hello"), 12);
    Q_UNUSED(bmp); Q_UNUSED(txt);
    src.setOpacity(txt, 0.5f);

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    LayerStack dst;
    QVERIFY(loadVstFromBytes(bytes, &dst).ok);

    // Two layers, not one flattened bitmap.
    QCOMPARE(dst.count(), 2);
    // One of them is still a TEXT layer (the text did not get baked
    // into the pixels).
    bool hasText = false;
    for (int i = 0; i < dst.count(); ++i) {
        LayerPtr l = dst.at(i);
        if (l && l->kind == Layer::Text) hasText = true;
    }
    QVERIFY2(hasText, "the text layer was flattened into a bitmap");
}

// ===== the failure modes =====

void tst_M2_1_Project::rejectsAFileThatIsNotAProject()
{
    LayerStack dst;
    const QByteArray junk("this is not a project file at all, not even close");
    const VstResult r = loadVstFromBytes(junk, &dst);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void tst_M2_1_Project::rejectsATruncatedProject()
{
    LayerStack src;
    cv::Mat m(20, 20, CV_8UC4, cv::Scalar(1, 1, 1, 255));
    src.addLayer(QStringLiteral("layer"), m);

    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    // Chop the tail off: the manifest claims more bytes than are there.
    QByteArray truncated = bytes.left(bytes.size() - 40);

    LayerStack dst;
    const VstResult r = loadVstFromBytes(truncated, &dst);
    QVERIFY2(!r.ok, "a truncated project file was accepted");
}

void tst_M2_1_Project::loadReplacesTheOldLayers()
{
    LayerStack dst;
    // The stack already has layers -- a load must NOT merge into them.
    cv::Mat m(10, 10, CV_8UC4, cv::Scalar(1, 1, 1, 255));
    dst.addLayer(QStringLiteral("old1"), m);
    dst.addLayer(QStringLiteral("old2"), m);
    dst.addLayer(QStringLiteral("old3"), m);
    QCOMPARE(dst.count(), 3);

    LayerStack src;
    src.addLayer(QStringLiteral("new"), m);
    QByteArray bytes;
    QVERIFY(saveVstToBytes(bytes, &src).ok);

    QVERIFY(loadVstFromBytes(bytes, &dst).ok);
    QCOMPARE(dst.count(), 1);
    QCOMPARE(dst.at(0)->name, QStringLiteral("new"));
}

// ===== the auto-save =====

void tst_M2_1_Project::autoSaveWritesARecoveryCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AutoSave saver;
    saver.setRecoveryDir(dir.path());

    LayerStack stack;
    cv::Mat m(16, 16, CV_8UC4, cv::Scalar(4, 5, 6, 255));
    stack.addLayer(QStringLiteral("layer"), m);

    QString err;
    QVERIFY2(saver.writeRecovery(QStringLiteral("C:/docs/photo.vst"), &stack, &err),
             qPrintable(err));

    const QString path = saver.recoveryPathFor(QStringLiteral("C:/docs/photo.vst"));
    QVERIFY2(QFileInfo::exists(path), "the recovery copy was not written");
    QVERIFY(QFileInfo(path).size() > 0);
}

void tst_M2_1_Project::autoSaveDoesNothingWhenTheDocumentIsClean()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AutoSave saver;
    saver.setRecoveryDir(dir.path());

    int writes = 0;
    saver.setDirtyChecker([]() { return false; });   // clean
    saver.setRecoveryWriter([&writes](const QString&) { ++writes; return true; });

    saver.tick();
    QCOMPARE(writes, 0);

    // A dirty document DOES get a recovery copy.
    saver.setDirtyChecker([]() { return true; });
    saver.tick();
    QCOMPARE(writes, 1);
}

void tst_M2_1_Project::theRecoveryCopyIsRemovedAfterASave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AutoSave saver;
    saver.setRecoveryDir(dir.path());

    LayerStack stack;
    cv::Mat m(16, 16, CV_8UC4, cv::Scalar(4, 5, 6, 255));
    stack.addLayer(QStringLiteral("layer"), m);

    const QString doc = QStringLiteral("C:/docs/photo.vst");
    QVERIFY(saver.writeRecovery(doc, &stack));
    QVERIFY(QFileInfo::exists(saver.recoveryPathFor(doc)));

    saver.removeRecovery(doc);
    QVERIFY2(!QFileInfo::exists(saver.recoveryPathFor(doc)),
             "the recovery copy outlived the save it was made for");
}

void tst_M2_1_Project::theRecoveryCopyIsOnlyOfferedWhenItIsNewer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AutoSave saver;
    saver.setRecoveryDir(dir.path());

    LayerStack stack;
    cv::Mat m(16, 16, CV_8UC4, cv::Scalar(4, 5, 6, 255));
    stack.addLayer(QStringLiteral("layer"), m);

    // The document's own file, written a while ago.
    const QString docPath = dir.filePath(QStringLiteral("doc.vst"));
    {
        QFile f(docPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("a saved document");
    }
    QFile::setPermissions(docPath, QFile::ReadOwner | QFile::WriteOwner);

    const QString doc = docPath;

    // No recovery copy yet -> nothing to recover.
    QVERIFY(saver.findRecoverable(doc).isEmpty());

    // A recovery copy OLDER than the saved file -> nothing to recover.
    QVERIFY(saver.writeRecovery(doc, &stack));
    QFile::setPermissions(saver.recoveryPathFor(doc),
                         QFile::ReadOwner | QFile::WriteOwner);
    const QDateTime old = QDateTime::currentDateTime().addDays(-1);
    {
        QFile f(saver.recoveryPathFor(doc));
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.setFileTime(old, QFileDevice::FileModificationTime);
    }
    QVERIFY2(saver.findRecoverable(doc).isEmpty(),
             "a stale recovery copy was offered for recovery");

    // A recovery copy NEWER than the saved file -> it is a crash.
    const QDateTime now = QDateTime::currentDateTime();
    {
        QFile f(saver.recoveryPathFor(doc));
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.setFileTime(now, QFileDevice::FileModificationTime);
    }
    QVERIFY(!saver.findRecoverable(doc).isEmpty());
}

void tst_M2_1_Project::theRecoveryCopyHoldsTheWholeStack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AutoSave saver;
    saver.setRecoveryDir(dir.path());

    LayerStack stack;
    cv::Mat m(16, 16, CV_8UC4, cv::Scalar(4, 5, 6, 255));
    stack.addLayer(QStringLiteral("bitmap"), m);
    stack.addTextLayer(QStringLiteral("text"), QStringLiteral("recovered"), 18);
    stack.setOpacity(1, 0.75f);

    const QString doc = QStringLiteral("C:/docs/photo.vst");
    QVERIFY(saver.writeRecovery(doc, &stack));

    // Read the recovery copy back: it must be a real project, not a
    // flat picture.
    LayerStack back;
    const VstResult r = loadVst(saver.recoveryPathFor(doc), &back);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(back.count(), 2);
    bool hasText = false;
    for (int i = 0; i < back.count(); ++i) {
        LayerPtr l = back.at(i);
        if (l && l->kind == Layer::Text) hasText = true;
    }
    QVERIFY2(hasText, "the recovery copy lost the text layer");
}

QTEST_MAIN(tst_M2_1_Project)
#include "tst_M2_1_Project.moc"
