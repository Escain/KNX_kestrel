# Brainstorm round 2 — Qt / UI architect

Reviewed: round1-protocol.md, round1-model.md, round1-quality.md, against round1-ui.md.

## 1. What I endorse
- **Model:** value-type entities, `Tx`-only mutation, one `ChangeSet` per commit, derived "needs download". Ideal for
  item models: rows hold `Id`s, never pointers, so a stale row cannot crash.
- **Model:** `Diagnostic.fixIt` as a transaction. The Problems dock gets an undoable "Fix" button for free.
- **Protocol:** sans-IO protocol, and a `DownloadPlan` that can be printed and diffed. The Operations dock will offer a
  "Preview plan" before every download, rendered from the same plan the CLI prints.
- **Protocol:** an in-process simulator. Robot GUI suites run against `kestrel-sim` exactly like the CLI suites.
- **Quality:** Robot tests start CLI-first, with GUI automation thin and compiled in only behind `KESTREL_ENABLE_AUTOMATION`.
- **Quality:** requirement IDs in test names (QML too: `test_FR_DEV_1__…`), required `qmllint`, shared `kestrel_app` services.

## 2. Rulings on the known tensions
**(a) Qt-free vs QtCore in the core.** Both sides get a firm boundary:
- **Zero Qt:** `kestrel_knx`, `kestrel_dpt`, the sim core, and `kestrel_base` (Result/Error, ids).
  Quality's table lists `kestrel_knx` and `kestrel_base` as QtCore; that is overruled.
- **QtCore allowed, QtGui never:** `kestrel_model`, `kestrel_catalog`, `kestrel_persist`, `kestrel_ets`, `kestrel_app`.
  Model's argument about `QXmlStreamReader`, `QString` and `QSaveFile` wins for these.
- The UI only needs QtCore types at its boundary, so this split costs the UI nothing.

**(b) Undo source of truth: the model's `History`, not QUndoStack.** I withdraw my round-1 `UndoCommandAdapter`.
- `ProjectSession` binds the Undo/Redo QActions to `History::canUndo()`, `undoText()` and `cleanIndex`.
- `mergeKey` replaces `QUndoCommand::mergeWith`. My editors commit on `editingFinished`, so the 1 s merge window rarely fires.
- A history panel, if ever built, is a QML `HistoryListModel`. No `UndoMirror`, no `QUndoView`.

**(c) ChangeSet → QAbstractItemModel without resets.** Each tree model keeps a **shadow index**: a lightweight node tree
of `{Id, parentNode, children vector}`. `data()` reads the live `Project` by Id. `ProjectNotifier` delivers each ChangeSet
**synchronously on the GUI thread** (a direct connection, never queued). The model then applies it to the shadow:
- **Removed:** the row is still in the shadow, so we can locate it → `beginRemoveRows` → drop the node → `endRemoveRows`.
  The live entity is already gone, which is fine because `data()` returns empty for unknown Ids.
- **Added:** look up the parent and the sorted position in `Project` → `beginInsertRows`.
- **Modified:** if the FieldMask touches `parent`/`order`/`number` (reparenting, renumbering a line), use `beginMoveRows`.
  Otherwise use `dataChanged` limited to the roles mapped from the FieldMask.
- **Ask of the model architect:** list cascaded removals children-first; give FieldMask a "structural" bit.
- **Bulk changes:** more than 500 structural changes for one model (CSV import, undo of a big macro) → `beginResetModel`.
  `ViewStateStore` saves the expanded and selected Ids first and restores them afterwards. That is the only allowed reset.
- **ParameterPageModel** stays on my round-1 keyed diff over the evaluator's `ActiveSet`.

**(d) Error handling: vendored `tl::expected` (CC0), aliased as `kestrel::Result<T>`** in a **std-only** `kestrel_base`.
- A home-grown Result would be a second, less-tested copy of `tl::expected` that we then have to maintain.
- `kestrel::Error = {ErrorCode code; std::vector<std::string> args; std::vector<std::string> context}`.
  It carries **no message text**: Quality's `QString message` would break (a), and it could not be translated.
- `kestrel_viewmodels` holds an `ErrorText::describe(const Error&)` table that uses `tr()`, the same scheme as
  `Diagnostic.messageId + args`.
- Async results arrive as `QFuture<Result<T>>`. VMs continue on the GUI thread with `.then(this, …)` and show the result
  through a `NotificationService` (an inline banner in the dock). Use a modal `QMessageBox` only when the user must decide.

## 3. Other conflicts and resolutions
- **Quality's single `kestrel_ui` target (Widgets + Quick + VMs)** → keep my split: `kestrel_viewmodels` (Core/Gui/Qml)
  and `kestrel_shell` (Widgets, QuickWidgets). That lets VM tests run without QuickWidgets.
  `kestrel_app` (QtCore, QObject allowed, no `QML_*` macros) holds the services the CLI also needs: ProjectSession and
  BusSession. `kestrel_viewmodels` exposes them to QML with `QML_FOREIGN` singleton wrappers, so the CLI never links Qt::Qml.
- **QML modules:** keep three URIs (`Kestrel.ViewModels`, `.Controls`, `.Views`), not one `Kestrel.Ui`: per-module lint and fakes.
- **Minimum Qt version:** I accept `REQUIRES 6.5` (NFR-1), with CI on 6.8.2. UI code may only use ≥6.5 APIs.
  `QQmlComponent::loadFromModule`, `QStyleHints::colorScheme` and `TreeView` all qualify.
- **Automation protocol:**
  JSON-RPC 2.0 framing (Quality's `{id,cmd,args}` shape plus standard errors); QAction objectName = `ActionRegistry` id
  (`device.download.full`); Quality's `wait-until` adopted.
- **Coverage:** "No gate on `kestrel_ui`" is fine for the shell. `kestrel_viewmodels` holds the UI logic, so it gets ≥ 75 %.
- **Group monitor:** Protocol's `GroupMonitorModel` becomes my `TelegramStore` + `TelegramModel`. BusThread → GUI crosses an
  SPSC ring drained by a timer, never one queued signal per telegram.
- **Calculated-parameter JS (protocol risk 2):** it needs its own sandboxed `QJSEngine` on a worker. It must **never** share the
  UI `QQmlEngine`, which runs on the GUI thread and has every service singleton in reach.
- **ZIP (outside my area):** I favour Model's vendored minizip-ng over Protocol's in-house reader, because it is less security-sensitive code for us to own.

## 4. Missing — this will bite us
1. **Commissioning writes vs undo.** IA programmed, `downloadedHash` and serial are written by bus operations. They need a
   **non-undoable transaction kind** that still emits a ChangeSet and leaves `History` intact. Otherwise Ctrl+Z "un-downloads" a device.
2. **Edits during a running download.** The plan snapshots the `DeviceImage`. Edits stay allowed, the editor shows a banner,
   and the device ends up "needs download". This must be specified, not discovered.
3. **Evaluator and catalog latency on the GUI thread.**
   `program()` parse (50–200 ms) → DeviceEditor loading state. `DynamicEvaluator` < 5 ms per change, else async with a
   generation counter dropping stale results. Catalog FTS on the worker connection, search debounced 150 ms.
4. **Shared `EntityRef`.** One type in `kestrel_model` (kind + `Id`), wrapped once as a `Q_GADGET` for QML, selection,
   drag-and-drop mime data and automation paths. No second ref type in the UI.
5. **Day-1 spikes in CI:**
   QQuickWidget under `offscreen` QPA + `software` backend; `QAccessible::setActive(true)` from the automation server
   (QML accessible interfaces with no AT bridge); `QQuickWidget::setContent` ownership. If these fail, GUI suites need Xvfb.
