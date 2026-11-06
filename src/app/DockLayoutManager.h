// SPDX-License-Identifier: MIT
//
// DockLayoutManager - persistent dock layout for the main window.
//
// M2-2: the panel system moved to the native QDockWidget +
// QMainWindow::saveState / restoreState pair. That pair is what makes
// a PS-style workspace possible: the user drags a panel to another edge,
// floats it, tabifies it with another one, and the arrangement survives
// a restart.
//
// Before M2-2 the "reset layout" action read a workspace_default.ini
// that nothing ever wrote (audit APP-19), so the action was a no-op that
// only ever showed an error message.
//
// Three things are needed for the feature to actually work:
//
//   save()    the current arrangement, written on exit
//   restore() the arrangement read back at startup
//   reset()   go back to the arrangement the user declared as default
//
// The "default" is itself a saved arrangement (saveAsDefault()). It is
// what reset() goes back to, and it is separate from the live layout so
// that "I moved a panel" and "I want the original arrangement back" are
// two different operations.
//
// Where the files live:
//   <AppDataLocation>/layout_current.ini   the live layout (written on exit)
//   <AppDataLocation>/layout_default.ini   the user's default layout
//
// Both are plain QSettings ini files. The layout payload is the byte
// blob QMainWindow::saveState() produces; it is opaque to us and must
// be handed back to restoreState() unchanged.
//
// The class is deliberately free of any MainWindow knowledge so that it
// can be tested on a bare QMainWindow.
#pragma once

#include <QByteArray>
#include <QString>

class QMainWindow;

namespace app {

class DockLayoutManager
{
public:
    DockLayoutManager();

    // The directory the layout files live in.
    //
    // Split out so a test can point it at a temp dir. The default is
    // QStandardPaths::AppDataLocation.
    static QString defaultDirectory();
    static QString currentLayoutPath(const QString& dir = QString());
    static QString defaultLayoutPath(const QString& dir = QString());

    // The window whose arrangement is managed.
    void setWindow(QMainWindow* window);
    QMainWindow* window() const { return m_window; }

    // Where the files are read from / written to.
    void setDirectory(const QString& dir);
    QString directory() const { return m_dir; }

    // -- the three pieces ------------------------------------------------

    // Write the current arrangement to layout_current.ini.
    //
    // Returns false when there is no window or the file cannot be
    // written; the reason is reported through lastError().
    bool save();

    // Read layout_current.ini and apply it to the window.
    //
    // Returns false when there is no file, no window, or the payload is
    // rejected by QMainWindow::restoreState (which happens when the
    // window's dock set changed since the layout was saved).
    bool restore();

    // Apply the user's default arrangement (layout_default.ini).
    //
    // When there is no default file yet, the built-in arrangement is
    // applied instead -- that is the "fresh install" case.
    bool reset();

    // Write the current arrangement as the user's default.
    bool saveAsDefault();

    // True when a default arrangement exists on disk.
    bool hasDefault() const;

    // Human readable reason for the last failure (empty on success).
    QString lastError() const { return m_lastError; }

private:
    static bool writeFile(const QString& path, const QByteArray& payload);
    static QByteArray readFile(const QString& path, bool* ok);

    // Pull the base64 state blob out of a layout file payload.
    //
    // The file is a tiny ini:
    //   [layout]
    //   version=1
    //   state=<base64>
    // Returns an empty array when the payload has no state line.
    static QByteArray extractState(const QByteArray& payload);

    QMainWindow* m_window = nullptr;
    QString      m_dir;
    QString      m_lastError;
};

} // namespace app
