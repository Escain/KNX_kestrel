# 02 — Architecture

Status: **v1.0** (2026-10-28), synthesised from the brainstorm room
(`brainstorm/round1-*.md`, `brainstorm/round2-*.md`). Decisions are recorded as ADRs in `adr/`.

## 1. Shape of the system

```
                ┌────────────── apps ───────────────┐
                │ kestrel (GUI)  kestrel-cli  kestrel-sim │
                └──────┬───────────────┬──────────┬──┘
   T2 (GUI)   shell ── qml ── viewmodels           │
                       │                │          │
   T1 (QtCore) ────────┴──── app ───────┘          │
                 model  persist  validate  bus  simnet
                                   │        │      │
   T0 (std)          base   knx   dpt   sim ◄──────┘
```

### Dependency tiers (ADR-001) — enforced by `cmake/KestrelLayering.cmake`

| Tier | May use | Libraries |
|------|---------|-----------|
| **T0** | C++20 standard library only (`archive`: + system zlib, ADR-004) | `base`, `knx`, `dpt`, `sim`, `archive` |
| **T1** | T0 + QtCore (+ QtNetwork in `bus`/`simnet`, + QtSql later in `catalog`) | `model`, `persist`, `validate`, `catalog`, `bus`, `simnet`, `app` (later `ets`) |
| **T2** | T1 + QtGui/Qml/Quick/Widgets | `viewmodels` (no Quick/Widgets), `qml` modules, `shell` |

- `kestrel-cli` and `kestrel-sim` link T0/T1 only; they never pull in QtGui.
- T0 logs through an injected `std::function` sink; T1 forwards it to `QLoggingCategory`.

## 2. Libraries

| Target | Tier | Responsibility |
|--------|------|----------------|
| `kestrel::base` | T0 | `Result<T>`, `Error`, `Bytes`, `ByteReader/Writer` |
| `kestrel::knx` | T0 | Addresses, cEMI, TPDU/APDU codecs, KNXnet/IP codecs, **sans-IO** state machines (tunnel client, management session) driven by an injected clock — see `04-knx-protocol.md` |
| `kestrel::dpt` | T0 | Datapoint-type codecs (core DPTs hand-written), formatting |
| `kestrel::sim` | T0 | Deterministic virtual bus + virtual devices (memory, prog-mode, IA, group objects), KNXnet/IP tunnelling *server* state machine |
| `kestrel::archive` | T0 | Own in-memory ZIP reader over zlib: stored/deflate, ZIP64, CRC, zip-bomb and path-traversal guards (ADR-004) |
| `kestrel::model` | T1 | Project domain model (value types), transactions, `History` (undo/redo), `ChangeSet` — see `03-domain-model.md` |
| `kestrel::persist` | T1 | Native `.kestrel` format (deterministic JSON; folder form first, ZIP later), migrations |
| `kestrel::validate` | T1 | Pure validation rules → `Diagnostic`s |
| `kestrel::catalog` | T1 | `.knxprod` reader (QXmlStreamReader) → `ProductDatabase`: hardware, products, catalog, application programs, merged com objects, per-channel index, translations — see `07-catalog.md` |
| `kestrel::bus` | T1 | Qt I/O adapters: `UdpTunnelLink` (QUdpSocket + `knx::TunnelClient`), discovery, timers; runs on a bus thread |
| `kestrel::simnet` | T1 | `KnxIpServer`: exposes a `sim::VirtualBus` on a real UDP port (used by `kestrel-sim` and in-process Qt tests) |
| `kestrel::app` | T1 | Use-case services shared by GUI and CLI: `BusSession` (bus thread, group read/write, monitor ring buffer, IA programming, device info) |
| `kestrel::viewmodels` | T2 | `ProjectSession` (document lifecycle; the CLI is stateless and does not need it), `ShadowTreeModel`-based item models, panel view models, `SelectionService`, `ActionRegistry`, `CatalogService` (user catalog), `CommissioningVM` (QtGui/Qml, **no** Quick/Widgets); uses `catalog` and `appconfig` — see `08-gui.md` |
| `kestrel_qml_*` | T2 | QML modules `Kestrel.Controls`, `Kestrel.Views` — presentation only |
| `kestrel::shell` | T2 | `MainWindow`, `QmlHost` (QQuickWidget), docks, workspace layouts |

M2 started `archive` (own ZIP reader; writer and WinZip-AES over OpenSSL later, ADR-004) and `catalog` (knxprod
reader and application-program model, `07-catalog.md`; later the user index, dynamic evaluator, memory-image builder).
Later milestones add `ets` (knxproj import), `download`
(procedure resolver / planner / executor, ADR-008).

## 3. Key mechanisms

### 3.1 Sans-IO protocol (ADR-002)
Codecs are pure functions over bytes. State machines expose `handle(event) → actions`, with time passed in
(`TimePoint now`). The Qt adapters in `bus` own the sockets and `QTimer`s and translate. Unit tests drive the
state machines with a manual clock: no sockets, no sleeps, no event loop.

### 3.2 Model mutation & undo (ADR-003)
- `Project` is a value type. Mutation only through `project.edit(label, [](Tx&){...})`.
- `Tx` records reversible primitive ops; nobody hand-writes undo code.
- Each commit/undo/redo yields **one ordered `ChangeSet`**: notifications, dirty flag, autosave and validation all use it.
- `History` (in `model`) is the **only** undo stack. The GUI binds Undo/Redo `QAction`s to it; there is no `QUndoStack`.
- **Commissioning facts** (IA programmed, serial read, downloaded hash) come from the bus and are written via
  `project.recordState(...)`. They emit a ChangeSet and set the dirty flag but **bypass undo**.
- Threading: `Project` is mutated on the GUI (main) thread only. Workers use snapshot copies.

### 3.3 UI composition (ADR-006)
- `QMainWindow` shell (widgets): menus, toolbars, status bar, docks, central `QTabWidget` of editors.
- **Every dock body and every editor tab is a `QmlHost : QQuickWidget`** built on **one shared `QQmlEngine`**.
  Hosts are created lazily on first show.
- QML is presentation only. All state, enablement and validation live in C++ view models, exposed via
  `QML_ELEMENT`/`QML_SINGLETON` (`qt_add_qml_module`), never via context properties.
- Shortcuts belong to `QAction`s; context menus are native `QMenu`s (QML popups are clipped by QQuickWidget).
- Style: Fusion for widgets and Qt Quick Controls, so both draw from the same `QPalette` (light and dark).
- Cross-dock selection goes through `SelectionService` (an `EntityRef` list plus the origin view).
- Item models apply ChangeSets incrementally (insert/remove/dataChanged) and never reset on an edit.

### 3.4 Bus access
`IBusLink` (T0 interface in `knx`) is implemented by `bus::UdpTunnelLink` (real KNXnet/IP) and by in-process
`sim` links. `app::BusSession` owns the link on a dedicated `QThread`, and the GUI talks to it via queued signals.
The group monitor ingests telegrams into a ring buffer, and the GUI drains it in batches (20–30 Hz).

### 3.5 GUI for configuring a network
Docks for the catalog and for commissioning, the catalog device editor with its parameters, and the hooks for the
configuration engine are described in `08-gui.md`.

### 3.6 Clean room (ADR-009)
No ETS binaries are decompiled. Proprietary XML is never committed. CI fixtures are hand-written from the public
spec (fake manufacturer `M-FFFF`). Real vendor files are used only via the opt-in `KESTREL_PRIVATE_CORPUS` env var.

## 4. Repository layout

```
CMakeLists.txt  CMakePresets.json  cmake/           build helpers, warnings, layering check
specs/                                                this folder
third_party/escain_unittests/                         UnitTests.hpp (vendored, author's library)
src/<lib>/include/kestrel/<lib>/*.hpp                 public headers
src/<lib>/src/*.cpp                                   implementation (+ private headers)
src/qml/Kestrel/{Controls,Views}/                     QML modules
apps/{kestrel,kestrel-cli,kestrel-sim}/               executables
tests/support/                                        unit_main.cpp, Printers.hpp, QtPrinters.hpp
tests/unit/<lib>/                                     UnitTests.hpp suites   → ctest `unit.<lib>`
tests/qt/<lib>/                                       Qt Test / Qt Quick Test → ctest `qt.<lib>.<Name>`
tests/robot/{suites,libraries,resources}/             Robot Framework        → ctest `robot.<suite>`
tests/data/                                           synthetic fixtures only
```

## 5. Conventions (summary — full list in ADR-007)
- Style follows `UnitTests.hpp`: tabs, Allman braces, `PascalCase` types, `camelCase` functions, `m_` members,
  `k` constants, `KESTREL_` macros. Namespaces `kestrel::<lib>`.
- Expected failures → `kestrel::Result<T>` (own, `std::expected`-shaped). Programmer errors → `assert`.
  Exceptions never cross a library's public API.
- Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion …`, `-Werror` in presets/CI.
- `QT_NO_CAST_FROM_ASCII`: use `u"…"_s` / `QStringLiteral`.
- Every value type has `operator==` and an `operator<<` (for `ASSERT_EQ` and logging).
