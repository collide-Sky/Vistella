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

    // Q4.1 (2026-09-23) — 5 new regression tests covering the unified-entry
    //   invariant restoration. Previous 4 tests verified *which paths*
    //   exist; the new tests verify *which paths must NOT exist*.
    void test_loadWindowState_defaultModeIsNormal();              // 5
    void test_mouseMoveEvent_dragRestoresVia_setModeNormal();      // 6
    void test_setMode_logInvariant_includesOldAndNewState();      // 7
    void test_mainwindow_h_documentsUnifiedEntryInvariant();      // 8
    void test_showMinimized_isTheOnlyException();                 // 9

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
    // it only restores to normal via setMode(Normal)).
    // Q4.1 (2026-09-23): 之前 mouseMoveEvent drag-to-restore 直接调 showNormal()
    //   绕过 setMode → m_mode 没更新 → 下次点 Maximized 按钮 / 双击 / QSettings
    //   启动加载都会强制 Maximized. 现在统一走 setMode(Normal).
    //   唯一允许的 showMaximized()/showNormal() 调用必须在 setMode body 里.
    QCOMPARE(totalShowMax, 1);
    QCOMPARE(totalShowNormal, 1);

    // Verify mouseMoveEvent drag handler no longer contains showNormal() —
    // it must go through setMode(Normal) for the invariant to hold.
    const QString dragBody = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::mouseMoveEvent(QMouseEvent"));
    QVERIFY2(!dragBody.isEmpty(),
             "void MainWindow::mouseMoveEvent(QMouseEvent) definition not found");
    QString dragCodeOnly = dragBody;
    dragCodeOnly.replace(reLineComment, QString());
    dragCodeOnly.replace(reBlockComment, QString());
    dragCodeOnly.replace(reStringLiteral, QStringLiteral("\"\""));
    QCOMPARE(dragCodeOnly.count(QStringLiteral("showNormal()")), 0);
    QVERIFY2(dragCodeOnly.contains(QStringLiteral("setMode(WindowMode::Normal)")),
             "mouseMoveEvent drag-to-restore must call setMode(WindowMode::Normal) "
             "to keep m_mode in sync (Q4.1 invariant)");
}

// ---------------------------------------------------------------------------
// Q4.1 (2026-09-23) regression tests — 5 invariants guarding the
//   "all mode changes go through setMode" restoration. Together with the
//   4 pre-existing tests, these form a tight fence around the state machine.
// ---------------------------------------------------------------------------

// Test 5: loadWindowState must default to Normal (not Maximized) so that
//   cold-start and "abnormal-exit + user-cancels-recovery" both fall into
//   setMode(Normal) → centered 1280x800, instead of being force-maximized.
void tst_WindowMode::test_loadWindowState_defaultModeIsNormal()
{
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    // Strip ONLY comments (NOT string literals — we want to match the
    // "window/mode" key inside QStringLiteral(...)).
    static const QRegularExpression reLineComment(QStringLiteral("//[^\n]*"));
    static const QRegularExpression reBlockComment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);

    // Locate the loadWindowState body and inspect the QSettings.value() default.
    const QString body = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::loadWindowState()"));
    QVERIFY2(!body.isEmpty(),
             "void MainWindow::loadWindowState() definition not found");

    QString bodyStrip = body;
    bodyStrip.replace(reLineComment, QString());

    // Must contain "window/mode" key (read access).
    QVERIFY2(bodyStrip.contains(QStringLiteral("window/mode")),
             "loadWindowState must read the \"window/mode\" QSettings key");

    // The QSettings.value() default value must be WindowMode::Normal (Q4.1).
    //   The form is: s.value("window/mode", int(WindowMode::Normal)).toInt();
    // Use a 2-step check: locate the window/mode substring, then inspect the
    // next ~80 chars (the default argument).
    const int keyPos = bodyStrip.indexOf(QStringLiteral("window/mode"));
    QVERIFY2(keyPos >= 0, "loadWindowState must reference window/mode QSettings key");
    // Window covers the whole s.value(...) call expression — up to ~80 chars
    // is enough to span `(QStringLiteral("window/mode"), int(WindowMode::Normal))`.
    const QString afterKey = bodyStrip.mid(keyPos,
        qMin(80, bodyStrip.size() - keyPos));
    QVERIFY2(afterKey.contains(QStringLiteral("WindowMode::Normal")),
             "loadWindowState QSettings.value default for window/mode must be "
             "int(WindowMode::Normal) — Q4.1 fix changed default from Maximized "
             "to Normal so cold-start + 'cancel-recovery' paths fall into "
             "setMode(Normal) instead of being force-maximized.");
    QVERIFY2(!afterKey.contains(QStringLiteral("WindowMode::Maximized")),
             "loadWindowState QSettings.value default must NOT be Maximized "
             "(Q4.1: this was the root cause of 'cancel-recovery also maximizes')");
}

// Test 6: mouseMoveEvent drag-to-restore must call setMode(WindowMode::Normal),
//   not showNormal() directly. Guards against reintroduction of the bypass
//   that caused m_mode to drift out of sync with the actual window state.
void tst_WindowMode::test_mouseMoveEvent_dragRestoresVia_setModeNormal()
{
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    const QString dragBody = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::mouseMoveEvent(QMouseEvent"));
    QVERIFY2(!dragBody.isEmpty(),
             "void MainWindow::mouseMoveEvent(QMouseEvent) definition not found");

    QVERIFY2(dragBody.contains(QStringLiteral("setMode(WindowMode::Normal)")),
             "mouseMoveEvent drag-to-restore must call setMode(WindowMode::Normal) "
             "to keep m_mode in sync (Q4.1)");

    // Must NOT directly call showNormal (bypass invariant).
    static const QRegularExpression reLineComment(QStringLiteral("//[^\n]*"));
    static const QRegularExpression reBlockComment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression reStringLiteral(
        QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    QString dragCodeOnly = dragBody;
    dragCodeOnly.replace(reLineComment, QString());
    dragCodeOnly.replace(reBlockComment, QString());
    dragCodeOnly.replace(reStringLiteral, QStringLiteral("\"\""));
    QCOMPARE(dragCodeOnly.count(QStringLiteral("showNormal()")), 0);
    QCOMPARE(dragCodeOnly.count(QStringLiteral("showMaximized()")), 0);
}

// Test 7: setMode() must log both old and new state at enter/exit, so that
//   future "who called setMode without updating m_mode?" bugs are easy to
//   trace from the log alone. Guards against accidental log simplification.
void tst_WindowMode::test_setMode_logInvariant_includesOldAndNewState()
{
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    const QString body = extractFunctionBody(cpp,
        QStringLiteral("void MainWindow::setMode(WindowMode"));
    QVERIFY2(!body.isEmpty(), "setMode body not found");

    // enter log line should mention oldMode (so we can trace regressions).
    QVERIFY2(body.contains(QStringLiteral("oldMode")),
             "setMode enter log must include 'oldMode' for regression tracing (Q4.1)");
    // exit log line should mention m_mode (new value).
    QVERIFY2(body.contains(QStringLiteral("m_mode")),
             "setMode body must mention m_mode (Q4.1 log invariant)");

    // Immediate m_isMaximized sync at the end (so callers reading
    // m_isMaximized right after setMode see the new value, not the old one).
    QVERIFY2(body.contains(QStringLiteral("m_isMaximized = isMaximized()")),
             "setMode must sync m_isMaximized before returning "
             "(Q4.1: avoid race for callers reading m_isMaximized immediately)");
}

// Test 8: mainwindow.h must document the unified-entry invariant, with
//   explicit ALLOWED / FORBIDDEN / EXCEPTION lists. This is the architectural
//   contract — without the documentation, future devs will not know what
//   the invariant is or why it matters.
void tst_WindowMode::test_mainwindow_h_documentsUnifiedEntryInvariant()
{
    const QString h = readOrEmpty(m_mainwindowH);
    QVERIFY2(!h.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowH));

    QVERIFY2(h.contains(QStringLiteral("INVARIANT")),
             "mainwindow.h must label setMode invariant with 'INVARIANT' marker");
    QVERIFY2(h.contains(QStringLiteral("ALLOWED")),
             "mainwindow.h invariant doc must list ALLOWED entry points");
    QVERIFY2(h.contains(QStringLiteral("FORBIDDEN")),
             "mainwindow.h invariant doc must list FORBIDDEN callers");
    QVERIFY2(h.contains(QStringLiteral("EXCEPTION")),
             "mainwindow.h invariant doc must note the showMinimized() exception");
    // Explicitly: showMaximized()/showNormal() outside setMode are forbidden.
    QVERIFY2(h.contains(QStringLiteral("showMaximized()"))
          && h.contains(QStringLiteral("showNormal()")),
             "mainwindow.h invariant doc must name the forbidden Qt calls");
}

// Test 9: showMinimized() is the only Qt-show call allowed outside setMode
//   (because minimizing doesn't touch Normal/Maximized mode). Any other
//   .showMaximized() / .showNormal() / .setGeometry() (window mode) on
//   the main window anywhere in src/ is a regression.
void tst_WindowMode::test_showMinimized_isTheOnlyException()
{
    // Scan src/app/mainwindow.cpp for direct showMinimized() calls —
    // these are tolerated (Qt standard minimize, not mode-changing).
    const QString cpp = readOrEmpty(m_mainwindowCpp);
    QVERIFY2(!cpp.isEmpty(), qPrintable(QString("cannot open ") + m_mainwindowCpp));

    static const QRegularExpression reLineComment(QStringLiteral("//[^\n]*"));
    static const QRegularExpression reBlockComment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression reStringLiteral(
        QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    QString codeOnly = cpp;
    codeOnly.replace(reLineComment, QString());
    codeOnly.replace(reBlockComment, QString());
    codeOnly.replace(reStringLiteral, QStringLiteral("\"\""));

    // showMinimized is the only Qt show call allowed outside setMode.
    // Confirm at least one exists (so the test isn't trivially passing).
    QVERIFY2(codeOnly.contains(QStringLiteral("showMinimized")),
             "expected showMinimized() somewhere in mainwindow.cpp");
    // No setGeometry() call directly in mainwindow.cpp that targets the
    // window itself (the canvas/painter-internal setGeometry calls inside
    // child widgets don't appear in mainwindow.cpp body).
    QVERIFY2(!codeOnly.contains(QStringLiteral("this->setGeometry")),
             "mainwindow.cpp must not call this->setGeometry directly — "
             "setMode is the only path allowed to size/place the window");
}

QTEST_MAIN(tst_WindowMode)
#include "tst_WindowMode.moc"