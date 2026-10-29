# 08 — GUI: configuring a network

Status: **v0.1** (2026-10-28). Libraries: `kestrel::viewmodels`, `Kestrel.Views` (QML), `kestrel::shell`.
Composition rules are in ADR-006 (QMainWindow, one QQuickWidget per dock or tab on one shared engine, view models in
C++, presentation-only QML). This file describes the docks and actions that take a project from an empty topology to
downloaded devices.

## 1. Workflow and layout

```
Topology / Buildings / Group Addresses (left, tabbed)   Editors (centre)   Properties (right)
Problems / Catalog / Commissioning / Bus Connection (bottom, tabbed)
```

1. Topology: add an area and a line.
2. Catalog: import the manufacturers' `.knxprod` files, then **Add Product to Line** (or drag a product onto a line).
3. Device editor (double-click a device): parameters, and links between group objects and group addresses
   (type an address, pick one, or drag it from the Group Addresses dock).
4. Bus Connection: search for interfaces or enter host:port, connect.
5. Commissioning: program the individual address, download the application, read device info. An installation
   configured with another tool can be taken over with *Read Configuration from Device*.
6. The Topology *Download* column and the editor header show which devices need a download. Problems lists linked
   com objects that the parameters deactivated.

`MainWindow::kLayoutVersion` is 3 (the Catalog and Commissioning docks were added).

## 2. Catalog (FR-CAT-1/2/3)

**Folder.** `$KESTREL_CATALOG_DIR` if set (tests), else `QStandardPaths::AppDataLocation + "/catalog"`. Import
copies the file there. A file whose content is already in the folder is not copied again. A different file with the
same name gets a `-2`, `-3`… suffix. A copy that cannot be parsed is removed again and reported.

**Index.** `CatalogService` keeps one `CatalogEntry` per (product, application program): manufacturer, product id
and text, order number, hardware name, program id, name, version and mask version. It is persisted in
`<folder>/index.json` and keyed by file name, size and modification time, so unchanged files are never parsed at
start-up. New or changed files are parsed on a worker thread (`QThreadPool`, 2 threads). Nothing is read until the
Catalog dock is first shown, a device editor needs a program, or a file is imported.

**Databases.** Parsed `catalog::LoadedDatabase`s are cached per file (LRU, 4 entries). `requestDatabase()` parses
off the GUI thread and signals `databaseReady`. `loadDatabase()` / `databaseForProgram()` parse synchronously and are
thread-safe (the download planner uses them on its worker thread).

**Dock.** Tool bar: *Import Product File…* (`catalog.import`, Ctrl+Shift+I, native file dialog), *Add Product to
Line* (`catalog.addToLine`, Ctrl+Shift+A, also Enter or double-click on a product), a filter (every word must match
some column) and the target line. The table has the columns Product, Order number, Application, Version and
Manufacturer, sorted by product.

**Add to line.** The target line follows the selection: the selected line, or the line of the selected device. It is
kept while other kinds of entities are selected. One undo step creates a `model::Device` with `productRef`,
`appProgramRef`, name = product text, and the lowest free device number (1..255) on the line. The new device is selected.
Rows can also be dragged (`application/x-kestrel-catalog-entry`, data = entry key) onto a line or device in Topology.

## 3. Device editor (FR-DEV-1/2, FR-GA-3)

A device with an `appProgramRef` is a **catalog device**. The editor shows its product, order number and application.
`catalogState` is `loading` while the product file is parsed, `ready`, `missing` (program not in the catalog: import
its product file), or `error`. Manual devices (no program) keep the M1 editor (hand-defined group objects).

**Group Objects tab.** One row per active com object: #, text, function, channel, size, flags, DPT and linked group
addresses (sending first). The active set comes from the `ActiveComObjectFilter` hook, which defaults to
`activeComObjects()`: `appconfig::evaluate()` over the device's parameters. When several active refs share an object
number, the first in document order wins. Rows are keyed by `ComObjectRef` id and change incrementally (no reset).

- **Link** (pick an existing address from the editable combo box, or type `1/2/3`): one undo step creates the model
  `ComObject` on demand (`catalogRef` = `ComObjectRef` id, number, text, function, size, flags, first valid DPT).
  If the address does not exist yet, it creates the `GroupAddress` too (named "text function", with the object's DPT,
  in the innermost group range). The first link of an object is the sending one.
- **Drag** a group address from the Group Addresses dock (`application/x-kestrel-ref`, data = `ga-…`) onto a row.
- **Unlink**: removing the last link of a catalog com object also removes its model `ComObject` in the same step.
- **Inactive linked objects**: a linked com object that a parameter change deactivated (its `ComObjectRef` is not
  active in `appconfig::evaluate`) stays in the table, greyed, with a warning above the table. Its links are kept but
  not downloaded (appconfig drops them), and they are never deleted automatically. Unlink them to clear the warning.

The header also shows the device's download state (§5).

**Parameters tab.** The active `ParameterRef`s (`appconfig::evaluate`), grouped under their channels and
`ParameterBlock`s in the order of the Dynamic section. Refs with `Access="None"` are hidden and `Read` refs are
read-only. The current value is `appconfig::valueOf()`. The editor depends on the parameter type: a combo box for
enumerations, a spin box for numbers (range from the type), a text field for text and float values, a label for
`TypeNone`. *Reset* goes back to the default. Edits go through `model::setDeviceParam` (undoable, consecutive edits of
one parameter merge). Storing the default value removes the entry. Another ref of the same `Parameter` loses its stale
value in the same step. Each edit re-evaluates both tabs.

## 4. Bus and commissioning (FR-BUS-1/2, FR-COM-1/2/4)

**Bus Connection dock** (`BusConnectionVM`): manual host:port, Connect/Disconnect, *Search* (KNXnet/IP discovery). A
double-click on a found interface connects to it. Actions: `bus.connect` (shows the dock), `bus.discover`,
`bus.disconnect`, `bus.groupMonitor` (Ctrl+M).

**Commissioning dock** (`CommissioningVM`). It acts on the target device (the selected device, or the device of a
selected group object) and shows its commissioning facts. It runs one bus operation at a time and shows a progress
bar (step x/y, or indeterminate), the result in green or red, and a log. The dock comes to the front when an operation
starts.

| Action | Id | Shortcut | Enabled when |
|--------|----|----------|--------------|
| Program Individual Address | `device.programAddress` | Ctrl+Shift+P | connected, idle, target device has an address |
| Download Application | `device.download` | Ctrl+Shift+D | … and the device has an application program |
| Read Configuration from Device | `device.readConfiguration` | | connected, idle, addressed catalog device |
| Read Device Info | `device.readInfo` | | connected, idle, addressed target |
| Show Devices in Programming Mode | `bus.programmingMode` | | connected, idle |
| Cancel Bus Operation | `device.cancelOperation` | | IA programming or a download is running |

- **Program individual address** shows "Press the programming button of the device now" until a device answers.
  Success records `iaProgrammed`.
- **Download**: the `DownloadPlanner` hook builds a `download::Plan` on a worker thread from a snapshot copy of the
  project ("Preparing the configuration…"). Then `BusSession::download` runs it with step progress. The default planner
  (`catalogDownloadPlanner`) takes the device's product database from the catalog and calls
  `appconfig::planDeviceDownload` (System B only; other masks fail with the planner's Unsupported message). Without a
  planner the action reports that the configuration generator is not available. Success records
  `downloadedHash = planHash(plan)` (SHA-256 of the plan's exact content).
- **Read device info** records the serial number.
- **Read configuration from device** (System B only) follows the sequence of `kestrel-cli device import`:
  1. Read PID 7 (table reference) and PID 27 (memory control block) of objects 1..4, plus PID 13 (program version) of
     object 4. A different manufacturer or application number fails. A different version is reported as a warning.
  2. Segment sizes come from the first 4 octets of PID 27. When that is 0, objects 1 and 3 use the u16 entry count
     (2 + 2n bytes), object 2 uses 2 + 4n bytes, and object 4 uses the size of the application's `RelativeSegment`
     with LSM 4.
  3. Read the four segments.
  4. `appconfig::importSystemBImages` runs on the GUI thread as one undo step and replaces the device's parameters,
     com objects and links.
  The product database is loaded off the GUI thread. There are 4 progress steps. Cancel takes effect at the next bus
  answer (the running management request completes). The warnings go to the log and the error banner.

Commissioning facts go through `Project::recordState`: the project becomes dirty, nothing is undoable. The actions are
in the **Commissioning** menu and in the device context menus (Topology, Buildings). The tool bar has *Add Product to
Line*, *Program Individual Address* and *Download Application*.

## 5. Device status (FR-DEV-3)

`DeviceStatusService` computes the status of catalog devices lazily and off the GUI thread. It starts at the first
query (Topology column, editor header, Problems). After each change, a 250 ms debounce runs. Then a cheap fingerprint
of every catalog device (application, parameters, com objects and links) selects the devices whose inputs changed. One
worker thread then works on a snapshot copy of the project and computes, for each of these devices:

- `planHash()` of the `DownloadPlanner`'s plan, but only for devices with a `downloadedHash`. Compared with
  `DeviceState::downloadedHash`, it gives the download state: *Not loaded* (never downloaded by Kestrel), *Loaded*
  (up to date), *Modified* (download needed), *…* (first computation), *?* (no planner, or the plan fails).
- the linked com objects whose `ComObjectRef` is not active. They become **CO-101** warnings in Problems:
  "Linked com object is not active with the device's parameters: its links are not downloaded".
  `ProblemsModel::setExtraSource` merges them into the `validate::run()` results.

Until a new result arrives, the previous one is shown, so typing in the parameter editor never waits and nothing
flickers. A burst of edits costs one computation. Results of a batch that started before the project was replaced, the
catalog changed or the planner changed are discarded.

## 6. Look

A flat workbench look: white panels on a grey gutter, one accent, and colour reserved for state.
The design is the "Kestrel UI redesign" canvas (Claude Design); the code is the reference.

- **One palette for widgets and QML.** `shell::initializeStyle()` sets Fusion and an application `QPalette` built
  from `shell::Theme` (light or dark, following the system colour scheme; `KESTREL_COLOR_SCHEME=dark|light`
  overrides it). Qt Quick's Fusion takes its colours from the platform theme, not from the application palette, so
  `QmlHost` copies the palette onto each scene's content item (`QQuickPalette` properties); the root item inherits
  it.
- **Chrome** (menu bar, tool bar, docks, tabs, status bar) is styled by the application style sheet
  (`shell::styleSheet`). Dock tabs are on top. A tabbed dock hides its title bar (the tab names it); a dock on its
  own keeps it. Download is the tool bar's primary action (accent button).
- **Icons** are Kestrel's own stroke SVGs (`src/shell/icons`), recoloured from the theme by `shell::themedIcon`, so
  they do not depend on the desktop icon theme.
- **QML tokens** come from the `Kestrel.Controls` `Theme` singleton: the palette roles, plus the colours a palette
  has no role for (selection tint, hover, status colours and their backgrounds), repeated from `shell::Theme`.
  Shared controls: `PanelToolBar`, `HeaderCell` (table headers), `TableCell` (rows with the selection tint and an
  accent bar), `Badge` (status), `AddressChip` (monospaced addresses), `SectionLabel`, `UnderlineTabBar` /
  `UnderlineTabButton`.

| Token | Light | Dark | Use |
|---|---|---|---|
| gutter | `#EEF0F3` | `#0F1216` | main window background |
| panel | `#FFFFFF` | `#171B21` | panels, tool bar, menus |
| panelAlt | `#F6F7F9` | `#1D222A` | headers, tab strips |
| border / borderStrong | `#E1E4E9` / `#D5D9E0` | `#2A3039` / `#3A424D` | separators / inputs |
| text / secondary / disabled | `#1B2230` / `#5B6472` / `#A3AAB5` | `#E4E7EC` / `#9AA3B0` / `#5E6774` | |
| accent | `#0B6E99` | `#3FA4CF` | selection bar, primary action, focus |
| selection | `#E6F1F6` on `#08506F` | `#17323F` on `#CDE8F4` | selected rows |
| warning / success / error | `#B45309` / `#1E7B3E` / `#B42318` | `#F59E0B` / `#34C26B` / `#F87171` | state only |

## 7. Hooks

```cpp
// kestrel/viewmodels/ConfigurationHooks.hpp
using ActiveComObjectFilter = std::function<std::vector<catalog::EffectiveComObject>(
	const catalog::ApplicationProgram&, const model::Device&, const QString& language)>;
using DownloadPlanner = std::function<Result<download::Plan>(const model::Project&, model::Id<model::Device>)>;
void Workspace::setActiveComObjectFilter(ActiveComObjectFilter);  // default activeComObjects (appconfig)
void Workspace::setDownloadPlanner(DownloadPlanner);              // default catalogDownloadPlanner(catalog())
```

## 8. Tests
- `qt.viewmodels.CatalogViewModels`: import, index reuse, bad files, filter, add to line with undo, drop on Topology,
  active com objects, linking and unlinking with undo, parameters that change the active objects. Also: needs download
  (planner off the GUI thread, debounced) and inactive linked objects (greyed, CO-101). Opt-in timing
  against real files through `KESTREL_PRIVATE_CORPUS`.
- `qt.viewmodels.BusViewModels`: IA programming, cancel, device info, programming-mode listing, download without a
  planner, with an injected plan, refused by the device, and end to end (catalog + appconfig planner → simulator).
  Read configuration: download a configuration, read it into a fresh project (parameters, com objects, links, one
  undo step), and cancel. Up to date after a download, modified after a parameter edit.
- `qt.shell.MainWindow`: every dock loads its view; the catalog dock, catalog device editor and commissioning dock
  in the real window (screenshots with `KESTREL_SCREENSHOT_DIR`).

## 9. Open points
- Drag and drop uses `Drag.Automatic` (a platform drag between QQuickWidgets). The tests cover the drop intents, not
  the mouse gesture, which has only been checked offscreen.
- Catalog texts are indexed in en-US. The device editor uses the project's content language.
- Reading the configuration does not set `downloadedHash`, so the device then shows *Not loaded*. Comparing the read
  images with the planned ones could mark it *Loaded*.
- Cancel cannot interrupt a management request that is already running (`BusSession` has no cancel for it).
