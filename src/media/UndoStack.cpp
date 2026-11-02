#include "UndoStack.h"

#include <QUndoCommand>

#include <functional>

namespace vistella {

// The lightweight wrapper that actually lives in the QUndoStack.
//
//   It forwards undo()/redo() to the real command and reports the real
//   command's memory footprint.
//
//   OWNERSHIP: the guard owns the real command. When the guard is
//   destroyed it releases the real command and tells the owner, so
//   the owner never holds a dangling pointer.
//
//   "Replay mode" swallows the FIRST redo(). That is what the
//   eviction rebuild uses: the survivors are pushed back without
//   re-applying their state to the document.
class UndoStack::ReplayGuard : public QUndoCommand,
                               public IUndoMemoryFootprint
{
public:
    // The callback receives the real command and whether the guard was
    // detached (the real command then survives the guard).
    using DestroyedCallback = std::function<void(QUndoCommand*, bool)>;

    ReplayGuard(QUndoCommand* inner, bool replayMode,
                DestroyedCallback onDestroyed = DestroyedCallback())
        : QUndoCommand(inner ? inner->text() : QString())
        , m_inner(inner)
        , m_replay(replayMode)
        , m_onDestroyed(std::move(onDestroyed))
    {
        if (inner) {
            setText(inner->text());
        }
    }

    ~ReplayGuard() override
    {
        if (m_inner && m_onDestroyed) {
            m_onDestroyed(m_inner, m_detached);
            m_inner = nullptr;
        }
    }

    void undo() override
    {
        if (m_inner) m_inner->undo();
    }

    void redo() override
    {
        if (!m_inner) return;
        if (m_replay) {
            // The rebuild: swallow this redo so the document is not
            // touched. The real command still has to forget that its
            // first redo is pending, otherwise the next real redo
            // would be swallowed too.
            m_replay = false;
            if (auto* fr = dynamic_cast<IUndoFirstRedo*>(m_inner)) {
                fr->consumeFirstRedo();
            }
            return;
        }
        m_inner->redo();
    }

    int id() const override
    {
        return m_inner ? m_inner->id() : -1;
    }

    bool mergeWith(const QUndoCommand* other) override
    {
        if (!m_inner || !other) return false;
        const auto* guard = dynamic_cast<const ReplayGuard*>(other);
        if (!guard || !guard->m_inner) return false;
        return m_inner->mergeWith(guard->m_inner);
    }

    size_t memoryFootprint() const override
    {
        if (!m_inner) return 0;
        const auto* fp = dynamic_cast<const IUndoMemoryFootprint*>(m_inner);
        return fp ? fp->memoryFootprint() : 0;
    }

    // Give up the real command WITHOUT releasing it.
    //
    //   The eviction rebuild takes the surviving real commands out of
    //   the stack and pushes them back as fresh guards. The old guards
    //   must then die without releasing the real commands -- otherwise
    //   the rebuild would delete the very commands it wants to keep.
    //
    //   The owner still has to learn that the old guard is gone (the
    //   bookkeeping is keyed by the guard), so the callback is NOT
    //   cleared -- it just stops releasing the command.
    void detachInner()
    {
        m_detached = true;
    }

private:
    QUndoCommand*       m_inner = nullptr;
    bool                m_replay = false;
    bool                m_detached = false;
    DestroyedCallback   m_onDestroyed;
};

UndoStack::UndoStack(QObject* parent)
    : QUndoStack(parent)
{
}

UndoStack::~UndoStack()
{
    // The guards own the real commands, and ~QUndoStack destroys the
    // guards -- which releases the real commands through the callback.
    // m_inners is only bookkeeping; nothing has to be deleted here.
}

void UndoStack::setMemoryBudgetBytes(size_t bytes)
{
    if (m_budgetBytes == bytes) return;
    m_budgetBytes = bytes;
    if (m_budgetBytes == 0) return;   // unlimited
    evictUntilUnderBudget();
}

size_t UndoStack::footprintOf(const QUndoCommand* cmd)
{
    if (!cmd) return 0;
    const auto* fp = dynamic_cast<const IUndoMemoryFootprint*>(cmd);
    return fp ? fp->memoryFootprint() : 0;
}

const QUndoCommand* UndoStack::innerCommand(int index) const
{
    if (index < 0 || index >= m_inners.size()) return nullptr;
    return m_inners.at(index);
}

void UndoStack::onGuardDestroyed(QUndoCommand* inner, bool detached)
{
    // The guard is gone. It was the owner of the real command, so the
    // command goes with it -- unless the guard was detached, in which
    // case the command was handed over to a fresh guard during the
    // eviction rebuild and has to survive.
    if (detached) {
        // The command lives on in a fresh guard. The bookkeeping entry
        // stays where it is -- it is keyed by the command, not by the
        // guard, so the handover does not move anything.
        return;
    }

    const int idx = m_inners.indexOf(inner);
    if (idx < 0) {
        // Already accounted for.
        return;
    }
    const size_t footprint = m_footprints.at(idx);

    m_inners.removeAt(idx);
    m_footprints.removeAt(idx);

    m_usedBytes = (m_usedBytes >= footprint) ? (m_usedBytes - footprint) : 0;
    delete inner;
}

void UndoStack::push(QUndoCommand* cmd)
{
    if (!cmd) return;

    const size_t footprint = footprintOf(cmd);

    // QUndoStack::push() calls redo() on the command. That is the
    // contract every command in this project relies on (a command
    // whose state was already applied by the caller must make its
    // first redo a no-op -- see LayerCommand::selfApplied()).
    //
    // The real command is kept in m_inners; the QUndoStack holds a
    // guard that owns it and forwards to it. The guard is NOT in
    // replay mode here, so the real redo() runs exactly as it used to.
    m_inners.append(cmd);
    m_footprints.append(footprint);
    m_usedBytes += footprint;
    QUndoStack::push(new ReplayGuard(cmd, /*replayMode=*/false,
                                     [this](QUndoCommand* dead, bool detached) {
                                         onGuardDestroyed(dead, detached);
                                     }));

    if (m_budgetBytes > 0 && m_usedBytes > m_budgetBytes) {
        evictUntilUnderBudget();
    }
}

int UndoStack::evictUntilUnderBudget()
{
    if (m_budgetBytes == 0) return 0;

    // How many of the NEWEST commands fit into the budget?
    //
    //   The oldest commands go first. The newest command is never
    //   dropped for the budget alone: evicting everything else to
    //   make room for a single huge command would leave the user with
    //   no history at all. That command stays and the stack ends up
    //   over budget -- the documented soft-limit behaviour.
    int keepCount = 0;
    size_t kept = 0;
    for (int i = m_inners.size() - 1; i >= 0; --i) {
        const size_t f = m_footprints.at(i);
        if (kept + f > m_budgetBytes) break;
        kept += f;
        ++keepCount;
    }

    if (keepCount >= m_inners.size()) {
        // Everything fits -- nothing to do.
        return 0;
    }
    if (keepCount == 0) {
        // A single command is bigger than the whole budget. Dropping
        // the rest would leave the user with no history at all, so the
        // history is kept as it is.
        return 0;
    }

    return evictOldest(keepCount);
}

int UndoStack::evictOldest(int keepCount)
{
    if (keepCount >= m_inners.size()) return 0;
    if (keepCount < 0) keepCount = 0;

    const int oldCount = m_inners.size();
    const int dropCount = oldCount - keepCount;

    // Where the stack was before the rebuild. The commands below the
    // index are the ones the user had UNDONE -- they have to stay
    // redoable afterwards, so the index is restored at the end.
    const int oldIndex = index();

    // The QUndoStack can only be emptied by removing its commands.
    // Marking them obsolete makes undo() delete them WITHOUT calling
    // their undo() -- so the document is not touched at all.
    //
    // The guards of the SURVIVORS are detached first: they have to die
    // without releasing the real commands, because the rebuild pushes
    // the survivors back as fresh guards. The guards of the DROPPED
    // commands keep their inner, so their destruction releases them.
    //
    // The survivors are captured BEFORE the drain: the drain destroys
    // the guards of the dropped commands, and their destruction
    // removes those commands from m_inners -- which shifts the
    // indices of the survivors.
    QVector<QUndoCommand*> survivors;
    survivors.reserve(keepCount);
    for (int i = dropCount; i < oldCount; ++i) {
        QUndoCommand* inner = m_inners.at(i);
        if (!inner) continue;
        survivors.append(inner);

        // The survivors are the NEWEST commands, so they sit at the
        // TOP of the stack -- stack index i, matching m_inners index i
        // (the oldest commands are at the bottom of both).
        auto* guard = const_cast<ReplayGuard*>(
            static_cast<const ReplayGuard*>(command(i)));
        if (guard) guard->detachInner();
    }

    for (int i = 0; i < count(); ++i) {
        const QUndoCommand* guard = command(i);
        if (!guard) continue;
        // command() hands out a const pointer, but setObsolete() is
        // the supported way to retire a command.
        const_cast<QUndoCommand*>(guard)->setObsolete(true);
    }
    while (count() > 0) {
        undo();
    }

    // The stack is empty now, so the undo limit can be lowered --
    // that is the only moment QUndoStack accepts a new limit.
    if (undoLimit() > 0) {
        setUndoLimit(keepCount > 0 ? keepCount : 1);
    }

    // Push the survivors back. The guards are created in replay mode:
    // their first redo() is swallowed, so no command re-applies its
    // state to the document.
    for (QUndoCommand* inner : survivors) {
        if (!inner) continue;
        QUndoStack::push(new ReplayGuard(inner, /*replayMode=*/true,
                                         [this](QUndoCommand* dead, bool detached) {
                                             onGuardDestroyed(dead, detached);
                                         }));
    }

    // Restore the index.
    //
    //   The index counts the APPLIED commands. The evicted commands
    //   were at the BOTTOM of the stack, so they were applied too --
    //   removing them does not change how many commands are applied.
    //   The index therefore stays where it was, clamped to the new
    //   (smaller) stack.
    //
    //   If the user had undone some commands, those are still
    //   redoable: they were pushed back as well, so the index still
    //   points at the same place in the history.
    const int newIndex = oldIndex < count() ? oldIndex : count();
    if (newIndex != index()) {
        setIndex(newIndex);
    }

    // The evicted commands are gone: their guards died during the
    // drain and released them (onGuardDestroyed updated the
    // bookkeeping and subtracted their bytes).
    m_evictedCount += dropCount;

    // The stack index moved down by dropCount; the owner has to move
    // its saved-index marker by the same amount.
    emit indexShifted(dropCount);

    return dropCount;
}

}   // namespace vistella
