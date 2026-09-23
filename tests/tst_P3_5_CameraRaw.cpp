// SPDX-License-Identifier: MIT
//
// tst_P3_5_CameraRaw - P3.5 (2026-09-23)
//
// 验证:
//   1. CameraRawLoader::hasLibRawSupport 反映编译态 MULTIDOC_HAVE_LIBRAW
//   2. supportedRawExtensions 包含 PS Camera Raw 主用扩展名 (CR2/NEF/ARW/DNG/...)
//   3. decodeRaw("nonexistent.dng", {}) 在 stub 模式下返 error + 空 image
//   4. decodeRaw 在 stub 模式下永远 error (因为没 libraw 解码) — 准备给真 libraw
//      集成时跑 e2e fixture 测试 (用 fixture/.dng 文件, 详见 docs/P3.5+)
//   5. CameraRawDialog 创建 + 5 slider + 4 button + 状态 banner
//
// FileExtensionRegistry 已注册 12 个 RAW 扩展名到 imageWorker,
// FileExtensionRegistryTests 覆盖 (现有 tst_FileExtensionRegistry 测试).
//
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QLabel>
#include <QSlider>
#include <QPushButton>

#include "../src/media/imagewindow/camera_raw/CameraRawLoader.h"
#include "../src/media/imagewindow/camera_raw/CameraRawDialog.h"
#include "../src/core/FileExtensionRegistry.h"

using namespace camera_raw;

class tst_P3_5_CameraRaw : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_hasLibRawSupport_reflectsCompileState();
    void test_supportedRawExtensions_coversMainFormats();
    void test_decodeRaw_stubReturnsError();
    void test_decodeRaw_emptyPathReturnsError();
    void test_CameraRawDialog_5sliders_4buttons_statusBanner();
    void test_CameraRawDialog_applySignalsEmitted();
    void test_FileExtensionRegistry_includesRawFormats();
};

void tst_P3_5_CameraRaw::initTestCase() {}
void tst_P3_5_CameraRaw::cleanupTestCase() {}

void tst_P3_5_CameraRaw::test_hasLibRawSupport_reflectsCompileState()
{
    // 真链接 libraw 时 hasLibRawSupport() == true; stub 时 == false.
    // 本机 dev 环境没装 libraw → 期望 false. 真 libraw 集成后此测试需要更新
    // (或保持 — stub 是默认编译环境).
#ifdef MULTIDOC_HAVE_LIBRAW
    QVERIFY(hasLibRawSupport());
#else
    QVERIFY(!hasLibRawSupport());
#endif
}

void tst_P3_5_CameraRaw::test_supportedRawExtensions_coversMainFormats()
{
    QStringList exts = supportedRawExtensions();
    QVERIFY(!exts.isEmpty());
    // PS Camera Raw 主用 12 格式 — 全部必须列出
    const QStringList must = {
        ".cr2", ".cr3", ".nef", ".arw", ".dng", ".raf",
        ".orf", ".rw2", ".pef", ".srw", ".x3f", ".nrw",
    };
    for (const auto &m : must) {
        QVERIFY2(exts.contains(m), qPrintable(QString("missing %1").arg(m)));
    }
}

void tst_P3_5_CameraRaw::test_decodeRaw_stubReturnsError()
{
    // Stub 模式: decodeRaw 永远返 error (not ok). 真 libraw 模式才会解码.
    CameraRawSettings s;
    auto r = decodeRaw(QStringLiteral("/nonexistent/test.dng"), s);
    QVERIFY(!r.ok());
    QVERIFY(r.image.empty());
    QVERIFY(!r.errorMsg.isEmpty());
}

void tst_P3_5_CameraRaw::test_decodeRaw_emptyPathReturnsError()
{
    // 空路径应该返 error (不 crash)
    auto r = decodeRaw(QString(), CameraRawSettings{});
    QVERIFY(!r.ok());
    QVERIFY(r.image.empty());
}

void tst_P3_5_CameraRaw::test_CameraRawDialog_5sliders_4buttons_statusBanner()
{
    CameraRawDialog dlg;
    auto sliders = dlg.findChildren<QSlider*>();
    auto buttons = dlg.findChildren<QPushButton*>();
    auto labels  = dlg.findChildren<QLabel*>();
    QCOMPARE(sliders.size(), 5);   // exposure/wbTemp/wbTint/sharpness/noiseRedux
    QCOMPARE(buttons.size(), 4);   // Reset/Apply/OK/Cancel
    QVERIFY(labels.size() >= 1);   // status banner
    // 默认值 (PS 50/50)
    QCOMPARE(dlg.settings().exposure, 50);
    QCOMPARE(dlg.settings().wbTemp, 50);
    QCOMPARE(dlg.settings().wbTint, 50);
    QCOMPARE(dlg.settings().sharpness, 50);
    QCOMPARE(dlg.settings().noiseRedux, 50);
}

void tst_P3_5_CameraRaw::test_CameraRawDialog_applySignalsEmitted()
{
    CameraRawDialog dlg;
    QSignalSpy spy(&dlg, &CameraRawDialog::applyRequested);
    QVERIFY(spy.isValid());
    // 找 Apply 按钮 → click → emit applyRequested
    auto applyBtn = dlg.findChild<QPushButton*>(QStringLiteral("Preview"));
    // 没设 objectName, 用 text 匹配
    auto buttons = dlg.findChildren<QPushButton*>();
    QPushButton *target = nullptr;
    for (auto *b : buttons) {
        if (b->text() == QStringLiteral("预览")) { target = b; break; }
    }
    QVERIFY2(target != nullptr, "Apply button not found");
    target->click();
    QCOMPARE(spy.count(), 1);
}

void tst_P3_5_CameraRaw::test_FileExtensionRegistry_includesRawFormats()
{
    using namespace FileExtensionRegistry;
    QStringList rawExts = {".cr2", ".cr3", ".nef", ".arw", ".dng", ".raf"};
    for (const auto &ext : rawExts) {
        const QString mid = moduleIdForExtension(ext);
        QVERIFY2(mid == QStringLiteral("imageWorker"),
                 qPrintable(QString("extension %1 mapped to %2, expected imageWorker")
                            .arg(ext).arg(mid)));
    }
}

QTEST_MAIN(tst_P3_5_CameraRaw)
#include "tst_P3_5_CameraRaw.moc"