# Brainstorm round 1 — Qt / UI architecture (Kestrel)

Author role: Qt/UI architect. Status: proposal for discussion. Targets Qt 6.8.2 (checked against the
installed `/usr/include/x86_64-linux-gnu/qt6` headers).

## 1. Main window composition

**Shell = widgets, content = QML.** `MainWindow : QMainWindow` owns menus, toolbars, status bar, docks and
a central `DocumentArea : QTabWidget` (documentMode, closable, movable tabs). These are all plain widgets.
Every dock body and every document tab is one `QmlHost : QQuickWidget`.

| Area | objectName | Content (QML root) |
|---|---|---|
| Left dock | `dock.buildings` | `BuildingsPanel` — TreeView Building/Floor/Room/devices |
| Left dock (tabbed) | `dock.topology` | `TopologyPanel` — Area/Line/Device tree, address + status badges |
| Left dock (tabbed) | `dock.groupAddresses` | `GroupAddressPanel` — main/middle/sub tree, DPT column |
| Right dock | `dock.properties` | `PropertiesPanel` — context-sensitive form for the current selection |
| Right dock (tabbed) | `dock.catalog` | `CatalogPanel` — search field + manufacturer/product tree, drag source |
| Bottom dock | `dock.problems` | `ProblemsPanel` — validation results (dup address, DPT mismatch), click-to-navigate |
| Bottom dock (tabbed) | `dock.busConnection` | `BusConnectionPanel` — discovered interfaces, connect, status |
| Bottom dock (tabbed) | `dock.operations` | `OperationsPanel` — download/programming queue with progress and cancel |
| Central tabs | `doc.<kind>.<uuid>` | `DeviceEditor` (parameters + comm objects), `GroupMonitor`, `BusMonitor`, `Welcome` |

Menus: File, Edit (Undo/Redo/Cut/Copy/Paste/Delete/Find), View (dock toggles via `QDockWidget::toggleViewAction`,
Workspaces), Project, Commissioning (Program address, Download ▸ Full/Parameters/Tables, Device info, Unload),
Bus (Connect, Disconnect, Group monitor, Bus monitor), Help. Toolbars: Main (New/Open/Save/Undo/Redo),
Commissioning, Bus. Every menu/toolbar entry is a `QAction` from the `ActionRegistry` (§2).

**Central tabs are editors, docks are navigators.** Opening a device (double-click/Enter in any tree) opens or
focuses its `DeviceEditor` tab. A single-click opens a *preview tab* (italic title, replaced by the next preview,
VS Code style) to avoid tab explosion; edits pin it. Hard cap of ~10 live editor tabs: older inactive tabs drop their
`QQuickWidget` (keep only the title + view state) and recreate it on activation.

**Workspaces.** `WorkspaceManager` stores named layouts (`Design`, `Commissioning`, `Diagnostics`, user-defined)
as `QMainWindow::saveState(kLayoutVersion)` + `saveGeometry()` in `QSettings`, plus per-view state (tree column
widths, expanded nodes, splitter positions) in a `ViewStateStore` keyed by host objectName. Rules: every dock and
toolbar has a stable objectName (restoreState silently ignores unnamed ones); bump `kLayoutVersion` whenever docks
are added/renamed; if `restoreState` returns false, apply the built-in default layout. "View ▸ Reset layout" always exists.

**How a dock hosts QML.** `QmlDock : QDockWidget` contains a `QmlHost`. `QmlHost` is constructed with the single
shared engine: `QQuickWidget(QQmlEngine*, QWidget*)` (verified in the 6.8.2 header). Resize mode
`SizeRootObjectToView`. Content is created **lazily** on first `visibilityChanged(true)` — tabified/hidden docks cost
nothing at startup. The 6.8.2 `QQuickWidget` has **no** `setInitialProperties`/`loadFromModule` (both are on
`QQuickView` only), so `QmlHost` does: `QQmlComponent::loadFromModule("Kestrel.Views", "TopologyPanel")` →
`createWithInitialProperties({{"vm", vm}})` → `QQuickWidget::setContent(url, component, item)`. This makes
`required property TopologyPanelVM vm` work and keeps views free of context properties. (Spike item: confirm the
ownership semantics of `setContent` and wrap them once in `QmlHost`.)

### QQuickWidget vs QQuickWindow + createWindowContainer — honest trade-offs

- `QQuickWidget` renders the scene into an offscreen texture that the widget backing store composites via RHI.
  Costs: one extra composition pass; **always renders on the GUI thread** (no threaded render loop), so heavy QML
  directly steals event-loop time; once a top-level contains a QQuickWidget the whole window goes through
  RHI composition (OpenGL on Linux by default). Benefits: it is a normal widget — dock floating/tabbing/stacking,
  overlapping widgets, `QWidget::grab()`, style/palette and focus chain all behave.
- `createWindowContainer` gets the threaded render loop, but native child windows break things that matter in a
  dock-heavy app: no overlap/stacking with sibling widgets, flicker and resource recreation when a dock floats
  (reparent recreates the native window), keyboard focus hand-off and `QAction` shortcuts are unreliable, and drag
  and drop / tooltips across the boundary are quirky. **Decision: QQuickWidget everywhere** (as NFR-2 says);
  `QmlHost` is the one place to swap in a container for a single view if profiling ever demands it.
- All QQuickWidgets in one top-level must use the same graphics API; we never mix. CI/VMs without GPU: run with
  `QT_QUICK_BACKEND=software` (QQuickWidget supports the software adaptation) and `QT_QPA_PLATFORM=offscreen`.
- **Popups are confined to the QQuickWidget rectangle.** A QML `ComboBox`/`Menu` in a 150 px tall dock gets
  clipped. Mitigations: context menus are native `QMenu`s built from action IDs (`ContextMenus.popup(ids, pos)`),
  long lists live in the large central editor, ComboBox popups use bounded height and flip.
- **No QML `Shortcut` items.** Shortcuts belong to `QAction`s on the main window. Keys first reach the QML focus item
  via `ShortcutOverride`, so a focused `TextField` correctly keeps Ctrl+Z/Ctrl+C for itself.
- Cross-view drag and drop (Catalog → Topology, GA → comm object, device → room) must use `Drag.dragType:
  Drag.Automatic` (real `QDrag` with mime `application/x-kestrel-refs` holding JSON entity refs). `Drag.Internal`
  does not cross QQuickWidget boundaries.

### One shared engine, startup cost, module registration

`QmlEngineProvider` creates exactly one `QQmlEngine` before any view, after `QQuickStyle::setStyle("Fusion")`.
Shared engine ⇒ one type registry, one component cache (second instance of a component is near-free), one set of
`QML_SINGLETON` services, one `engine->retranslate()`. The engine is destroyed after all hosts (owned by `Application`,
hosts deleted first in `MainWindow` destructor) — wrong order = crash on exit.
Startup budget: window visible < 1.0 s, first panel interactive < 1.5 s on the dev box. Levers: lazy hosts,
`qt_add_qml_module` ahead-of-time compilation (qmlcachegen; QML script compiler where it applies), avoiding
untyped JS so the compiler can compile bindings, and `qmllint` warnings treated as errors in CI.
Registration: C++ types use `QML_ELEMENT` / `QML_NAMED_ELEMENT` / `QML_UNCREATABLE` / `QML_SINGLETON` (+ static
`create(QQmlEngine*, QJSEngine*)` that returns the service owned by `AppContext`, with
`QJSEngine::setObjectOwnership(CppOwnership)`). No `qmlRegisterType`, no `setContextProperty`.
`qt_standard_project_setup(REQUIRES 6.8)` gives the `qrc:/qt/qml` resource prefix policy. QML modules are static
libraries; the app links the plugin targets and uses `Q_IMPORT_QML_PLUGIN`.

## 2. C++ ↔ QML boundary (MVVM)

Layers: `kestrel_core` (QtCore only: model, core commands, evaluator, protocol) → `kestrel_viewmodels`
(QtCore/Gui/Qml, **no Quick, no Widgets**) → QML views (pure presentation) and `kestrel_shell` (widgets).

- **Services (`QML_SINGLETON`, owned by `AppContext`)**: `ProjectSession` (open project, path, dirty, autosave),
  `SelectionService`, `ActionRegistry` (exposed to QML as `Actions`), `NavigationService` (open editor, reveal in
  tree), `BusSession` (connection state, wraps core bus), `ThemeService` (`Theme`), `ViewStateStore`.
- **Item models** (`QAbstractItemModel`, stable role enums, `roleNames()`): `BuildingTreeModel`, `TopologyTreeModel`,
  `GroupAddressTreeModel`, `CatalogTreeModel` (+ `CatalogFilterProxy`, recursive filtering), `CommObjectTableModel`,
  `ParameterBlockTreeModel`, `ParameterPageModel`, `TelegramModel`, `ProblemsModel`, `InterfaceListModel`,
  `OperationListModel`. Common roles: `ref` (entity ref), `display`, `iconName`, `address`, `statusFlags`, `toolTip`.
  Models listen to fine-grained core change notifications and emit `beginInsertRows`/`dataChanged` — **never
  `modelReset` on an edit** (it kills TreeView expansion, selection and scroll). Every model is run under
  `QAbstractItemModelTester` in tests. Qt Quick `TreeView` + `TreeViewDelegate` (6.8) virtualizes, so 10 000 GAs is fine.
- **ViewModels** (one per view, `QML_ELEMENT` + `QML_UNCREATABLE`, created by the shell, passed as required
  property): `BuildingsPanelVM`, `TopologyPanelVM`, `GroupAddressPanelVM`, `CatalogPanelVM`, `PropertiesPanelVM`,
  `DeviceEditorVM`, `GroupMonitorVM`, `BusConnectionVM`, `OperationsPanelVM`. They expose `Q_PROPERTY`s (models,
  filter text, current item) and `Q_INVOKABLE` intent methods (`renameItem(ref, name)`, `createLine(areaRef)`,
  `linkGroupAddress(coRef, gaRef)`). QML never sees core types; refs cross as a `Q_GADGET` `EntityRef {kind, uuid}`.
- **Selection.** Docks have different models, so a shared `QItemSelectionModel` is impossible. `SelectionService`
  holds `QList<EntityRef> selection`, `EntityRef current`, and `origin` (host objectName). A tree view pushes on
  user selection; other views react only if `origin != self` (no feedback loops) and decide what to show
  (Properties shows the current entity; Topology reveals a device selected in Buildings only if "link with
  selection" is on). Back/Forward navigation history lives here too.
- **Commands / undo.** Core defines `kestrel::core::Command` (apply/revert, mergeable id); one `QUndoStack`
  (QtGui in Qt 6, so allowed in viewmodels) per project in `ProjectSession`, filled through `UndoCommandAdapter :
  QUndoCommand`. VM intent methods build core commands and push them; slider/spinbox drags merge via `mergeWith`;
  multi-step operations use macros. QML never touches the stack. Undo/Redo actions come from
  `QUndoStack::createUndoAction`.
- **Actions from QML.** `ActionRegistry` maps string IDs (`project.save`, `device.download.full`, `ga.newMiddleGroup`)
  to `QAction`s; QML uses `Actions.trigger("device.download.full")` and binds `Actions.state("edit.undo")` → an
  `ActionProxy` QObject (`text`, `enabled`, `checked`, `iconName`, `shortcutText`). Action enablement is computed
  in C++ from the selection, so menu, toolbar, context menu and QML buttons can never disagree.

## 3. Dynamic parameter dialog

`DeviceEditor.qml` = SplitView: left `TreeView` over `ParameterBlockTreeModel` (only visible blocks), right
page over `ParameterPageModel`, bottom/second tab `TableView` over `CommObjectTableModel` (number, name, function,
linked GAs, length, C/R/W/T/U/I flags, DPT).

- The core evaluator (other architect) owns `choose/when` semantics. `DeviceEditorVM` asks it for the *effective*
  layout after each change and diffs the result **by stable parameter-ref id**, emitting minimal row
  insert/remove/dataChanged. A reset would lose focus and scroll mid-edit — the #1 UX bug of such dialogs.
- `ParameterPageModel` is a flat list; roles: `rowKind` (Separator, Heading, Parameter, Info), `editorType`
  (Enum, Radio, Checkbox, Integer, Float, Text, Time, Date, IpAddress, Color, Slider, Hex, ReadOnly), `label`,
  `value`, `defaultValue`, `minimum`, `maximum`, `step`, `unit`, `options` (list of {value, text}), `readOnly`,
  `modified`, `validationMessage`, `helpText`, `indent`. Mapping from DatapointType/ParameterType + UI hints to
  `editorType` is C++ only.
- QML: `ListView` with `DelegateChooser { role: "editorType" }` (Qt.labs.qmlmodels) choosing one small, typed
  component per editor from `Kestrel.Controls/Parameters/` (`EnumEditor`, `NumberEditor`, `TimeEditor`, `IpEditor`
  …). Each editor only calls `vm.setValue(row, v)`; commits on `editingFinished`/`activated`, not per keystroke.
  Validation is done in C++ and reflected via `validationMessage`. Extras: "show modified only", "reset to default",
  parameter search across all blocks (VM jumps to the block and row).
- Comm objects visibility follows the same evaluator (FR-DEV-2); GAs are linked by dropping a GA on a row or via
  a picker; the table updates through `dataChanged` only.

## 4. Group monitor at high telegram rates

- Bus thread pushes decoded-header telegrams into an SPSC ring (`TelegramSink`); the GUI side drains it on a
  `QTimer` at 20–30 Hz. **One** `beginInsertRows` per tick, never one signal per telegram.
- `TelegramStore`: fixed-capacity ring (default 100 000, configurable), POD records ~48 B (timestamp, src, dst,
  APCI, flags, ≤14-byte small-buffer payload) ≈ 5 MB. When full, the tick does one `beginRemoveRows(0, k-1)` then
  the append.
- `TelegramModel : QAbstractTableModel` over the store. DPT text, names of source device / GA are resolved
  **lazily in `data()`** with an LRU cache — ingest never formats strings.
- Filters (`TelegramFilter`: GA ranges, source, APCI, text) are applied incrementally by C++ into an index vector;
  no `QSortFilterProxyModel` on 100k rows. Pause freezes the visible snapshot while capture continues; "follow
  tail" autoscroll toggle; export CSV is a C++ action on a worker thread.
- QML: `TableView` + `HorizontalHeaderView`, `reuseItems: true`, fixed row height, `Text` delegates (no Controls,
  no per-cell JS, no animations) — remember QQuickWidget renders on the GUI thread. Acceptance: 5 000 tel/s from
  the simulated bus for 60 s keeps the UI responsive (event-loop latency < 50 ms), measured in a benchmark test.
- Group read/write (FR-BUS-6) sits in a toolbar above the table: GA picker + DPT-aware value editor reused from
  the parameter editors.

## 5. Theming, icons, i18n

- **Style: Fusion for both worlds.** `QApplication::setStyle("Fusion")` + `QQuickStyle::setStyle("Fusion")`.
  Qt Quick Fusion draws from the application `QPalette`, so docks/menus (widgets) and panels (QML) match, light and
  dark. `ThemeService` computes the palette (System via `QStyleHints::colorScheme()`, Light, Dark) and sets it once
  on `QApplication`; QML picks it up automatically. Material/Universal look alien next to widget chrome; Basic looks
  unfinished; native platform styles are not in our toolchain list and make screenshots non-deterministic.
- `Theme` singleton adds what the palette lacks: spacing scale, row heights, semantic status colours
  (needs-download, error, programmed) derived from palette so dark mode works.
- **Icons:** one bundled SVG theme `kestrel` (index.theme in resources) built on a permissive set (Lucide, ISC)
  plus our KNX glyphs (line, coupler, GA). Widgets use `QIcon::fromTheme`; QML Controls use `icon.name` (same
  lookup); tree/table delegates use `image://kicon/<name>` from an `IconImageProvider` on the shared engine that
  tints `currentColor` SVGs with the palette.
- **i18n:** `tr()`/`qsTr()` everywhere; `qt_add_translations` with lupdate scanning C++ and the QML modules; runtime
  language switch = swap `QTranslator`, `engine->retranslate()` (one call thanks to the shared engine), widgets
  handle `QEvent::LanguageChange`. Product texts from `.knxprod` are *data* (multi-language in the XML): VMs
  pick the language from `ProjectSession.contentLanguage`, never via `tr()`. No string concatenation in UI text.

## 6. Testability

1. **Qt Test for models/VMs** (`tests/ui/viewmodels`): VMs against in-memory fixture projects and the simulated
   bus (NFR-5); `QAbstractItemModelTester` on every model; `QSignalSpy` on selection/undo; no Quick needed.
2. **Qt Quick Test for QML components** (`tests/ui/qml`, `quick_test_main_with_setup`): the setup object
   registers fakes. VM "interfaces" are abstract QObject bases registered `QML_UNCREATABLE`
   (`AbstractParameterPageModel`, `AbstractTreePanelVM`); production and `Fake*` classes both derive, so a typed
   `required property` accepts the fake and qmllint still checks the view. Tests: delegate selection per
   `editorType`, commit-on-editingFinished, focus kept across visibility changes, keyboard navigation.
3. **Robot Framework for acceptance.** Two drivers:
   - **`kestrel-cli`** (core only, no GUI): import, validate, download to simulated devices. Most acceptance
     tests run here — fast and stable.
   - **Automation server** for thin GUI tests: `kestrel_automation` lib, active only with
     `--automation=<socket-name>` (compiled in but off by default; refuses to start in release unless
     `KESTREL_ALLOW_AUTOMATION=1`). `QLocalServer`, newline-delimited **JSON-RPC 2.0**. Methods:
     `app.ping`, `app.waitIdle` (event loop idle, no pending loaders/operations), `action.list`,
     `action.trigger {id}`, `action.state {id}`, `ui.find {path}`, `ui.read {path, property}`,
     `ui.write {path, property, value}`, `ui.activate {path}`, `ui.select {path, ref}`, `ui.grab {path, file}`,
     `selection.get`, `bus.sim.inject {telegram}`, `bus.sim.sent`. Paths are objectName chains crossing the
     widget/QML boundary: `mainWindow/dock.topology/topology.tree`, resolved via `QObject::findChild` and
     `QQuickWidget::rootObject()->findChild`. Interactions go through `QAccessibleInterface` actions (press,
     setText) rather than synthetic pixel clicks — robust to layout, and it forces us to keep accessibility working.
   - Robot side: `tests/robot/libraries/KestrelLibrary.py` (keywords `Start Kestrel`, `Trigger Action`,
     `Select In Tree`, `Set Parameter`, `Telegram Should Be Sent`), run with `QT_QPA_PLATFORM=offscreen` and
     `QT_QUICK_BACKEND=software`.
   - Convention enforced by a CI script: every interactive QML item has `objectName` and `Accessible.name`.

## 7. Directory layout and CMake targets

```
src/
  core/                    kestrel_core (other architects; QtCore only)
  viewmodels/              kestrel_viewmodels  STATIC, Qt::Core Gui Qml  — qt_add_qml_module URI Kestrel.ViewModels
    services/              ProjectSession, SelectionService, ActionRegistry, NavigationService, BusSession, ThemeService
    models/                *TreeModel, *TableModel, TelegramStore/TelegramModel, ParameterPageModel
    vm/                    *PanelVM, DeviceEditorVM, GroupMonitorVM, UndoCommandAdapter
  qml/
    Kestrel/Controls/      kestrel_qml_controls  URI Kestrel.Controls (KTreeView, StatusBadge, Parameters/*Editor.qml)
    Kestrel/Views/         kestrel_qml_views     URI Kestrel.Views    (TopologyPanel.qml, DeviceEditor.qml, GroupMonitor.qml …)
  shell/                   kestrel_shell STATIC, Qt::Widgets QuickWidgets QuickControls2
                           MainWindow, QmlHost, QmlDock, DocumentArea, WorkspaceManager, QmlEngineProvider,
                           ContextMenus, IconImageProvider, modal dialogs (QDialog + QmlHost)
  automation/              kestrel_automation STATIC, Qt::Network (QLocalServer)
  app/                     kestrel (executable): main.cpp, AppContext, resources/icons, i18n/*.ts
  cli/                     kestrel-cli (executable, core only)
tests/
  ui/viewmodels/           tst_* (Qt Test)            ui/qml/  tst_*.qml + setup.cpp (Qt Quick Test)
  robot/                   suites/, libraries/KestrelLibrary.py, resources/
```

Dependency rule (checked by a CMake script): `viewmodels` never links Quick/Widgets; `qml` modules import only
`Kestrel.ViewModels`, `Kestrel.Controls` and Qt modules; only `shell` links QuickWidgets. File dialogs use
`QFileDialog` from the shell (native), not QuickDialogs2. Note: QtNetwork is needed for the automation server
(present on the dev box, not in the brief's module list).

## My 3 non-negotiable decisions

1. **One shared `QQmlEngine`, QQuickWidget everywhere, created lazily through a single `QmlHost`** — with
   `QML_ELEMENT`/`QML_SINGLETON` via `qt_add_qml_module`, no context properties, no QML `Shortcut`s, native
   `QMenu`s for context menus.
2. **QML is presentation only**: all state, validation, `choose/when` evaluation, action enablement and undo live
   in `kestrel_viewmodels` (no Quick/Widgets dependency), exposed as `QAbstractItemModel`s + ViewModels with
   string-ID actions; models emit fine-grained changes, never `modelReset` on edits.
3. **Every UI element is addressable and testable**: stable objectName + Accessible.name convention, VM fakes for
   Qt Quick Test, and a JSON-RPC automation socket (plus `kestrel-cli`) for Robot Framework — designed in from day
   one, not bolted on.
