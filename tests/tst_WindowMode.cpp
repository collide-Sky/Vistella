// SPDX-License-Identifier: MIT
//
// tst_WindowMode - P1.4.7 (2026-09-18) WindowMode architecture regression test.
//
// Purpose:
//   P1.4.6 fixed a bug where 3 separate paths in the codebase each called
//   showMaximized()/showNormal() directly:
//     - loadWindowState() in mainwindow.cpp
//     - main.cpp splash timer (3 callsites: clean, no-files, post-prompt)
//     - setMode() itself
//   The fix unified all 3 paths through setMode() via applyWindowMode().
//   This test guards that invariant: future code MUST go through setMode().
//
// Approach:
//   Constructing MainWindow in a unit test pulls in the full workspace /
//   image pipeline, which is heavy and not testable here. Instead we read
//   the source files as text and verify the architectural invariant holds.
//   This is a static / textual check, not a runtime Qt-window test, but it
//   is exactly the right tool for "no one added a 4th direct call site".
//
// Tests:
//   1. applyWindowMode exists in mainwindow.h
//   2. loadWindowState body routes through applyWindowMode (no direct Qt call)
//   3. main.cpp does not call showMaximized()/showNormal() directly
//   4. setMode body is the sole entry for showMaximized(). showNormal() is
//      used twice: once inside setMode, once inside mouseMoveEvent's
//      drag-to-restore handler (event-driven, not programmatic). Both are
//      tolerated; any additional programmatic callsite is a regression.
//
// Path discovery:
//   Test binaries run from <build>/. We walk up the directory tree until
//   we find src/app/mainwindow.cpp (the project source root marker).
//   This works regardless of generator / build dir layout as long as the
//   standard <project_root>/build/.../<Target> structure is preserved.
//   Note: the build tree may also have a src/app/ directory (CMake output),
//   so we look for a source FILE inside, not just the directory.

#include <QTest>
#include <QObject>
#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

#include <QDebug>
#include <QCoreApplication>

class tst_WindowMode : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 1. Header declares applyWindowMode as the single re-entry point
    void test_applyWindowMode_declaredInHeader();

    // 2. loadWindowState body ends with applyWindowMode() and contains no
    //    direct showMaximized()/showNormal() calls.
    void test_loadWindowState_routesThrough_applyWindowMode();

    // 3. main.cpp never calls showMaximized()/showNormal() directly. The
    //    only allowed entry is w.applyWindowMode() (which delegates to
    //    setMode). Catches regressions from future splash / startup paths.
    void test_main_cpp_doesNotCallQtShowDirectly();

    // 4. setMode body is the SOLE entry for showMaximized()/showNormal().
    //    Each appears exactly once, both inside the setMode function body,
    //    nowhere else in mainwindow.cpp.
    void test_setMode_isSoleEntryForShowMaximizedShowNormal();

private:
    QString m_mainwindowCpp;   // absolute path
    QString m_mainwindowH;
    QString m_mainCpp;

    // Walk up from the test binary's directory until we find src/app/.
    QString locateSourceRoot() const;
    // Read whole file or return empty string on failure (test should QSKIP).
    QString readOrEmpty(const QString &absPath) const;
    // Extract the body of `void MainWindow::funcName(` from a .cpp blob.
    // Returns the substring from the opening `{` to the matching closing `}`.
    QString extractFunctionBody(const QString &cpp, const QString &funcSigPrefix) const;
};

QString tst_WindowMode::locateSourceRoot() const
{
    // We look for the canonical marker: src/app/mainwindow.cpp. There may be
    // an unrelated src/app directory inside the build tree (CMake output), so
    // we must check for the source FILE, not just the source directory.
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString probe = d.absoluteFilePath(QStringLiteral("src/app/mainwindow.cpp"));
        if (QFileInfo::exists(probe))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

QString tst_WindowMode::readOrEmpty(const QString &absPath) const
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString tst_WindowMode::extractFunctionBody(const QString &cppText,
                                            const QString &funcSigPrefix) const
{
    // Naive but adequate: find the function signature, then scan braces.
    const int sigIdx = cppText.indexOf(funcSigPrefix);
    if (sigIdx < 0)
        return QString();
    int braceDepth = 0;
    int bodyStart = -1;
    bool started = false;
    for (int i = sigIdx; i < cppText.size(); ++i) {
        const QChar c = cppText.at(i);
        if (c == QLatin1Char('{')) {
            braceDepth += 1;
            if (!started) {
                bodyStart = i + 1;
                started = true;
            }
        } else if (c == QLatin1Char('}')) {
            braceDepth -= 1;
            if (started && braceDepth == 0)
                return cppText.mid(bodyStart, i - bodyStart);
        }
    }
    return QString();
}

// =============================================================================
// Test implementation
// =============================================================================

void tst_WindowMode::initTestCase()
{
    const QString root = locateSourceRoot();
    QVERIFY2(!root.isEmpty(),
             "Cannot find src/app/mainwindow.cpp — test must run from a "
             "build dir under the project root (e.g. <root>/build/...)");

    m_mainwindowH = root + QStringLiteral("/src/app/mainwindow.h");
    m_mainwindowCpp = root + QStringLiteral("/src/app/mainwindow.cpp");
    m_mainCpp = root + QStringLiteral("/src/app/main.cpp");

    QVERIFY2(QFileInfo::exists(m_mainwindowH), qPrintable(m_mainwindowH));
    QVERIFY2(QFileInfo::exists(m_mainwindowCpp), qPrintable(m_mainwindowCpp));
    QVERIFY2(QFileInfo::exists(m_mainCpp), qPrintable(m_mainCpp));
}

void tst_WindowMode::cleanupTestCase()
{
    // no-op
}

void tst_WindowMode::test_applyWindowMode_declaredInHeader()
{
    const QString h = readOrEmpty(m_mainwindowH);
    QVERIFY2(!h.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowH));

    // The public re-entry point must exist as a member function.
    QVERIFY2(h.contains(QStringLiteral("applyWindowMode")),
             "mainwindow.h must declare applyWindowMode() as the unified "
             "window-mode re-entry point (see P1.4.6 fix)");
    // And it must delegate to setMode (either inline or in body).
    QVERIFY2(h.contains(QStringLiteral("applyWindowMode"))
          && (h.contains(QStringLiteral("applyWindowMode() { setMode("))
              || h.contains(QStringLiteral("applyWindowMode()\n{ setMode("))),
             "applyWindowMode() must delegate to setMode(m_mode); direct "
             "Qt show calls are forbidden here.");
}

void tst_WindowMode::test_loadWindowState_routesThrough_applyWindowMode()
{
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    const QString body = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::loadWindowState()"));
    QVERIFY2(!body.isEmpty(),
             "void MainWindow::loadWindowState() definition not found");

    // Body must end with an applyWindowMode() call (after the QSettings read).
    QVERIFY2(body.contains(QStringLiteral("applyWindowMode()")),
             "loadWindowState must end with applyWindowMode() so Normal mode "
             "centers geometry and button text syncs");

    // Body must NOT contain direct Qt show calls.
    QVERIFY2(!body.contains(QStringLiteral("showMaximized()")),
             "loadWindowState must not call showMaximized() directly — "
             "always go through setMode via applyWindowMode");
    QVERIFY2(!body.contains(QStringLiteral("showNormal()")),
             "loadWindowState must not call showNormal() directly — "
             "always go through setMode via applyWindowMode");
}

void tst_WindowMode::test_main_cpp_doesNotCallQtShowDirectly()
{
    const QString cpp = readOrEmpty(m_mainCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainCpp));

    // Strip comments and string literals so we don't catch false positives
    // (e.g. a log message mentioning the function name).
    QString stripped = cpp;
    // Strip line comments.
    static const QRegularExpression reLineComment(QStringLiteral("//[^\n]*"));
    stripped.replace(reLineComment, QString());
    // Strip block comments.
    static const QRegularExpression reBlockComment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    stripped.replace(reBlockComment, QString());
    // Strip string literals (very naive, but sufficient here).
    static const QRegularExpression reStringLiteral(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    stripped.replace(reStringLiteral, QStringLiteral("\"\""));

    QVERIFY2(!stripped.contains(QStringLiteral(".showMaximized()")),
             "main.cpp must not call showMaximized() directly. "
             "Use MainWindow::applyWindowMode() (which routes through setMode).");
    QVERIFY2(!stripped.contains(QStringLiteral(".showNormal()")),
             "main.cpp must not call showNormal() directly. "
             "Use MainWindow::applyWindowMode() (which routes through setMode).");

    // Sanity: applyWindowMode IS used (so the test would not silently pass
    // on a stripped-from-existence main.cpp).
    QVERIFY2(cpp.contains(QStringLiteral("applyWindowMode()")),
             "main.cpp should call MainWindow::applyWindowMode() as the "
             "window-mode entry point");
}

void tst_WindowMode::test_setMode_isSoleEntryForShowMaximizedShowNormal()
{
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    // Strip comments so a "showMaximized()" mention inside a comment is not
    // counted as a real call. String literals are unlikely to contain these
    // exact tokens, but strip them too for safety.
    static const QRegularExpression reLineComment(QStringLiteral("//[^\n]*"));
    static const QRegularExpression reBlockComment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression reStringLiteral(
        QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    QString codeOnly = cpp;
    codeOnly.replace(reLineComment, QString());
    codeOnly.replace(reBlockComment, QString());
    codeOnly.replace(reStringLiteral, QStringLiteral("\"\""));

    // Whole-file call counts (real source lines, comments excluded).
    const int totalShowMax = codeOnly.count(QStringLiteral("showMaximized()"));
    const int totalShowNormal = codeOnly.count(QStringLiteral("showNormal()"));

    // setMode body — exactly one of each inside.
    const QString setModeBody = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::setMode(WindowMode"));
    QVERIFY2(!setModeBody.isEmpty(),
             "void MainWindow::setMode(WindowMode) definition not found");

    QString setModeCodeOnly = setModeBody;
    setModeCodeOnly.replace(reLineComment, QString());
    setModeCodeOnly.replace(reBlockComment, QString());
    setModeCodeOnly.replace(reStringLiteral, QStringLiteral("\"\""));

    QCOMPARE(setModeCodeOnly.count(QStringLiteral("showMaximized()")), 1);
    QCOMPARE(setModeCodeOnly.count(QStringLiteral("showNormal()")), 1);

    // Outside setMode: zero showMaximized() (drag handler does not maximize,
    // it only restores to normal). One showNormal() is allowed and must
    // live in mouseMoveEvent (titleBar drag-to-restore gesture, which is
    // event-driven, not a programmatic entry point).
    QCOMPARE(totalShowMax, 1);
    QCOMPARE(totalShowNormal, 2);

    // Verify the second showNormal() lives in mouseMoveEvent's drag handler.
    const QString dragBody = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::mouseMoveEvent(QMouseEvent"));
    QVERIFY2(!dragBody.isEmpty(),
             "void MainWindow::mouseMoveEvent(QMouseEvent) definition not found");
    QString dragCodeOnly = dragBody;
    dragCodeOnly.replace(reLineComment, QString());
    dragCodeOnly.replace(reBlockComment, QString());
    dragCodeOnly.replace(reStringLiteral, QStringLiteral("\"\""));
    QCOMPARE(dragCodeOnly.count(QStringLiteral("showNormal()")), 1);
}

QTEST_MAIN(tst_WindowMode)
#include "tst_WindowMode.moc"