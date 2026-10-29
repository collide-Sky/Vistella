// tst_P8_4.cpp
//
// Unit test for P8.4 (2026-10-16): 多线程合成 (EngineContext::background fan-out
// + TileCache mutex protection).
//
//   TileCache: 4 threads x 1000 ops each, no races
//   RenderGraph: CompositeStrategy selector + engine==nullptr fallback
//   RenderGraph: execute(engine) parallel produces identical canvas to
//     execute() sequential (pixel-for-pixel)
//   RenderGraph: executeParallel fans tiles across workers (per-tile
//     concurrent submissions actually run; verified via std::atomic
//     counter incremented inside each tile worker)
//
//   P8.4 commit (2026-10-16).

#include <QTest>
#include <QApplication>

#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>   // cv::Vec3b type

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using cv::Vec3b;
using cv::Vec4b;

#include "../src/media/compositor/TileGrid.h"
#include "../src/media/compositor/CompositorNode.h"
#include "../src/media/compositor/TileCache.h"
#include "../src/media/compositor/CompositorGraph.h"
#include "../src/media/compositor/RenderGraph.h"

#include "engine_context.h"        // vistella::tp::EngineContext
#include "executor.hpp"             // vistella::tp::Priority / TaskContext / Task::Fn

using compositor::TileGrid;
using compositor::TileKey;
using compositor::CompositorNode;
using compositor::CompositorGraph;
using compositor::TileCache;
using compositor::RenderGraph;
using compositor::RenderContext;
using compositor::CompositeStrategy;

// Test node: fills its tile with a per-tile (tileX, tileY) dependent color
// so cache misses can be detected.
class TestTileColorNode : public CompositorNode {
public:
    explicit TestTileColorNode(cv::Scalar base = cv::Scalar(50, 100, 200),
                               QString n = QStringLiteral("TileColor"))
        : m_base(base), m_name(n) {}
    // Overload disambiguation: when caller passes only a name (const char*
    // or QString), MSVC can't pick between the two defaulted params above.
    // Use this ctor to force "name-only" construction.
    explicit TestTileColorNode(QString n)
        : m_base(50, 100, 200), m_name(n) {}
    explicit TestTileColorNode(const char* n)
        : m_base(50, 100, 200), m_name(QString::fromLatin1(n)) {}
    cv::Mat compute(int tileX, int tileY,
                    const TileGrid& grid,
                    RenderContext& /*ctx*/) override {
        const cv::Rect r = grid.tileRect(tileX, tileY);
        // The compositing canvas is CV_8UC4 BGRA (pixel contract), so the
        //   tile is produced in that format too -- otherwise the corner
        //   tag below would land in the wrong bytes.
        cv::Mat out(r.height, r.width, CV_8UC4,
                    cv::Scalar(m_base[0], m_base[1], m_base[2], 255));
        // Tag top-left corner with (tileX, tileY) so we can spot-check.
        if (out.rows > 0 && out.cols > 0) {
            out.at<Vec4b>(0, 0) = Vec4b(
                static_cast<uchar>((tileX * 13 + 7) % 256),
                static_cast<uchar>((tileY * 17 + 11) % 256),
                0,
                255);
        }
        return out;
    }
    QString name() const override { return m_name; }
    cv::Scalar m_base;
    QString m_name;
};

class tst_P8_4 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // TileCache mutex (P8.4)
    void tileCache_concurrentPutGet_noRace();
    void tileCache_concurrentInvalidate_noRace();
    void tileCache_concurrentMixedOps_noRace();

    // RenderGraph composite strategy (P8.4)
    void renderGraph_strategyDefaultIsSequential();
    void renderGraph_strategySetter_roundtrip();
    void renderGraph_nullEngine_fallsBackToSequential();
    void renderGraph_parallelOutputEqualsSequential();
    void renderGraph_parallelSubmitsAllTiles();
    void renderGraph_parallelSecondPass_allHits();

    // Per-tile backend dispatch (P8.4.x)
    void renderGraph_noBackendByDefault();
    void renderGraph_withCpuBackend_passThroughPreservesPixels();
    void renderGraph_backendForceCpu_skipsGpuBackend();
    void renderGraph_backendSecondPass_doesNotRedispatch();
    void renderGraph_parallelWithBackend_threadsSafe();
};

void tst_P8_4::initTestCase() {}
void tst_P8_4::cleanupTestCase() {}

// ---------------------------------------------------------------------------
// TileCache concurrency (P8.4: mutable QMutex m_lock on every method)
// ---------------------------------------------------------------------------
void tst_P8_4::tileCache_concurrentPutGet_noRace()
{
    TileCache cache;
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &cache]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                const auto nodeId = static_cast<CompositorNode::NodeId>((t * 7919 + i * 31) % 16 + 1);
                const int tx     = i % 8;
                const int ty     = (i / 8) % 8;
                const int ver    = 1;
                TileKey k{ nodeId, tx, ty, ver };
                cv::Mat v(64, 64, CV_8UC3,
                          cv::Scalar((t * 7 + i) & 0xFF,
                                     (t * 13 + i) & 0xFF,
                                     (t * 19 + i) & 0xFF));
                cache.put(k, v);
                cv::Mat out;
                const bool got = cache.tryGet(k, out);
                if (got) {
                    QCOMPARE(out.size(), v.size());
                    QCOMPARE(out.type(), v.type());
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // Sanity: at most 16 * 8 * 8 = 1024 unique keys; cache should have
    // some (not necessarily all if there were overwrites) entries.
    QVERIFY(cache.size() > 0);
    QVERIFY(cache.size() <= 1024);
}

void tst_P8_4::tileCache_concurrentInvalidate_noRace()
{
    // Pre-populate cache with tiles for nodes 1..4.
    TileCache cache;
    for (int n = 1; n <= 4; ++n) {
        for (int tx = 0; tx < 4; ++tx) {
            for (int ty = 0; ty < 4; ++ty) {
                cache.put({static_cast<CompositorNode::NodeId>(n), tx, ty, 1}, cv::Mat(64, 64, CV_8UC3, cv::Scalar(0)));
            }
        }
    }
    QCOMPARE(cache.size(), 64);

    constexpr int kThreads = 4;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &cache]() {
            // Each thread invalidates a node in a loop, interleaved with
            // a competing put on a different key. Mutex must keep the
            // hash table consistent.
            for (int i = 0; i < 200; ++i) {
                const auto targetNode = static_cast<CompositorNode::NodeId>((t + i) % 4 + 1);
                cache.invalidateNode(targetNode);
                // Repopulate with a fresh key for the SAME node.
                TileKey k{ targetNode, 0, 0, 2 };   // version 2
                cache.put(k, cv::Mat(32, 32, CV_8UC3, cv::Scalar(99)));
            }
        });
    }
    for (auto& th : threads) th.join();

    // After all threads finish, every node 1..4 should still have the
    // v=2 tile present (last put wins; no torn writes).
    for (int n = 1; n <= 4; ++n) {
        cv::Mat out;
        QVERIFY(cache.tryGet({static_cast<CompositorNode::NodeId>(n), 0, 0, 2}, out));
    }
}

void tst_P8_4::tileCache_concurrentMixedOps_noRace()
{
    // Mixed: put, tryGet, invalidateNode, clear, size, cachedNodes.
    // Stresses every API entry point with the mutex in place.
    TileCache cache;
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 1000;

    std::atomic<int> gets{0};
    std::atomic<int> puts{0};
    std::atomic<int> invalidates{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &cache, &gets, &puts, &invalidates]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                const int op = (i + t) % 6;
                const auto nodeId = static_cast<CompositorNode::NodeId>((t * 1009 + i * 7) % 8 + 1);
                TileKey k{ nodeId, i % 4, (i / 4) % 4, 1 };
                switch (op) {
                case 0:
                    cache.put(k, cv::Mat(32, 32, CV_8UC3, cv::Scalar(0)));
                    puts.fetch_add(1);
                    break;
                case 1: {
                    cv::Mat out;
                    (void)cache.tryGet(k, out);
                    gets.fetch_add(1);
                    break;
                }
                case 2:
                    cache.invalidateNode(nodeId);
                    invalidates.fetch_add(1);
                    break;
                case 3:
                    (void)cache.size();
                    break;
                case 4:
                    (void)cache.cachedNodes();
                    break;
                case 5:
                    if ((i % 50) == 0) cache.clear();
                    break;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // No assertion beyond "did not crash". Mutex protects hash table;
    // without it a TSAN run would flag data races.
    QVERIFY(puts.load() > 0);
    QVERIFY(gets.load() > 0);
    QVERIFY(invalidates.load() > 0);
    (void)cache.size();   // final accessor still works
}

// ---------------------------------------------------------------------------
// RenderGraph composite strategy
// ---------------------------------------------------------------------------
void tst_P8_4::renderGraph_strategyDefaultIsSequential()
{
    CompositorGraph g;
    TestTileColorNode fill("fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);
    QCOMPARE(rg.compositeStrategy(), CompositeStrategy::Sequential);
}

void tst_P8_4::renderGraph_strategySetter_roundtrip()
{
    CompositorGraph g;
    TestTileColorNode fill("fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);
    rg.setCompositeStrategy(CompositeStrategy::Parallel);
    QCOMPARE(rg.compositeStrategy(), CompositeStrategy::Parallel);
    rg.setCompositeStrategy(CompositeStrategy::Sequential);
    QCOMPARE(rg.compositeStrategy(), CompositeStrategy::Sequential);
}

void tst_P8_4::renderGraph_nullEngine_fallsBackToSequential()
{
    // engine==nullptr must NOT crash; the implementation routes to
    // executeSequential() instead of dereferencing a null pointer.
    CompositorGraph g;
    TestTileColorNode fill("fill");
    g.addNode(&fill);
    TileGrid grid(512, 512);   // 2x2 = 4 tiles
    RenderGraph rg(&g, fill.id(), &grid);
    rg.setCompositeStrategy(CompositeStrategy::Parallel);
    cv::Mat canvas = rg.execute(static_cast<vistella::tp::EngineContext*>(nullptr));
    QCOMPARE(canvas.size(), cv::Size(512, 512));
    QCOMPARE(rg.lastTilesDone(), 4);
}

void tst_P8_4::renderGraph_parallelOutputEqualsSequential()
{
    // Render the same graph with Sequential, then Parallel (with engine).
    // The two outputs must match pixel-for-pixel.
    auto engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 2));

    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(50, 100, 200), "fill");
    g.addNode(&fill);
    TileGrid grid(512, 512);   // 2x2 = 4 tiles
    RenderGraph rg(&g, fill.id(), &grid);

    rg.setCompositeStrategy(CompositeStrategy::Sequential);
    cv::Mat seq = rg.execute();

    rg.setCompositeStrategy(CompositeStrategy::Parallel);
    cv::Mat par = rg.execute(engine.get());

    QCOMPARE(seq.size(), par.size());
    QCOMPARE(seq.type(), par.type());

    // Pixel-for-pixel
    // The compositing canvas is CV_8UC4 BGRA (pixel contract), so it is
    // read as Vec4b -- a Vec3b read would mix in the next pixel's bytes.
    QCOMPARE(seq.channels(), 4);
    for (int y = 0; y < seq.rows; ++y) {
        for (int x = 0; x < seq.cols; ++x) {
            Vec4b a = seq.at<Vec4b>(y, x);
            Vec4b b = par.at<Vec4b>(y, x);
            QCOMPARE((int)a[0], (int)b[0]);
            QCOMPARE((int)a[1], (int)b[1]);
            QCOMPARE((int)a[2], (int)b[2]);
            QCOMPARE((int)a[3], (int)b[3]);
        }
    }
    QCOMPARE(rg.lastTilesDone(), 4);
}

void tst_P8_4::renderGraph_parallelSubmitsAllTiles()
{
    // Verify Parallel strategy actually submits N tasks (not a single one
    // or zero). We do this by replacing the node with one whose
    // compute() increments an external atomic, then count.
    auto engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 4));

    std::atomic<int> tileComputeCalls{0};
    class CountingNode : public CompositorNode {
    public:
        explicit CountingNode(std::atomic<int>* counter) : m_counter(counter) {}
        cv::Mat compute(int /*tileX*/, int /*tileY*/,
                        const TileGrid& grid,
                        RenderContext& /*ctx*/) override {
            m_counter->fetch_add(1);
            const cv::Rect r = grid.tileRect(0, 0);   // dummy rect
            return cv::Mat(r.height, r.width, CV_8UC3, cv::Scalar(0));
        }
        QString name() const override { return QStringLiteral("Count"); }
        std::atomic<int>* m_counter;
    };

    CompositorGraph g;
    CountingNode count(&tileComputeCalls);
    g.addNode(&count);
    TileGrid grid(1024, 1024);   // 4x4 = 16 tiles
    RenderGraph rg(&g, count.id(), &grid);

    rg.setCompositeStrategy(CompositeStrategy::Parallel);
    cv::Mat canvas = rg.execute(engine.get());
    QCOMPARE(canvas.size(), cv::Size(1024, 1024));
    QCOMPARE(rg.lastTilesDone(), 16);

    // Count runs may exceed 16 if the test harness re-renders or if
    // intermediate caches short-circuit. We assert >= 16 (the render
    // itself produced at least one compute per tile).
    QVERIFY2(tileComputeCalls.load() >= 16,
             "Parallel execute() must invoke the node's compute() at least "
             "once per tile (16 for a 4x4 grid).");
}

void tst_P8_4::renderGraph_parallelSecondPass_allHits()
{
    // Parallel re-render on the same version = cache hit. After the first
    // render, every tile of the output node is in the cache, so the
    // second render must be a no-op (compute() not called).
    auto engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 2));

    std::atomic<int> computeCalls{0};
    class CountingNode : public CompositorNode {
    public:
        explicit CountingNode(std::atomic<int>* c) : m_c(c) {}
        cv::Mat compute(int /*tx*/, int /*ty*/,
                        const TileGrid& grid,
                        RenderContext& /*ctx*/) override {
            m_c->fetch_add(1);
            const cv::Rect r = grid.tileRect(0, 0);
            return cv::Mat(r.height, r.width, CV_8UC3, cv::Scalar(0));
        }
        QString name() const override { return QStringLiteral("Count"); }
        std::atomic<int>* m_c;
    };

    CompositorGraph g;
    CountingNode node(&computeCalls);
    g.addNode(&node);
    TileGrid grid(512, 512);   // 4 tiles
    RenderGraph rg(&g, node.id(), &grid);
    rg.setCompositeStrategy(CompositeStrategy::Parallel);

    (void)rg.execute(engine.get());
    const int after1 = computeCalls.load();
    QVERIFY(after1 >= 4);

    (void)rg.execute(engine.get());
    const int after2 = computeCalls.load();
    // No version change = no recompute; second render must not invoke
    // the node's compute() again.
    QCOMPARE(after2, after1);
}

// ---------------------------------------------------------------------------
// Per-tile backend dispatch (P8.4.x: 2026-10-19)
// ---------------------------------------------------------------------------

#include "../src/media/gpu/BackendRegistry.h"
#include "../src/media/gpu/CpuBackend.h"
#include "../src/media/gpu/RenderBackend.h"

void tst_P8_4::renderGraph_noBackendByDefault()
{
    // No setBackend() call -> dispatch is a no-op, identical to P8.4.
    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(50, 100, 200), "fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);
    QCOMPARE(rg.backend(), nullptr);
    cv::Mat canvas = rg.execute();
    // The compositing canvas is CV_8UC4 BGRA (pixel contract).
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[0], 50);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[1], 100);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[2], 200);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[3], 255);
}

void tst_P8_4::renderGraph_withCpuBackend_passThroughPreservesPixels()
{
    // Wire a CpuBackend. P8.2 declares it as pass-through (input.clone()).
    // A tile rendered via the registry under Auto mode should look
    // identical to the no-backend render.
    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(50, 100, 200), "fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);

    gpu::BackendRegistry reg;   // default-constructed (Auto mode)
    rg.setBackend(&reg);

    cv::Mat canvas = rg.execute();
    // The compositing canvas is CV_8UC4 BGRA (pixel contract).
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[0], 50);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[1], 100);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[2], 200);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[3], 255);
    QCOMPARE(rg.lastTilesDone(), 1);
    QCOMPARE(rg.lastHitCount(), 0);
    QCOMPARE(rg.lastMissCount(), 1);
}

void tst_P8_4::renderGraph_backendForceCpu_skipsGpuBackend()
{
    // With ForceCpu mode, pick() must return the CPU backend even if
    // GpuBackend.isAvailable() reports true.
    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(10, 20, 30), "fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);

    gpu::BackendRegistry reg;
    reg.setMode(gpu::RenderMode::ForceCpu);
    rg.setBackend(&reg);

    cv::Mat canvas = rg.execute();
    // The compositing canvas is CV_8UC4 BGRA (pixel contract).
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[0], 10);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[1], 20);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[2], 30);
    QCOMPARE((int)canvas.at<Vec4b>(128, 128)[3], 255);
}

void tst_P8_4::renderGraph_backendSecondPass_doesNotRedispatch()
{
    // Second render with same version: the post-backend tile is already
    // cached, so backend.processTile() must NOT be called again. We
    // verify this by checking the cache hit / miss counters: the second
    // pass should be all-hits.
    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(0, 0, 0), "fill");
    g.addNode(&fill);
    TileGrid grid(256, 256);
    RenderGraph rg(&g, fill.id(), &grid);
    gpu::BackendRegistry reg;
    rg.setBackend(&reg);

    (void)rg.execute();
    QCOMPARE(rg.lastMissCount(), 1);
    QCOMPARE(rg.lastHitCount(), 0);

    (void)rg.execute();
    // Cache hit on the second pass -> backend.processTile() not invoked
    // again (the cached value is the post-backend result).
    QCOMPARE(rg.lastMissCount(), 0);
    QCOMPARE(rg.lastHitCount(), 1);
}

void tst_P8_4::renderGraph_parallelWithBackend_threadsSafe()
{
    // 4 tiles in parallel + CpuBackend dispatch = race-free. The
    // CpuBackend's processTile() returns input.clone(), which writes
    // to its own cv::Mat. No shared mutable state on the backend.
    auto engine = std::make_unique<vistella::tp::EngineContext>(
        vistella::tp::EngineContext::make_profile(
            vistella::tp::EngineContext::Profile::ImageEditor, 4));

    CompositorGraph g;
    TestTileColorNode fill(cv::Scalar(50, 100, 200), "fill");
    g.addNode(&fill);
    TileGrid grid(512, 512);   // 2x2 = 4 tiles
    RenderGraph rg(&g, fill.id(), &grid);
    rg.setCompositeStrategy(CompositeStrategy::Parallel);

    gpu::BackendRegistry reg;
    rg.setBackend(&reg);

    cv::Mat canvas = rg.execute(engine.get());
    QCOMPARE(canvas.size(), cv::Size(512, 512));
    QCOMPARE(rg.lastTilesDone(), 4);

    // Each tile went through CpuBackend (Auto mode, default shared
    // state = CPU). The output must still be the source color.
    // The compositing canvas is CV_8UC4 BGRA (pixel contract).
    QCOMPARE((int)canvas.at<Vec4b>(100, 100)[0], 50);
    QCOMPARE((int)canvas.at<Vec4b>(400, 400)[0], 50);
}

QTEST_MAIN(tst_P8_4)
#include "tst_P8_4.moc"