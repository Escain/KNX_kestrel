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
5. Commissioning: program the individual address, download the application, read device info.

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

Commissioning facts go through `Project::recordState`: the project becomes dirty, nothing is undoable. The actions are
in the **Commissioning** menu and in the device context menus (Topology, Buildings). The tool bar has *Add Product to
Line*, *Program Individual Address* and *Download Application*.

## 5. Hooks

```cpp
// kestrel/viewmodels/ConfigurationHooks.hpp
using ActiveComObjectFilter = std::function<std::vector<catalog::EffectiveComObject>(
	const catalog::ApplicationProgram&, const model::Device&, const QString& language)>;
using DownloadPlanner = std::function<Result<download::Plan>(const model::Project&, model::Id<model::Device>)>;
void Workspace::setActiveComObjectFilter(ActiveComObjectFilter);  // default activeComObjects (appconfig)
void Workspace::setDownloadPlanner(DownloadPlanner);              // default catalogDownloadPlanner(catalog())
```

## 6. Tests
- `qt.viewmodels.CatalogViewModels`: import, index reuse, bad files, filter, add to line with undo, drop on Topology,
  active com objects, linking and unlinking with undo, parameters that change the active objects. Opt-in timing
  against real files through `KESTREL_PRIVATE_CORPUS`.
- `qt.viewmodels.BusViewModels`: IA programming, cancel, device info, programming-mode listing, download without a
  planner, with an injected plan, refused by the device, and end to end (catalog + appconfig planner → simulator).
- `qt.shell.MainWindow`: every dock loads its view; the catalog dock, catalog device editor and commissioning dock
  in the real window (screenshots with `KESTREL_SCREENSHOT_DIR`).

## 7. Open points
- Drag and drop uses `Drag.Automatic` (a platform drag between QQuickWidgets). The tests cover the drop intents, not
  the mouse gesture, which has only been checked offscreen.
- A com object that becomes inactive after a parameter change keeps its model `ComObject` and links. There is no
  warning yet.
- Catalog texts are indexed in en-US. The device editor uses the project's content language.
- No "Read configuration from device" action yet (`appconfig::importSystemBImages` exists).
- No per-device "needs download" indicator yet (compare `downloadedHash` with the current plan).
