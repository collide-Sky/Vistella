// SPDX-License-Identifier: MIT
//
// AutoSave
//
// The auto-save and the crash recovery -- M2-1.
//
// THE PROBLEM
//
//   Before this, a crash lost EVERYTHING: the auto-save was a TODO and
//   the recovery was a TODO (docs/DEBT.md#A-AUTOSAVE). The user closed
//   the app, the app died, and the work was gone.
//
// WHAT THIS DOES
//
//   Two halves:
//
//     1. AutoSave -- every N seconds, if the document has unsaved
//        changes, write a RECOVERY COPY. It is NOT the document's own
//        file: the user asked for that file to hold what they last
//        saved, and silently overwriting it would destroy the state the
//        user last deliberately kept. The recovery copy goes to a
//        separate directory.
//
//     2. Recovery -- on startup, look for recovery copies that are
//        NEWER than the document's own file. Those are the crashed
//        sessions' work, and the user is offered them.
//
// THE RULES
//
//   - The recovery copy is written with the SAME .vst format as a
//     normal save, so it holds the whole layer stack and not a flat
//     picture. A recovery copy that lost the layers would be a
//     half-recovery.
//
//   - The write is ATOMIC (QSaveFile): a crash during the auto-save
//     cannot leave a truncated recovery file that then fails to load.
//
//   - The recovery copy is only written when the document is DIRTY.
//     Writing an unchanged document every N seconds would fill the
//     disk and teach the user to ignore the recovery prompt.
//
//   - The recovery file is DELETED once the user saves the document
//     properly. A recovery copy that outlives the save it was made
//     for is just stale noise.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace layers { class LayerStack; }

namespace project {

class AutoSave : public QObject
{
    Q_OBJECT
public:
    explicit AutoSave(QObject* parent = nullptr);
    ~AutoSave() override;

    // The interval between two auto-save checks, in milliseconds.
    //   The default is 5 minutes -- Photoshop's default is 5, and 10
    //   minutes is the other common one.
    void    setIntervalMs(int ms);
    int     intervalMs() const { return m_intervalMs; }

    // Start / stop the timer.
    void start();
    void stop();
    bool isRunning() const;

    // Where the recovery copies live.
    //
    //   The default is <QStandardPaths::AppDataLocation>/recovery, so
    //   the copies stay out of the user's documents folder and out of
    //   the project folder.
    void    setRecoveryDir(const QString& dir);
    QString recoveryDir() const { return m_recoveryDir; }

    // The recovery file for one document.
    //
    //   The name is derived from the document's own file path, so two
    //   open documents do not overwrite each other's recovery copy.
    //   A document that has never been saved gets a name derived from
    //   its window identity instead.
    QString recoveryPathFor(const QString& documentPath) const;

    // Write a recovery copy of the stack.
    //
    //   Returns false (and fills `error`) when the copy could not be
    //   written. The caller decides whether to report it -- an
    //   auto-save failure is usually not worth a dialog.
    bool writeRecovery(const QString& documentPath,
                       const layers::LayerStack* stack,
                       QString* error = nullptr);

    // Remove the recovery copy of a document.
    //
    //   Called after a successful save: the recovery copy has done its
    //   job.
    void removeRecovery(const QString& documentPath);

    // The recovery copies that are NEWER than the document's own file.
    //
    //   Those are the sessions that crashed. A recovery copy that is
    //   OLDER than the saved file means the user saved after the crash
    //   (or the copy is stale) -- there is nothing to recover.
    QStringList findRecoverable(const QString& documentPath) const;

    // ===== the hooks the owner installs =====
    //
    //   The auto-save needs two things from the document: whether it
    //   has unsaved changes, and how to write a recovery copy. The
    //   owner installs both, so this class does not have to know what a
    //   document is.
    void setDirtyChecker(std::function<bool()> fn) { m_isDirty = std::move(fn); }
    void setRecoveryWriter(std::function<bool(const QString&)> fn) { m_writeRecovery = std::move(fn); }

    // Run one auto-save tick NOW (the timer tick does the same).
    //
    //   Exposed so the test can drive it without waiting for a timer.
    void tick() { onTick(); }

private slots:
    // The timer tick: write a recovery copy if the document is dirty.
    void onTick();

private:
    // Set by the owner (ImageWindow) so the tick knows whether there is
    // anything worth saving.
    std::function<bool()> m_isDirty;
    std::function<bool(const QString&)> m_writeRecovery;

    QTimer* m_timer = nullptr;
    int     m_intervalMs = 5 * 60 * 1000;
    QString m_recoveryDir;
};

} // namespace project
