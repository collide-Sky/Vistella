// SPDX-License-Identifier: MIT
//
// AutoSave implementation
//
//   See AutoSave.h for the why.
#include "AutoSave.h"

#include "VstProject.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include <QCryptographicHash>

#include <algorithm>

namespace project {

namespace {

// A stable name for a document, used for the recovery file.
//
//   The document's own path is hashed, so two documents never share a
//   recovery file and the name does not leak the path into the file
//   system (the path can contain characters that are not valid in a
//   file name, and it can be long).
QString documentKey(const QString& documentPath)
{
    const QByteArray h =
        QCryptographicHash::hash(documentPath.toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(h.toHex().left(16));
}

} // namespace

AutoSave::AutoSave(QObject* parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMs);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &AutoSave::onTick);

    // The default recovery directory: the app's own data location, so
    // the copies never end up in the user's documents folder.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_recoveryDir = base.isEmpty() ? QDir::tempPath()
                                    : QDir(base).filePath(QStringLiteral("recovery"));
}

AutoSave::~AutoSave() = default;

void AutoSave::setIntervalMs(int ms)
{
    // A zero or negative interval would spin the timer; clamp it to
    // something sane.
    m_intervalMs = std::max(1000, ms);
    if (m_timer) m_timer->setInterval(m_intervalMs);
}

void AutoSave::start()
{
    if (m_timer) m_timer->start();
}

void AutoSave::stop()
{
    if (m_timer) m_timer->stop();
}

bool AutoSave::isRunning() const
{
    return m_timer && m_timer->isActive();
}

void AutoSave::setRecoveryDir(const QString& dir)
{
    m_recoveryDir = dir;
}

QString AutoSave::recoveryPathFor(const QString& documentPath) const
{
    return QDir(m_recoveryDir).filePath(documentKey(documentPath) + QStringLiteral(".vst"));
}

bool AutoSave::writeRecovery(const QString& documentPath,
                             const layers::LayerStack* stack,
                             QString* error)
{
    if (!stack) {
        if (error) *error = QStringLiteral("no layer stack to save");
        return false;
    }

    QDir dir(m_recoveryDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("cannot create the recovery directory %1")
                                .arg(m_recoveryDir);
        return false;
    }

    const QString path = recoveryPathFor(documentPath);

    // The SAME format as a normal save, so the recovery copy holds the
    // whole layer stack. A recovery copy that lost the layers would be
    // a half-recovery.
    const VstResult r = saveVst(path, stack);
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    return true;
}

void AutoSave::removeRecovery(const QString& documentPath)
{
    const QString path = recoveryPathFor(documentPath);
    QFile::remove(path);
}

QStringList AutoSave::findRecoverable(const QString& documentPath) const
{
    QStringList out;

    const QString path = recoveryPathFor(documentPath);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return out;

    // The recovery copy has to be NEWER than the document's own file.
    // An older one means the user saved after the crash (or the copy is
    // just stale) -- there is nothing to recover.
    const QFileInfo docInfo(documentPath);
    if (docInfo.exists() && docInfo.lastModified() >= info.lastModified()) {
        return out;
    }

    out.append(path);
    return out;
}

void AutoSave::onTick()
{
    if (!m_isDirty) return;
    if (!m_isDirty()) return;
    if (!m_writeRecovery) return;
    m_writeRecovery(QString());
}

} // namespace project
