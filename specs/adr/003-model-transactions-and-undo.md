# ADR-003 — Value-type model, transactions, a single undo history

Status: Accepted

**Decision.**
- Entities are value types referenced by id.
- Mutation happens only through `Project::edit`, whose `Tx` records reversible primitive ops (no hand-written undo).
- Every commit, undo and redo produces one ordered `ChangeSet`.
- `model::History` is the only undo stack. The GUI binds QActions to it and uses no `QUndoStack`. Bus-sourced commissioning
  facts go through `recordState()`: they are not undoable, but they do mark the project dirty.
- The project is mutated on the GUI thread only, and workers use snapshots.

**Rejected.** A QObject entity tree (memory use, signal storms, thread affinity). `QUndoStack` as the source of truth (it lives in QtGui
and is unavailable to the CLI). Two undo stacks.
