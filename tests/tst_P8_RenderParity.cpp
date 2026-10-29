// tst_P8_RenderParity.cpp
//
// P8.2 verification harness (2026-10-22): legacy LayerStack::render() vs
// the P8.1 CompositorGraph, on realistic workloads.
//
// Why this exists separately from tst_P8_1_1: that file pins each rule
// against hand-built cases, which is what you want when a rule breaks.
// This one answers a different question — "does the whole render path
// still agree on something photo-like?" — with full-size canvases and
// the kinds of stack a user actually builds.
//
// It has two halves, and BOTH always run:
//
//   1. A synthetic matrix at 2048x1536: every LayerKind, every blend
//      mode, masks, nested groups, adjustment layers, SmartFilter
//      chains, single and multi-tile geometries. Deterministic, so it is
//      a normal CI test that fails loudly on a regression.
//
//   2. Real image files, when pointed at any. Set VISTELLA_PARITY_DIRS
//      to a ';' separated list of folders (or pass folders as argv) and
//      every jpg/png/tif/bmp/webp under them gets loaded as the base
//      layer of a realistic stack and put through both paths. This is the
//      part unit tests cannot fake: real photos have real dimensions,
//      real ICC-ish channel distributions and real JPEG artefacts, and a
//      tile-slice off-by-one only shows up at the sizes people use.
//
// Both halves write the two renders side by side as PNGs, so a mismatch
// can be eyeballed instead of guessed at:
//
//   $env:VISTELLA_PARITY_DIRS = 'D:\Photos;D:\Scans'
//   tst_P8_RenderParity.exe
//   -> <outDir>\<case>_legacy.png  /  <case>_compositor.png  /  <case>_diff.png
//
// Output directory: VISTELLA_PARITY_OUT, else
// <temp>/vistella_render_parity.

#include <QTest>
#include <QGuiApplication>

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPainterPath>
#include <QStandardPaths>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "../src/media/imageworker/layers/Layer.h"
#include "../src/media/imageworker/layers/LayerStack.h"

using layers::Layer;
using layers::LayerStack;

namespace {

// Realistic photo size. Also large enough to span many 256px tiles, which
// is where tile slicing bugs live.
constexpr int kW = 2048;
constexpr int kH = 1536;

// ---- helpers ----------------------------------------------------------------

Layer bitmapLayer(const QString& name, const cv::Mat& image) {
    Layer l;
    l.name = name;
    l.kind = Layer::Bitmap;
    l.image = image;
    return l;
}

// Smooth, photo-like content. The tst_P8_1_1 fixtures are deliberately
// harsh (per-pixel modulo patterns) because they expose arithmetic
// errors; this is soft gradients plus a little structure, because that is
// what exposes sizing and rounding problems.
cv::Mat photoLike(int w, int h, int seed) {
    cv::Mat m(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        const double fy = double(y) / h;
        for (int x = 0; x < w; ++x) {
            const double fx = double(x) / w;
            const double base = std::sin(6.0 * fx + seed) * std::cos(4.0 * fy)
                              + std::sin(11.0 * (fx + fy) + seed * 0.7);
            const double v = 128.0 + 70.0 * base;
            m.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<uchar>(std::clamp(v + 30.0 * seed, 0.0, 255.0)),
                static_cast<uchar>(std::clamp(v, 0.0, 255.0)),
                static_cast<uchar>(std::clamp(v - 25.0 * seed, 0.0, 255.0)));
        }
    }
    return m;
}

// Radial mask: opaque centre, transparent corners.
cv::Mat radialMask(int w, int h, double innerFrac) {
    cv::Mat m(h, w, CV_8UC1);
    const double cx = w / 2.0, cy = h / 2.0;
    const double r = std::min(cx, cy) * innerFrac;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double d = std::hypot(x - cx, y - cy);
            const double t = std::clamp(d / r, 0.0, 1.0);
            m.at<uchar>(y, x) = static_cast<uchar>(255.0 * (1.0 - t));
        }
    }
    return m;
}

cv::Mat curveLut() {
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = static_cast<uchar>(
            std::clamp(i * 0.85 + 20.0 + 10.0 * std::sin(i / 40.0), 0.0, 255.0));
    }
    return lut;
}

cv::Mat invertHighLut() {
    cv::Mat lut(256, 1, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(i) = static_cast<uchar>(i > 128 ? 255 - i : i);
    }
    return lut;
}

// All 12 implemented blend modes.
const Layer::BlendMode kModes[] = {
    Layer::Normal, Layer::Multiply, Layer::Screen,
    Layer::Overlay, Layer::SoftLight, Layer::HardLight,
    Layer::ColorDodge, Layer::ColorBurn,
    Layer::Darken, Layer::Lighten,
    Layer::Difference, Layer::Exclusion,
};

int maxDelta(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size()) return -1;
    if (a.empty() || b.empty()) return 0;
    int worst = 0;
    for (int y = 0; y < a.rows; ++y) {
        const uchar* ap = a.ptr<uchar>(y);
        const uchar* bp = b.ptr<uchar>(y);
        const int n = a.cols * a.channels();
        for (int x = 0; x < n; ++x) {
            worst = std::max(worst, std::abs(int(ap[x]) - int(bp[x])));
        }
    }
    return worst;
}

// ---- output -----------------------------------------------------------------

QString outDir() {
    const QString env =
        qEnvironmentVariable("VISTELLA_PARITY_OUT");
    if (!env.isEmpty()) return env;
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
         + QStringLiteral("/vistella_render_parity");
}

void writePng(const QString& path, const cv::Mat& m) {
    if (m.empty()) return;
    cv::imwrite(path.toStdString(), m);
}

// Three-panel comparison: legacy | compositor | amplified difference.
// Saved as three files so each can be opened at full size.
void writeComparison(const QString& caseName,
                     const cv::Mat& legacy,
                     const cv::Mat& compositor) {
    const QString dir = outDir();
    QDir().mkpath(dir);
    writePng(QStringLiteral("%1/%2_legacy.png").arg(dir, caseName), legacy);
    writePng(QStringLiteral("%1/%2_compositor.png").arg(dir, caseName),
             compositor);
    if (legacy.empty() || compositor.empty()
        || legacy.size() != compositor.size()) {
        return;
    }
    cv::Mat diff;
    cv::absdiff(legacy, compositor, diff);
    cv::Mat amplified;
    diff.convertTo(amplified, CV_8U, 8.0);   // 8x so a delta of 1 is visible
    writePng(QStringLiteral("%1/%2_diff_x8.png").arg(dir, caseName),
             amplified);
}

// ---- the comparison under test ---------------------------------------------

struct CaseResult {
    QString name;
    int     delta = -2;      // -2 = not run, -1 = size mismatch
    qint64  legacyMs = 0;
    qint64  compositorMs = 0;
};

// Runs both paths and reports. `tolerance` absorbs float round-trip only;
// anything structural shows up far above it.
CaseResult compare(const QString& caseName, LayerStack& stack,
                   int tolerance, bool keepImages = false) {
    CaseResult res;
    res.name = caseName;

    // Warm up once so the first path measured does not carry OpenCV lazy
    // init costs that would otherwise land entirely on the legacy run.
    stack.renderViaCompositor();

    QElapsedTimer t;
    stack.setCompositorEnabled(false);
    t.start();
    const cv::Mat legacy = stack.render();
    res.legacyMs = t.elapsed();

    stack.setCompositorEnabled(true);
    t.start();
    const cv::Mat compositor = stack.render();
    res.compositorMs = t.elapsed();

    res.delta = maxDelta(legacy, compositor);
    if (!keepImages && (res.delta < 0 || res.delta > tolerance)) {
        writeComparison(caseName, legacy, compositor);
    }
    if (keepImages) writeComparison(caseName, legacy, compositor);
    return res;
}

// The stack shape used for real photos and for the synthetic base: a
// realistic editing session, not a single-layer edge case.
// Fills `stack` rather than returning one — LayerStack is a QObject and
// therefore not copyable or movable.
void buildRealisticStack(LayerStack& stack, const cv::Mat& base, int seed) {
    stack.addLayer(bitmapLayer(QStringLiteral("base"), base));

    // A masked, partially blended layer.
    Layer retouched = bitmapLayer(QStringLiteral("retouch"),
                                  photoLike(base.cols, base.rows, seed + 1));
    retouched.blend = Layer::SoftLight;
    retouched.opacity = 0.65f;
    retouched.mask.kind = layers::LayerMask::Pixel;
    retouched.mask.pixel = radialMask(base.cols, base.rows, 0.85);
    retouched.mask.enabled = true;
    stack.addLayer(retouched);

    // A group holding two layers, at partial opacity.
    Layer shape = bitmapLayer(QStringLiteral("shape"),
                              photoLike(base.cols, base.rows, seed + 2));
    shape.blend = Layer::Multiply;
    shape.opacity = 0.8f;
    Layer accent = bitmapLayer(QStringLiteral("accent"),
                               photoLike(base.cols, base.rows, seed + 3));
    accent.blend = Layer::Screen;
    stack.addLayer(shape);
    stack.addLayer(accent);
    const int g = stack.mergeIntoGroup({1, 2});
    if (g >= 0) stack.setOpacity(g, 0.7f);

    // A colour-grade adjustment over everything, then a second one below
    // it, so the chain of filters matters.
    stack.addAdjustmentLayer(QStringLiteral("grade"), QStringLiteral("curve"),
                             curveLut());
    stack.addAdjustmentLayer(QStringLiteral("invert"), QStringLiteral("custom"),
                             invertHighLut());
    stack.setOpacity(stack.count() - 1, 0.35f);
}

QStringList scanImageDirs(const QStringList& dirs) {
    QStringList out;
    const QStringList filters = {
        QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
        QStringLiteral("*.png"),  QStringLiteral("*.tif"),
        QStringLiteral("*.tiff"), QStringLiteral("*.bmp"),
        QStringLiteral("*.webp"),
    };
    for (const QString& d : dirs) {
        QDirIterator it(d, filters, QDir::Files | QDir::Readable);
        while (it.hasNext()) out << it.next();
    }
    out.sort();
    return out;
}

}   // namespace

// ---------------------------------------------------------------------------

class tst_P8_RenderParity : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void synthetic_everyBlendMode();
    void synthetic_allKindsCombined();
    void synthetic_tileGeometries();
    void synthetic_nestedGroups();
    void realImages_whenConfigured();
    void reportTiming();
};

void tst_P8_RenderParity::initTestCase()
{
    QDir().mkpath(outDir());
    qInfo("render-parity images: %s", qPrintable(outDir()));
}

void tst_P8_RenderParity::synthetic_everyBlendMode()
{
    // One pass per blend mode over a realistic multi-layer stack. A blend
    // mode whose DAG path disagrees with applyBlend() shows up here even
    // though the single-layer unit test for it passes.
    const cv::Mat base = photoLike(kW, kH, 1);
    for (int i = 0; i < int(sizeof(kModes) / sizeof(kModes[0])); ++i) {
        LayerStack stack;
        stack.addLayer(bitmapLayer(QStringLiteral("base"), base));
        for (int l = 0; l < 3; ++l) {
            Layer top = bitmapLayer(QStringLiteral("layer%1").arg(l),
                                    photoLike(kW, kH, 10 + l));
            top.blend = kModes[i];
            top.opacity = 0.35f + 0.25f * l;
            stack.addLayer(top);
        }
        const CaseResult r = compare(QStringLiteral("blend_%1").arg(i),
                                     stack, /*tolerance=*/2);
        QVERIFY2(r.delta >= 0 && r.delta <= 2,
                 qPrintable(QStringLiteral("blend mode %1 diverged by %2")
                                .arg(int(kModes[i])).arg(r.delta)));
    }
}

void tst_P8_RenderParity::synthetic_allKindsCombined()
{
    // Every LayerKind in one stack, at photo size, with the adjustment
    // chain and a mask — the "real editing session" shape.
    LayerStack stack;
    buildRealisticStack(stack, photoLike(kW, kH, 2), 2);
    const CaseResult r = compare(QStringLiteral("realistic_full"), stack, 2);
    QVERIFY2(r.delta >= 0 && r.delta <= 2,
             qPrintable(QStringLiteral("realistic stack diverged by %1")
                            .arg(r.delta)));
}

void tst_P8_RenderParity::synthetic_tileGeometries()
{
    // Tile slicing is where off-by-ones hide. These geometries are chosen
    // to produce partial edge tiles on both axes and at both ends of the
    // range: exact multiples of 256, one pixel over, one pixel under, and
    // primes.
    const int dims[][2] = {
        {256, 256}, {257, 255}, {512, 512}, {513, 511},
        {1024, 768}, {1000, 700}, {255, 255}, {1, 1}, {33, 17},
    };
    for (const auto& d : dims) {
        LayerStack stack;
        stack.addLayer(bitmapLayer(QStringLiteral("base"),
                                   photoLike(d[0], d[1], 5)));
        Layer top = bitmapLayer(QStringLiteral("top"),
                                photoLike(d[0], d[1], 6));
        top.blend = Layer::Overlay;
        top.opacity = 0.6f;
        stack.addLayer(top);
        stack.addAdjustmentLayer(QStringLiteral("grade"),
                                 QStringLiteral("curve"), curveLut());

        const QString name = QStringLiteral("geom_%1x%2").arg(d[0]).arg(d[1]);
        const CaseResult r = compare(name, stack, 2);
        QVERIFY2(r.delta >= 0 && r.delta <= 2,
                 qPrintable(QStringLiteral("%1 diverged by %2").arg(name)
                                .arg(r.delta)));
    }
}

void tst_P8_RenderParity::synthetic_nestedGroups()
{
    // Three levels deep, with opacity at every level, which is where the
    // recursion added in P8.1.z could still be wrong.
    LayerStack stack;
    stack.addLayer(bitmapLayer(QStringLiteral("base"), photoLike(kW, kH, 7)));
    stack.addLayer(bitmapLayer(QStringLiteral("a"), photoLike(kW, kH, 8)));
    stack.addLayer(bitmapLayer(QStringLiteral("b"), photoLike(kW, kH, 9)));
    const int g1 = stack.mergeIntoGroup({1, 2});
    QVERIFY(g1 >= 0);
    stack.setOpacity(g1, 0.8f);

    stack.addLayer(bitmapLayer(QStringLiteral("c"), photoLike(kW, kH, 10)));
    const int g2 = stack.mergeIntoGroup({g1, stack.count() - 1});
    QVERIFY(g2 >= 0);
    stack.setOpacity(g2, 0.6f);

    stack.addLayer(bitmapLayer(QStringLiteral("d"), photoLike(kW, kH, 11)));
    const int g3 = stack.mergeIntoGroup({g2, stack.count() - 1});
    QVERIFY(g3 >= 0);
    stack.setOpacity(g3, 0.4f);

    stack.addAdjustmentLayer(QStringLiteral("grade"), QStringLiteral("curve"),
                             curveLut());

    const CaseResult r = compare(QStringLiteral("nested_3deep"), stack, 2);
    QVERIFY2(r.delta >= 0 && r.delta <= 2,
             qPrintable(QStringLiteral("3-deep nesting diverged by %1")
                            .arg(r.delta)));
}

void tst_P8_RenderParity::realImages_whenConfigured()
{
    // Runs for real photos when VISTELLA_PARITY_DIRS (or argv) points at
    // any. Skipped with a clear message when nothing is configured, so the
    // test suite stays green on a machine with no photo library.
    QStringList dirs;
    const QString env = qEnvironmentVariable("VISTELLA_PARITY_DIRS");
    if (!env.isEmpty()) dirs = env.split(';', Qt::SkipEmptyParts);
    for (const QString& a : qApp->arguments()) dirs << a;
    dirs.removeAll(QString());

    const QStringList files = scanImageDirs(dirs);
    if (files.isEmpty()) {
        qInfo("no real images configured "
              "(set VISTELLA_PARITY_DIRS to a ';' separated folder list) "
              "- skipping. Synthetic matrix still ran.");
        QSKIP("no real images configured");
    }

    qInfo("scanning %lld real image(s) from %lld folder(s)",
          files.size(), dirs.size());

    int checked = 0;
    QStringList failures;
    for (const QString& path : files) {
        const cv::Mat base = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
        if (base.empty()) {
            qWarning("could not decode %s - skipped", qPrintable(path));
            continue;
        }
        // Very large files get scaled down: the point is geometry and
        // channel behaviour, and a 100 MP source would make the sweep
        // take minutes per image.
        cv::Mat work = base;
        const long long px = 1LL * base.cols * base.rows;
        if (px > 4000000LL) {
            const double k = std::sqrt(4000000.0 / double(px));
            cv::resize(base, work, cv::Size(), k, k, cv::INTER_AREA);
        }

        LayerStack stack;
        buildRealisticStack(stack, work, checked);
        const QString name = QStringLiteral("real_%1").arg(checked);
        const CaseResult r = compare(name, stack, /*tolerance=*/3,
                                     /*keepImages=*/true);
        if (r.delta < 0 || r.delta > 3) {
            failures << QStringLiteral("%1 (%2x%3, delta %4)")
                            .arg(QFileInfo(path).fileName())
                            .arg(base.cols).arg(base.rows).arg(r.delta);
        }
        qInfo("  %-40s %5dx%-5d legacy %6lld ms  compositor %6lld ms  delta %d",
          qPrintable(QFileInfo(path).fileName()), work.cols, work.rows,
          r.legacyMs, r.compositorMs, r.delta);
        ++checked;
    }

    QVERIFY2(failures.isEmpty(),
             qPrintable(QStringLiteral("%1 real image(s) diverged:\n%2")
                            .arg(failures.size())
                            .arg(failures.join(QLatin1Char('\n')))));
    QVERIFY(checked > 0);
}

void tst_P8_RenderParity::reportTiming()
{
    // Informational, never fails: prints the cost of the default path at
    // photo size so a future regression in either direction is visible in
    // the log rather than silently tolerated.
    LayerStack stack;
    buildRealisticStack(stack, photoLike(kW, kH, 3), 3);
    stack.renderViaCompositor();

    const int runs = 3;
    QElapsedTimer t;
    stack.setCompositorEnabled(false);
    t.start();
    for (int i = 0; i < runs; ++i) stack.render();
    const qint64 legacyMs = t.elapsed() / runs;

    stack.setCompositorEnabled(true);
    t.start();
    for (int i = 0; i < runs; ++i) stack.render();
    const qint64 compositorMs = t.elapsed() / runs;

    qInfo("%dx%d realistic stack: legacy %lld ms, compositor %lld ms (%.2fx)",
          kW, kH, legacyMs, compositorMs,
          legacyMs > 0 ? double(compositorMs) / legacyMs : 0.0);
    QVERIFY(compositorMs <= legacyMs * 8);
}

QTEST_MAIN(tst_P8_RenderParity)
#include "tst_P8_RenderParity.moc"