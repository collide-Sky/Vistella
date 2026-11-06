// SPDX-License-Identifier: MIT
//
// DockLayoutManager implementation.
//
// See DockLayoutManager.h for the why.
#include "DockLayoutManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QSaveFile>
#include <QStandardPaths>

namespace app {

namespace {

// The key the layout blob lives under inside the ini file.
const char* const kLayoutKey = "MainWindow/layoutState";

// The version of the file format. Bumped when the payload layout
// changes in a way that makes old files unreadable.
const char* const kFormatVersion = "1";

} // namespace

DockLayoutManager::DockLayoutManager()
    : m_dir(defaultDirectory())
{
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

QString DockLayoutManager::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString DockLayoutManager::currentLayoutPath(const QString& dir)
{
    const QString base = dir.isEmpty() ? defaultDirectory() : dir;
    return QDir(base).filePath(QStringLiteral("layout_current.ini"));
}

QString DockLayoutManager::defaultLayoutPath(const QString& dir)
{
    const QString base = dir.isEmpty() ? defaultDirectory() : dir;
    return QDir(base).filePath(QStringLiteral("layout_default.ini"));
}

void DockLayoutManager::setDirectory(const QString& dir)
{
    m_dir = dir;
}

void DockLayoutManager::setWindow(QMainWindow* window)
{
    m_window = window;
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

bool DockLayoutManager::writeFile(const QString& path, const QByteArray& payload)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile writes to a temp file and renames on commit, so a crash
    // mid-write cannot leave a half-written layout behind -- a corrupt
    // layout would silently lose the user's arrangement.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (f.write(payload) != payload.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

QByteArray DockLayoutManager::readFile(const QString& path, bool* ok)
{
    if (ok) *ok = false;
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray payload = f.readAll();
    f.close();
    if (ok) *ok = true;
    return payload;
}

QByteArray DockLayoutManager::extractState(const QByteArray& payload)
{
    const QByteArray needle = QByteArrayLiteral("state=");
    int pos = payload.indexOf(needle);
    if (pos < 0) {
        return {};
    }
    pos += needle.size();
    int end = payload.indexOf('\n', pos);
    if (end < 0) {
        end = payload.size();
    }
    QByteArray b64 = payload.mid(pos, end - pos).trimmed();
    if (b64.isEmpty()) {
        return {};
    }
    return QByteArray::fromBase64(b64);
}

// ---------------------------------------------------------------------------
// The three pieces
// ---------------------------------------------------------------------------

bool DockLayoutManager::save()
{
    m_lastError.clear();
    if (!m_window) {
        m_lastError = QStringLiteral("no window bound");
        return false;
    }

    const QByteArray state = m_window->saveState();
    if (state.isEmpty()) {
        m_lastError = QStringLiteral("saveState() returned an empty payload");
        return false;
    }

    // The payload is stored verbatim under a versioned key. QSettings
    // would base64 it; a plain file keeps it readable and lets us hand
    // it back to restoreState() unchanged.
    QByteArray payload;
    payload += QByteArrayLiteral("[layout]\n");
    payload += QByteArrayLiteral("version=") + kFormatVersion + QByteArrayLiteral("\n");
    payload += QByteArrayLiteral("state=") + state.toBase64() + QByteArrayLiteral("\n");

    if (!writeFile(currentLayoutPath(m_dir), payload)) {
        m_lastError = QStringLiteral("cannot write ") + currentLayoutPath(m_dir);
        return false;
    }
    return true;
}

bool DockLayoutManager::restore()
{
    m_lastError.clear();
    if (!m_window) {
        m_lastError = QStringLiteral("no window bound");
        return false;
    }

    bool ok = false;
    const QByteArray payload = readFile(currentLayoutPath(m_dir), &ok);
    if (!ok) {
        m_lastError = QStringLiteral("no saved layout at ") + currentLayoutPath(m_dir);
        return false;
    }

    const QByteArray state = extractState(payload);
    if (state.isEmpty()) {
        m_lastError = QStringLiteral("saved layout carries no state blob");
        return false;
    }

    // restoreState() returns false when the payload does not match the
    // window's current dock set (a dock was added or removed since the
    // layout was saved). That is not fatal: the window keeps its
    // current arrangement.
    if (!m_window->restoreState(state)) {
        m_lastError = QStringLiteral("restoreState() rejected the saved layout");
        return false;
    }
    return true;
}

bool DockLayoutManager::reset()
{
    m_lastError.clear();
    if (!m_window) {
        m_lastError = QStringLiteral("no window bound");
        return false;
    }

    bool ok = false;
    const QByteArray payload = readFile(defaultLayoutPath(m_dir), &ok);
    if (!ok) {
        // No default saved yet: the built-in arrangement is the default.
        // Nothing to restore, and the window already IS in its built-in
        // arrangement, so this is a success.
        m_lastError.clear();
        return true;
    }

    const QByteArray state = extractState(payload);
    if (state.isEmpty()) {
        m_lastError = QStringLiteral("default layout carries no state blob");
        return false;
    }

    if (!m_window->restoreState(state)) {
        m_lastError = QStringLiteral("restoreState() rejected the default layout");
        return false;
    }
    return true;
}

bool DockLayoutManager::saveAsDefault()
{
    m_lastError.clear();
    if (!m_window) {
        m_lastError = QStringLiteral("no window bound");
        return false;
    }

    const QByteArray state = m_window->saveState();
    if (state.isEmpty()) {
        m_lastError = QStringLiteral("saveState() returned an empty payload");
        return false;
    }

    QByteArray payload;
    payload += QByteArrayLiteral("[layout]\n");
    payload += QByteArrayLiteral("version=") + kFormatVersion + QByteArrayLiteral("\n");
    payload += QByteArrayLiteral("state=") + state.toBase64() + QByteArrayLiteral("\n");

    if (!writeFile(defaultLayoutPath(m_dir), payload)) {
        m_lastError = QStringLiteral("cannot write ") + defaultLayoutPath(m_dir);
        return false;
    }
    return true;
}

bool DockLayoutManager::hasDefault() const
{
    return QFileInfo::exists(defaultLayoutPath(m_dir));
}

} // namespace app
