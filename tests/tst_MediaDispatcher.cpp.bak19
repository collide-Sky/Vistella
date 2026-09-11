// =============================================================================
//  tst_MediaDispatcher — MediaDispatcher 单例 + 路由 + 模块注册测试
//  需要 QCoreApplication (QObject 信号)
//
//  测试注意:
//   - MediaDispatcher 是全局单例, 所有测试共享状态
//   - 测完要 unregisterAll() 清干净, 避免污染下一个 test
//   - 用一个 TestModule 模拟 IModule, 不依赖真 Worker
// =============================================================================

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QWidget>

#include "MediaDispatcher.h"
#include "IModule.h"
#include "../src/core/FileExtensionRegistry.h"

// ---- 前向声明 (TestModule 的 openFile 内部用, 完整定义在下方) ----
class TestWorkspace;
class TestModuleNoCreate;

// ---- TestModule: 模拟一个 IModule, 用 .testext 扩展名 ----
class TestModule : public IModule
{
public:
    TestModule(const QString &id, const QStringList &exts)
        : m_id(id), m_exts(exts) {}

    ModuleInfo info() const override {
        ModuleInfo mi;
        mi.id = m_id;
        mi.name = m_id + QStringLiteral(" (test)");
        mi.extensions = m_exts;
        return mi;
    }

    // openFile 体内要用 TestWorkspace 完整定义, 体外实现 (下方 TestWorkspace 之后)
    IWorkspace *openFile(const QString &path) override;
    IWorkspace *createNew() override { return nullptr; }   // 返 nullptr, 用于测试不支持 createNew

    QString m_id;
    QStringList m_exts;
};

class TestWorkspace : public IWorkspace
{
public:
    explicit TestWorkspace(QWidget *parent = nullptr) : IWorkspace(parent) {}
    QString filePath()  const override { return m_path; }
    QString moduleId()  const override { return m_mid; }
    QString displayName() const override { return m_path.isEmpty() ? m_mid : QFileInfo(m_path).fileName(); }
    bool    isDirty()   const override { return false; }
    bool    save()      override { return true; }
    bool    saveAs(const QString &) override { return true; }
    bool    loadFile(const QString &p) override { m_path = p; return true; }

    void setFilePath(const QString &p) { m_path = p; }
    void setModuleId(const QString &m) { m_mid = m; }

    QString m_path;
    QString m_mid;
};

// TestModule::openFile 体外实现 — 此时 TestWorkspace 已完整定义
IWorkspace *TestModule::openFile(const QString &path)
{
    auto *w = new TestWorkspace();
    w->setModuleId(m_id);
    w->setFilePath(path);
    return w;
}

// ---- TestModuleNoCreate: 模拟支持 createNew 的模块 ----
class TestModuleNoCreate : public TestModule
{
public:
    TestModuleNoCreate(const QString &id, const QStringList &exts)
        : TestModule(id, exts) {}
    IWorkspace *createNew() override
    {
        // createNew 也要设 moduleId, 跟 openFile 行为一致
        auto *w = new TestWorkspace();
        w->setModuleId(m_id);
        return w;
    }
};

class tst_MediaDispatcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();          // 每个 test 前清状态
    void cleanup();

    // ---- 单例 ----
    void test_singleton();

    // ---- 注册 / 反注册 ----
    void test_registerModule();
    void test_unregisterModule();
    void test_unregisterAll();
    void test_duplicateId();

    // ---- 查询 ----
    void test_moduleForId();
    void test_moduleForFile();
    void test_isExtensionKnown();
    void test_moduleCount();

    // ---- 路由: openFile ----
    void test_openFile_empty();
    void test_openFile_unknownExt();
    void test_openFile_knownButNoModule();
    void test_openFile_knownAndRegistered();
    void test_openFile_singletonAffectedByLastRegistered();

    // ---- 路由: openModuleById ----
    void test_openModuleById_unknown();
    void test_openModuleById_noCreateNew();
    void test_openModuleById_supported();

    // ---- 信号 ----
    void test_modulesChanged_emits();

private:
    TestModule    *m_modA = nullptr;   // .testext → "moduleA"
    TestModule    *m_modB = nullptr;   // .otherext → "moduleB"
    TestModuleNoCreate *m_modC = nullptr;   // .createnext → "moduleC" (支持 createNew)
};

void tst_MediaDispatcher::initTestCase()
{
    m_modA = new TestModule(QStringLiteral("moduleA"), { QStringLiteral(".testext") });
    m_modB = new TestModule(QStringLiteral("moduleB"), { QStringLiteral(".otherext") });
    m_modC = new TestModuleNoCreate(QStringLiteral("moduleC"), { QStringLiteral(".createnext") });
}

void tst_MediaDispatcher::cleanupTestCase()
{
    delete m_modA;
    delete m_modB;
    delete m_modC;
}

void tst_MediaDispatcher::init()
{
    MediaDispatcher::instance().unregisterAll();
}

void tst_MediaDispatcher::cleanup()
{
    MediaDispatcher::instance().unregisterAll();
}

// =============================================================================
//  实现
// =============================================================================

void tst_MediaDispatcher::test_singleton()
{
    auto &a = MediaDispatcher::instance();
    auto &b = MediaDispatcher::instance();
    QCOMPARE(&a, &b);   // 同一指针
}

void tst_MediaDispatcher::test_registerModule()
{
    auto &d = MediaDispatcher::instance();
    QCOMPARE(d.moduleCount(), 0);

    d.registerModule(m_modA);
    QCOMPARE(d.moduleCount(), 1);
    QVERIFY(d.modules().contains(m_modA));

    d.registerModule(m_modB);
    QCOMPARE(d.moduleCount(), 2);

    // 重复注册同一 module 不会重复加
    d.registerModule(m_modA);
    QCOMPARE(d.moduleCount(), 2);

    // nullptr 注册 = no-op
    d.registerModule(nullptr);
    QCOMPARE(d.moduleCount(), 2);
}

void tst_MediaDispatcher::test_unregisterModule()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);
    d.registerModule(m_modB);
    QCOMPARE(d.moduleCount(), 2);

    d.unregisterModule(m_modA);
    QCOMPARE(d.moduleCount(), 1);
    QVERIFY(!d.modules().contains(m_modA));
    QVERIFY(d.modules().contains(m_modB));

    // 不存在的 module = no-op
    d.unregisterModule(nullptr);
    d.unregisterModule(m_modA);   // 已移除
    QCOMPARE(d.moduleCount(), 1);
}

void tst_MediaDispatcher::test_unregisterAll()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);
    d.registerModule(m_modB);
    d.registerModule(m_modC);
    QCOMPARE(d.moduleCount(), 3);

    d.unregisterAll();
    QCOMPARE(d.moduleCount(), 0);
    QVERIFY(d.modules().isEmpty());

    // 空状态再 unregisterAll 不崩
    d.unregisterAll();
    QCOMPARE(d.moduleCount(), 0);
}

void tst_MediaDispatcher::test_duplicateId()
{
    // 阶段 0 决策: 重复 id 后注册覆盖前注册
    auto &d = MediaDispatcher::instance();
    auto *modA2 = new TestModule(QStringLiteral("moduleA"), { QStringLiteral(".testext") });   // 同 id, 不同指针
    d.registerModule(m_modA);
    d.registerModule(modA2);
    QCOMPARE(d.moduleCount(), 1);   // 覆盖, 仍 1 个
    QCOMPARE(d.moduleForId(QStringLiteral("moduleA")), modA2);
    delete modA2;
}

void tst_MediaDispatcher::test_moduleForId()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);
    QCOMPARE(d.moduleForId(QStringLiteral("moduleA")), m_modA);
    QCOMPARE(d.moduleForId(QStringLiteral("nonexistent")), nullptr);
    QCOMPARE(d.moduleForId(QString()), nullptr);
}

void tst_MediaDispatcher::test_moduleForFile()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);
    d.registerModule(m_modB);

    // 已注册模块的扩展名
    QCOMPARE(d.moduleForFile(QStringLiteral("foo.testext")), m_modA);
    QCOMPARE(d.moduleForFile(QStringLiteral("D:/a/b/foo.testext")), m_modA);
    QCOMPARE(d.moduleForFile(QStringLiteral("/path/to/bar.otherext")), m_modB);

    // 大小写不敏感 (走 toLower)
    QCOMPARE(d.moduleForFile(QStringLiteral("FOO.TESTEXT")), m_modA);

    // 未注册的扩展名
    QCOMPARE(d.moduleForFile(QStringLiteral("foo.xyz")), nullptr);

    // 空路径
    QCOMPARE(d.moduleForFile(QString()), nullptr);
    QCOMPARE(d.moduleForFile(QStringLiteral("README_no_ext")), nullptr);
}

void tst_MediaDispatcher::test_isExtensionKnown()
{
    auto &d = MediaDispatcher::instance();
    // 没注册任何模块: 走 FileExtensionRegistry 默认表
    //   默认表有 .png (imageWorker), 所以 .png 已知; .xyz 不在
    QVERIFY(d.isExtensionKnown(QStringLiteral("foo.png")));
    QVERIFY(d.isExtensionKnown(QStringLiteral("foo.mp3")));
    QVERIFY(d.isExtensionKnown(QStringLiteral("foo.mp4")));
    QVERIFY(!d.isExtensionKnown(QStringLiteral("foo.xyz")));
    QVERIFY(!d.isExtensionKnown(QStringLiteral("foo.exe")));
    QVERIFY(!d.isExtensionKnown(QString()));
    QVERIFY(!d.isExtensionKnown(QStringLiteral("no_ext")));

    // 注册自定义模块后: 自定义扩展名也已知
    d.registerModule(m_modA);
    QVERIFY(d.isExtensionKnown(QStringLiteral("foo.testext")));
}

void tst_MediaDispatcher::test_moduleCount()
{
    auto &d = MediaDispatcher::instance();
    QCOMPARE(d.moduleCount(), 0);
    d.registerModule(m_modA);
    QCOMPARE(d.moduleCount(), 1);
    d.registerModule(m_modB);
    QCOMPARE(d.moduleCount(), 2);
    d.unregisterAll();
    QCOMPARE(d.moduleCount(), 0);
}

void tst_MediaDispatcher::test_openFile_empty()
{
    auto &d = MediaDispatcher::instance();
    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openFile(QString());
    QCOMPARE(w, nullptr);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString());
}

void tst_MediaDispatcher::test_openFile_unknownExt()
{
    auto &d = MediaDispatcher::instance();
    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openFile(QStringLiteral("D:/foo/bar.xyz"));
    QCOMPARE(w, nullptr);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().contains(QStringLiteral("no module supports")));
}

void tst_MediaDispatcher::test_openFile_knownButNoModule()
{
    auto &d = MediaDispatcher::instance();
    // 没注册任何模块, 但扩展名 .png 在 FileExtensionRegistry 默认表里
    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openFile(QStringLiteral("D:/foo/bar.png"));
    QCOMPARE(w, nullptr);
    QCOMPARE(spy.count(), 1);
    // 错误信息应该明确说 "module xxx not registered"
    QVERIFY(spy.at(0).at(1).toString().contains(QStringLiteral("not registered")));
}

void tst_MediaDispatcher::test_openFile_knownAndRegistered()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);

    QSignalSpy spyFailed(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openFile(QStringLiteral("D:/foo/bar.testext"));
    QVERIFY(w != nullptr);
    QCOMPARE(w->filePath(), QStringLiteral("D:/foo/bar.testext"));
    QCOMPARE(w->moduleId(), QStringLiteral("moduleA"));
    QCOMPARE(spyFailed.count(), 0);   // 没失败
    delete w;   // 测试 ownership
}

void tst_MediaDispatcher::test_openFile_singletonAffectedByLastRegistered()
{
    auto &d = MediaDispatcher::instance();
    // 决策 5: 阶段 0 后注册的 IModule 覆盖默认 FileExtensionRegistry
    // 也就是说: 阶段 0 时 m_extMap 填的是 nullptr 占位, 真模块注册后才会被覆盖
    // 下面测试: 先注册 modA (.testext), 然后再注册一个同扩展名 (但 id 不同) — 不会冲突因为 .testext 不在默认表
    d.registerModule(m_modA);
    IWorkspace *w = d.openFile(QStringLiteral("foo.testext"));
    QVERIFY(w != nullptr);
    delete w;

    // 真正"覆盖默认"测试: 注册一个 modA2 跟 modA 扩展名完全相同, id 相同
    // 阶段 0 决策: duplicate id 触发 "replace" (先 unregister 旧的)
    auto *modA2 = new TestModule(QStringLiteral("moduleA"), { QStringLiteral(".testext") });
    d.registerModule(modA2);
    IWorkspace *w2 = d.openFile(QStringLiteral("foo.testext"));
    QVERIFY(w2 != nullptr);
    QCOMPARE(w2->moduleId(), QStringLiteral("moduleA"));
    delete w2;
    delete modA2;
}

void tst_MediaDispatcher::test_openModuleById_unknown()
{
    auto &d = MediaDispatcher::instance();
    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openModuleById(QStringLiteral("nonexistent"));
    QCOMPARE(w, nullptr);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().contains(QStringLiteral("not registered")));
}

void tst_MediaDispatcher::test_openModuleById_noCreateNew()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modA);   // modA.createNew() 返 nullptr

    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openModuleById(QStringLiteral("moduleA"));
    QCOMPARE(w, nullptr);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(1).toString().contains(QStringLiteral("does not support createNew")));
}

void tst_MediaDispatcher::test_openModuleById_supported()
{
    auto &d = MediaDispatcher::instance();
    d.registerModule(m_modC);   // modC 支持 createNew

    QSignalSpy spy(&d, &MediaDispatcher::openFailed);
    IWorkspace *w = d.openModuleById(QStringLiteral("moduleC"));
    QVERIFY(w != nullptr);
    QCOMPARE(w->moduleId(), QStringLiteral("moduleC"));
    QCOMPARE(spy.count(), 0);
    delete w;
}

void tst_MediaDispatcher::test_modulesChanged_emits()
{
    auto &d = MediaDispatcher::instance();
    QSignalSpy spy(&d, &MediaDispatcher::modulesChanged);

    d.registerModule(m_modA);
    QCOMPARE(spy.count(), 1);

    d.registerModule(m_modB);
    QCOMPARE(spy.count(), 2);

    d.unregisterModule(m_modA);
    QCOMPARE(spy.count(), 3);

    d.unregisterAll();
    QCOMPARE(spy.count(), 4);

    // 空状态再 unregisterAll 不 emit
    d.unregisterAll();
    QCOMPARE(spy.count(), 4);

    // nullptr register/unregister 不 emit
    d.registerModule(nullptr);
    d.unregisterModule(nullptr);
    QCOMPARE(spy.count(), 4);
}

QTEST_MAIN(tst_MediaDispatcher)
#include "tst_MediaDispatcher.moc"
