// =============================================================================
//  tst_FileExtensionRegistry — FileExtensionRegistry namespace 静态函数测试
//  纯逻辑测试, 不需要 QApplication
// =============================================================================

#include <QTest>
#include <QString>
#include <QStringList>
#include <QSet>

#include "FileExtensionRegistry.h"

class tst_FileExtensionRegistry : public QObject
{
    Q_OBJECT

private slots:
    // ---- 基础扩展名查表 ----
    void test_moduleIdForExtension_basic();
    void test_moduleIdForExtension_pathStyle();
    void test_moduleIdForExtension_unknown();
    void test_moduleIdForExtension_empty();

    // ---- 大小写不敏感 (内部 normalizeSuffix 走 toLower) ----
    void test_moduleIdForExtension_caseInsensitive();

    // ---- visionWorker 跟 imageWorker 共享扩展名 ----
    void test_visionAndImageShareExts();

    // ---- 4 大模块都存在 ----
    void test_allModuleIds();

    // ---- isSupportedExtension / allSupportedExtensions ----
    void test_isSupportedExtension();
    void test_allSupportedExtensions();

    // ---- extensionsFor 返每个模块的扩展名 ----
    void test_extensionsFor();

private:
    // 模块 id (避免硬编码字符串, 跟 FileExtensionRegistry::ModuleId:: 一致)
    static constexpr const char *kImageWorker  = "imageWorker";
    static constexpr const char *kAudioWorker  = "audioWorker";
    static constexpr const char *kVideoWorker  = "videoWorker";
    static constexpr const char *kVisionWorker = "visionWorker";
};

// =============================================================================
//  实现
// =============================================================================

void tst_FileExtensionRegistry::test_moduleIdForExtension_basic()
{
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".png")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".jpg")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".mp3")), QString::fromLatin1(kAudioWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".mp4")), QString::fromLatin1(kVideoWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".wav")), QString::fromLatin1(kAudioWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".flac")), QString::fromLatin1(kAudioWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".mkv")), QString::fromLatin1(kVideoWorker));
}

void tst_FileExtensionRegistry::test_moduleIdForExtension_pathStyle()
{
    // 完整路径: "D:/a/b/c.png" 也能正确抽出 ".png"
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("D:/a/b/c.png")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("/home/user/photo.jpg")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("C:\\Users\\test\\song.mp3")), QString::fromLatin1(kAudioWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("relative/path/film.mkv")), QString::fromLatin1(kVideoWorker));

    // Linux 风格路径
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("./a/b.flac")), QString::fromLatin1(kAudioWorker));
}

void tst_FileExtensionRegistry::test_moduleIdForExtension_unknown()
{
    // 不在表里的扩展名
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".xyz")), QString());
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".exe")), QString());
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".docx")), QString());
    // 完全没扩展名
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("README")), QString());
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("D:/path/to/file_without_ext")), QString());
}

void tst_FileExtensionRegistry::test_moduleIdForExtension_empty()
{
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QString()), QString());
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("")), QString());
}

void tst_FileExtensionRegistry::test_moduleIdForExtension_caseInsensitive()
{
    // 大写 / 混合大小写都应该识别 (FileExtensionRegistry 内部走 toLower)
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".PNG")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".Jpg")), QString::fromLatin1(kImageWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral(".Mp3")), QString::fromLatin1(kAudioWorker));
    QCOMPARE(FileExtensionRegistry::moduleIdForExtension(QStringLiteral("D:/A/B/IMAGE.JPEG")), QString::fromLatin1(kImageWorker));
}

void tst_FileExtensionRegistry::test_visionAndImageShareExts()
{
    // visionWorker 跟 imageWorker 必须共享扩展名 (阶段 0 决策 5: vision 跑在图像上)
    const QStringList imgExts = FileExtensionRegistry::extensionsFor(QString::fromLatin1(kImageWorker));
    const QStringList visExts = FileExtensionRegistry::extensionsFor(QString::fromLatin1(kVisionWorker));

    QVERIFY(!imgExts.isEmpty());
    QVERIFY(!visExts.isEmpty());
    QCOMPARE(imgExts, visExts);   // 两个 list 必须一致

    // visionWorker 自己不能是空表 (防止有人误改)
    QVERIFY(visExts.contains(QStringLiteral(".png")));
    QVERIFY(visExts.contains(QStringLiteral(".jpg")));
    QVERIFY(visExts.contains(QStringLiteral(".webp")));
}

void tst_FileExtensionRegistry::test_allModuleIds()
{
    const QStringList ids = FileExtensionRegistry::allModuleIds();
    QCOMPARE(ids.size(), 4);
    QVERIFY(ids.contains(QString::fromLatin1(kImageWorker)));
    QVERIFY(ids.contains(QString::fromLatin1(kAudioWorker)));
    QVERIFY(ids.contains(QString::fromLatin1(kVideoWorker)));
    QVERIFY(ids.contains(QString::fromLatin1(kVisionWorker)));
}

void tst_FileExtensionRegistry::test_isSupportedExtension()
{
    QVERIFY(FileExtensionRegistry::isSupportedExtension(QStringLiteral("image.png")));
    QVERIFY(FileExtensionRegistry::isSupportedExtension(QStringLiteral(".jpg")));
    QVERIFY(FileExtensionRegistry::isSupportedExtension(QStringLiteral("D:/a/b/c.mp4")));
    QVERIFY(FileExtensionRegistry::isSupportedExtension(QStringLiteral(".WEBP")));   // 大写

    QVERIFY(!FileExtensionRegistry::isSupportedExtension(QStringLiteral(".exe")));
    QVERIFY(!FileExtensionRegistry::isSupportedExtension(QStringLiteral("README")));
    QVERIFY(!FileExtensionRegistry::isSupportedExtension(QString()));
}

void tst_FileExtensionRegistry::test_allSupportedExtensions()
{
    const QStringList exts = FileExtensionRegistry::allSupportedExtensions();
    QVERIFY(!exts.isEmpty());
    // 必须有 image / audio / video 各至少一个代表
    QVERIFY(exts.contains(QStringLiteral(".png")));
    QVERIFY(exts.contains(QStringLiteral(".mp3")));
    QVERIFY(exts.contains(QStringLiteral(".mp4")));
    // 必须有序 (内部 sort)
    QVERIFY(std::is_sorted(exts.cbegin(), exts.cend(), [](const QString &a, const QString &b){
        return QString::compare(a, b) < 0;
    }));
    // 不重复 — 用 QSet 测唯一性 (QList::iterator 不支持 std::unique 的赋值语义)
    const QSet<QString> uniqExts(exts.cbegin(), exts.cend());
    QCOMPARE(exts.size(), uniqExts.size());
}

void tst_FileExtensionRegistry::test_extensionsFor()
{
    const QStringList imgExts = FileExtensionRegistry::extensionsFor(QString::fromLatin1(kImageWorker));
    QVERIFY(!imgExts.isEmpty());
    QVERIFY(imgExts.contains(QStringLiteral(".png")));
    QVERIFY(imgExts.contains(QStringLiteral(".jpg")));
    QVERIFY(imgExts.contains(QStringLiteral(".jpeg")));
    QVERIFY(imgExts.contains(QStringLiteral(".bmp")));
    QVERIFY(imgExts.contains(QStringLiteral(".webp")));

    const QStringList audExts = FileExtensionRegistry::extensionsFor(QString::fromLatin1(kAudioWorker));
    QVERIFY(audExts.contains(QStringLiteral(".wav")));
    QVERIFY(audExts.contains(QStringLiteral(".mp3")));
    QVERIFY(audExts.contains(QStringLiteral(".flac")));

    const QStringList vidExts = FileExtensionRegistry::extensionsFor(QString::fromLatin1(kVideoWorker));
    QVERIFY(vidExts.contains(QStringLiteral(".mp4")));
    QVERIFY(vidExts.contains(QStringLiteral(".mkv")));

    // 不存在的模块返空 list
    QVERIFY(FileExtensionRegistry::extensionsFor(QStringLiteral("nonexistentModule")).isEmpty());
}

QTEST_GUILESS_MAIN(tst_FileExtensionRegistry)
#include "tst_FileExtensionRegistry.moc"
