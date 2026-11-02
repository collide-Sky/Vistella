#ifndef UNDOSTACK_H
#define UNDOSTACK_H

// =============================================================
//  UndoStack -- the undo stack with a MEMORY BUDGET (M1-7)
//
//  Contract:
//
//    1. QUndoStack::setUndoLimit() bounds the NUMBER of commands. It
//       says nothing about memory. A 24 MP document costs 144 MB per
//       full-image undo entry (before + after clones), so 100 strokes
//       is ~14 GB -- the app is killed by the OS long before the
//       command count limit is reached.
//
//    2. This stack bounds the total BYTES held by the undo history.
//       Every command reports its own footprint (see
//       IUndoMemoryFootprint below); the stack keeps a running total
//       and drops the OLDEST commands when the total exceeds the
//       budget.
//
//    3. Commands that do not report a footprint count as 0 bytes --
//       they are small (a colour, a name, a flag) and there is no
//       point in evicting them.
//
//    4. The budget is a soft limit for the FIRST over-budget command:
//       one command that is bigger than the whole budget is still
//       kept (evicting everything else to make room for it would
//       leave the user with no history at all). The stack then
//       evicts until it is back under the budget.
//
//  -----------------------------------------------------------------
//  HOW THE EVICTION WORKS (and why it is built the way it is)
//  -----------------------------------------------------------------
//
//  QUndoStack offers exactly ONE way to drop a command from the BOTTOM
//  of the stack: setUndoLimit(). But Qt 6 refuses to change the limit
//  while the stack is non-empty:
//
//      void QUndoStack::setUndoLimit(int limit)
//      {
//          if (Q_UNLIKELY(!d->command_list.isEmpty())) {
//              qWarning("QUndoStack::setUndoLimit(): an undo limit can "
//                       "only be set when the stack is empty");
//              return;                      // <-- silently does nothing
//          }
//          ...
//      }
//
//  (verified against the Qt 6.11.1 sources, see
//  qtbase/src/gui/util/qundostack.cpp:1243)
//
//  So the stack cannot simply "lower the limit and let Qt trim".
//
//  What Qt DOES offer is a supported way to remove a command that is
//  marked obsolete:
//
//      void QUndoStack::undo()
//      {
//          int idx = d->index - 1;
//          QUndoCommand *cmd = d->command_list.at(idx);
//          if (!cmd->isObsolete())
//              cmd->undo();               // <-- skipped when obsolete
//          if (cmd->isObsolete()) {
//              delete d->command_list.takeAt(idx);   // <-- removed here
//              ...
//          }
//      }
//
//  A command that is marked obsolete is therefore deleted WITHOUT its
//  undo() ever being called -- i.e. with NO effect on the document.
//
//  The eviction is built from those two facts:
//
//    (a) This stack stores the real commands in its OWN list
//        (m_inners). The QUndoStack only ever holds lightweight
//        ReplayGuard wrappers that reference the real command.
//
//    (b) To evict, every guard is marked obsolete and the QUndoStack
//        is drained with undo(). That removes the guards -- and only
//        the guards -- with no effect whatsoever on the document.
//        The real commands stay alive in m_inners.
//
//    (c) The survivors are then pushed back as fresh guards in
//        "replay" mode: their first redo() is swallowed, so no
//        command re-applies its state to the document. The stack is
//        byte-for-byte identical to before the eviction, minus the
//        evicted commands.
//
//    (d) Finally the evicted real commands are deleted. That is the
//        point of the exercise: their memory is released.
//
//  Step (c) needs one extra piece of cooperation from the command
//  classes. Most commands in this project guard their first redo
//  with a "the caller already applied it" flag (see
//  LayerCommand::m_firstRedo and friends). If the guard swallows
//  that first redo, the flag would stay set and the NEXT real redo
//  (after a user undo) would be swallowed too -- the redo would
//  silently do nothing. The commands therefore implement
//  IUndoFirstRedo::consumeFirstRedo(), which clears the flag
//  WITHOUT applying anything.
//
//  Commands without such a flag (the plain state commands such as
//  ImageEditCommand or FilterCommand) re-apply their state on every
//  redo, which would mutate the document during the rebuild. They
//  therefore also implement IUndoFirstRedo::consumeFirstRedo() --
//  for them the hook is a no-op, but their redo() is idempotent in
//  the sense that it writes the state the document is already in...
//  which is NOT true for the SURVIVORS that are not the top one.
//
//  => The rebuild therefore only re-pushes the survivors in replay
//     mode, and every real command must implement
//     IUndoFirstRedo so that the swallowed first redo leaves the
//     command in a state where the NEXT redo works correctly.
// =============================================================

#include <QUndoStack>
#include <QVector>

namespace vistella {

// A command that knows how many bytes it holds.
//
//   Implemented with dynamic_cast / qobject_cast on the pushed
//   command, so the push call sites do not have to change.
class IUndoMemoryFootprint
{
public:
    virtual ~IUndoMemoryFootprint() = default;
    // Bytes held by this command, excluding the QUndoCommand overhead.
    virtual size_t memoryFootprint() const = 0;
};

// A command whose FIRST redo() is special.
//
//   Every command in this project is pushed AFTER its effect has
//   already been applied to the document, so the redo() that
//   QUndoStack::push() triggers must not apply the state a second
//   time. The commands that need this implement it with a flag.
//
//   The undo stack's eviction rebuilds the stack by replaying the
//   surviving commands. During that rebuild the first redo() is
//   swallowed (so the document is not touched), and the command has
//   to be told that its "first redo" was consumed -- otherwise the
//   next real redo would be swallowed as well and the redo would
//   silently do nothing.
//
//   consumeFirstRedo() must therefore clear the command's first-redo
//   flag WITHOUT applying any state.
class IUndoFirstRedo
{
public:
    virtual ~IUndoFirstRedo() = default;
    virtual void consumeFirstRedo() = 0;
};

class UndoStack : public QUndoStack
{
    Q_OBJECT

public:
    // The lightweight wrapper that lives in the QUndoStack.
    //
    //   It forwards undo()/redo() to the real command and reports the
    //   real command's memory footprint. It OWNS the real command, so
    //   the real command is released exactly once, when the guard
    //   dies.
    class ReplayGuard;

    explicit UndoStack(QObject* parent = nullptr);
    ~UndoStack() override;

    // The memory budget in bytes. 0 = unlimited (the default, so an
    // existing stack keeps behaving exactly as before until the owner
    // sets a budget).
    void    setMemoryBudgetBytes(size_t bytes);
    size_t  memoryBudgetBytes() const { return m_budgetBytes; }

    // Bytes currently held by the undo history.
    size_t  memoryUsedBytes() const { return m_usedBytes; }

    // Push a command, accounting for its footprint and evicting the
    // oldest commands if the budget is exceeded.
    //
    //   This SHADOWS QUndoStack::push() on purpose: QUndoStack::push
    //   is not virtual, so the owner has to hold an UndoStack* (not a
    //   QUndoStack*) for the accounting to run at all. Callers that
    //   go through a QUndoStack* would bypass the budget.
    void push(QUndoCommand* cmd);

    // Number of commands that were dropped because of the budget.
    int evictedCommandCount() const { return m_evictedCount; }

    // The real command behind the wrapper at the given index, or
    // nullptr. Exposed for tests and for the history view.
    const QUndoCommand* innerCommand(int index) const;

signals:
    // Emitted after commands were dropped from the bottom of the
    // stack. The owner must shift its saved-index marker by the same
    // amount, otherwise the document's dirty state becomes wrong.
    void indexShifted(int count);

private:
    // Footprint of a command, 0 when it does not report one.
    static size_t footprintOf(const QUndoCommand* cmd);

    // Drop commands from the bottom until the total is back under the
    // budget. Returns how many were dropped.
    int evictUntilUnderBudget();

    // Rebuild the stack keeping only the NEWEST keepCount commands.
    // Returns the number of dropped commands.
    int evictOldest(int keepCount);
    // The real commands, in stack order. The QUndoStack itself only
    // holds ReplayGuard wrappers (see the file header).
    QVector<QUndoCommand*> m_inners;
    QVector<size_t>        m_footprints;

    // Called by a ReplayGuard when it is destroyed.
    //
    //   QUndoStack::push() deletes the commands above the current index
    //   ("the redo tail") when a new command is pushed after an undo.
    //   Those guards are destroyed by Qt, so the stack has to learn
    //   about it -- otherwise m_inners and the real stack drift apart
    //   and the eviction math is computed on commands that are gone.
    //
    //   The guard owns the real command, so this is also where the
    //   real command is released -- unless the guard was detached
    //   during the eviction rebuild, in which case the command was
    //   handed over to a fresh guard and has to survive.
    void onGuardDestroyed(QUndoCommand* inner, bool detached);

    size_t m_budgetBytes = 0;
    size_t m_usedBytes   = 0;
    int    m_evictedCount = 0;
};

}   // namespace vistella

#endif   // UNDOSTACK_H
