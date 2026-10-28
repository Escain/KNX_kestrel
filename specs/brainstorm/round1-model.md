# Brainstorm round 1: Data model and persistence

Author role: data model and persistence architect. Status: proposal for discussion (2026-10-28).
Scope: domain model, mutation and undo, catalog storage, native file format, ETS import, validation, targets.

What I checked on the dev box:
- `QUndoStack` / `QUndoCommand` / `QUndoGroup` ship in **QtGui** in Qt 6 (`qt6/QtGui/qundostack.h`), not in QtWidgets.
- There are **no zip headers** (no libzip, minizip, QuaZip or KF6Archive). We have `zlib.h`, libdeflate, zstd, liblzma
  and `sqlite3.h`, and the Qt `QSQLITE` driver is installed. `QZipReader` is private API, so we will not use it.
- `knx_master.xml` (namespace `project/23`, MasterData Version 11, about 1 MB) contains these sections: DatapointTypes
  (57 DPT, 326 DPST), DatapointRoles, InterfaceObjectTypes/Properties, PropertyDataTypes, MediumTypes,
  MaskVersions (with load procedures), FunctionalBlocks, ProductLanguages, FunctionTypes, SpaceUsages,
  Manufacturers (660), Languages.

---

## 1. Core domain model

### 1.1 Decision: QtCore is allowed, QObject entities are not
- **`kestrel_knx`** (addresses, DPT ids, DPT encode/decode, com-object flags, medium enums) is **std-only**: header-heavy
  and `constexpr`-friendly. It is shared with the bus/protocol stack and tested with `UnitTests.hpp` alone.
- **Model, catalog and persistence may use QtCore** (`QString`, `QByteArray`, `QXmlStreamReader`, `QJsonDocument`,
  `QSaveFile`, implicit sharing, plus Qt Sql in the catalog). A std-only model would make us rewrite UTF-16/i18n
  string handling, XML streaming, JSON and atomic file writes, and add a conversion layer at every UI boundary.
  QtCore is headless and works in a CLI, so it satisfies NFR-3.
- **Entities are plain value types, not `QObject`s.** I reject a QObject tree for these reasons:
  1. 10k GAs plus about 50k com-object instances as QObjects is a large memory cost and makes signal fan-out slow (NFR-8).
  2. QObjects have thread affinity. Import and autosave run on worker threads (NFR-7), and value types let a worker build
     a whole `Project` and `std::move` it to the GUI thread.
  3. Value types copy. A snapshot (for autosave, validation or export) is just a copy, and it is cheap because large
     members use `QSharedDataPointer` copy-on-write.
  4. Per-property signals encourage UIs that react halfway through a multi-step edit.

### 1.2 Identity
- **Catalog entities keep the KNX string ids** as their primary key (`M-0083_A-0098-12-6A1B`, `M-0083_H-…_HP-…`,
  `DPST-9-1`, `MV-0705`). Manufacturers made these ids globally unique and stable, and they are what links a project to
  an application program. They are interned as `CatalogKey` (an index into a string table plus `QString` for I/O).
- **Project entities get Kestrel ids**: `Id<Tag>` holding a random 64-bit value, written as a prefixed base32 string
  (`dev-7f3k2m9qv1ab`, `ga-…`, `bp-…`). ETS project ids (`P-0815-0_DI-3`) are tied to one project, change on
  copy/merge, and encode a hierarchy we do not want. Random ids (instead of a `nextId` counter) avoid merge conflicts
  when users keep projects in git branches. The original ETS id is kept as `knxId` provenance for re-export.
- **References between entities are always by `Id`, never by pointer.** Reverse indexes are derived data rebuilt by
  `Project` and never persisted. Examples: GA→links, device→place, IA→device.

### 1.3 Entities (sketch)
```
Project            { Id, name, ProjectSettings{gaStyle: ThreeLevel|TwoLevel|Free, defaultLanguage, ...},
                     Table<BuildingPart>, Table<Area>, Table<Line>, Table<Segment>, Table<Device>,
                     Table<GroupRange>, Table<GroupAddress>, Table<Link>, Table<Trade>, MasterDataRef }
BuildingPart       { Id, parent?, kind: Building|Floor|Room|Corridor|Stairway|DistributionBoard|..., name,
                     order, usage (SpaceUsage key) }
Area/Line/Segment  { Id, parent, number (0..15 for area and line), medium (TP|IP|RF|PL), name, domainAddress (RF/IP) }
Device             { Id, segment, address? (device number 0..255; IA derived from area.line.number),
                     place?: Id<BuildingPart>, name, description, serial?,
                     product: CatalogKey (Hardware2Program + CatalogItem), appProgram: CatalogKey?,
                     params: ParamValues   // sparse: ParameterRefId -> canonical string; defaults NOT stored
                     comObjects: ComObjectOverrides  // only entries with non-default flags/DPT/text
                     state: { iaProgrammed, downloadedHash, ... },  extras: AttrBag }
Link               { Id, device, comObjectRef: CatalogKey, ga: Id<GroupAddress>, order }  // order 0 = sending GA
GroupRange         { Id, parent?, name, rangeStart, rangeEnd }
GroupAddress       { Id, range?, raw (uint16), name, dpt: DptRef?, central, passThroughLineCoupler, extras }
```
- **Parameter values are stored as canonical strings, the way ETS does.** A typed view (`int`, `double`, enum, time and
  so on) comes from the catalog `ParameterType`. This keeps import and export lossless and keeps the model independent of
  the catalog parser.
- **Links are a first-class relation table.** They are not a list hanging off com objects as in ETS's `Links="…"` attribute.
  Many-to-many queries in both directions, link ordering (FR-GA-3) and undo all become simple.
- **"Needs download" is derived, not stored**: `hash(params, links, IA) != state.downloadedHash`. This stays correct
  under undo and redo for free.
- Visibility of parameters and com objects (`choose/when`) is **not stored**. `catalog::Evaluator(appProgram, params)`
  computes it.

### 1.4 Change notification
- There is no per-field signal. Each committed transaction (and each undo or redo) produces **one `ChangeSet`**:
  a list of `{Added|Removed|Modified, EntityRef, FieldMask}`.
- `Project` publishes it through a std-only observer (`Subscription onChanged(std::function<void(const ChangeSet&)>)`).
  The app layer has one `ProjectNotifier : QObject` that re-emits it as a Qt signal for QML and `QAbstractItemModel`
  adapters (`beginInsertRows` / `dataChanged`).
- Consumers include tree models, the validation scheduler, the dirty flag and the autosave timer. All of them read the
  same ChangeSet.

---

## 2. Mutation and undo

- **The only way to mutate is a transaction.** `Project` exposes const accessors only, plus:
  `project.edit(tr("Link object"), [&](Tx& tx){ tx.insert(link); tx.set(gaId, &GroupAddress::dpt, dpt); });`
- `Tx` records **primitive reversible ops**: `Insert`, `Erase` (stores the erased value), `SetField` (old and new value via
  a member-pointer visitor), `SetParam`, `Reorder`. Undo replays the inverses in reverse order.
  **Nobody hand-writes an `undo()`.** High-level "commands" such as AddDeviceFromCatalog, MoveDevice, RenumberLine or
  ImportGAsCsv are plain functions that open one transaction. Hand-written undo code is where undo bugs come from.
- **Batching**: nested `edit()` calls fold into the outermost transaction, so a batch is one undo step. A failed lambda
  (exception or `Tx::abort()`) rolls back everything already applied, so the model never stays half-edited.
- **Merging**: a transaction may carry a `mergeKey` (for example `{device, paramRef}`). Consecutive transactions with the
  same key within 1 s merge, so typing in a spin box does not flood the history.
- `History` (in `kestrel_model`, QtCore-free API) holds an undo/redo list, a `cleanIndex` used for the dirty flag and
  "saved" state, and an optional size limit.
- **QUndoStack**: it lives in QtGui, but our core must not link QtGui (it brings a platform plugin and font stack into the
  CLI and tests). Decision: **our `History` is the single source of truth.** The app exposes Undo/Redo `QAction`s bound
  to `History::canUndo()` and `undoText()` (about 40 lines). If we want `QUndoView` later, an app-side `UndoMirror`
  pushes proxy `QUndoCommand`s whose first `redo()` does nothing and whose undo/redo delegate to `History`. Two stacks
  are never authoritative.

---

## 3. Catalog storage

- **One user-level catalog** shared by all projects, at `$XDG_DATA_HOME/kestrel/catalog/` (QStandardPaths AppDataLocation):
  - `catalog.sqlite` is the index: manufacturers, catalog sections and items, hardware, products,
    hardware2program, application program headers (number, version, mask, name, sizes), languages, and an
    **FTS5** table for search (FR-CAT-2). It is accessed through Qt Sql (QSQLITE) on a dedicated connection per thread.
  - `blobs/<sha256>.xml.zst` holds the **original application program XML, stored verbatim and compressed**. This file
    stays the canonical source, and SQLite is only an index. If our parser improves we re-parse, we do not migrate.
  - `master/knx_master-<version>.xml.zst` holds the newest MasterData seen in any imported knxprod or knxproj.
- **Lazy parse and cache.** Opening a device's parameters calls `CatalogService::program(key)`, which returns a
  `std::shared_ptr<const ApplicationProgram>`. The model is immutable once built, so it can be shared across threads,
  and it sits in an LRU cache of about 16 programs. The parse uses a single `QXmlStreamReader` pass on a worker thread
  (expected 50–200 ms for a few-MB file). **No custom binary cache in v1.** We add one (`cache/<sha>-<parserVersion>.bin`)
  only if profiling shows a need. Translations load per language on demand.
- **Indexing does not build the model.** Importing a `.knxprod` streams Catalog.xml and Hardware.xml fully but reads only
  the `<ApplicationProgram>` header attributes of each program, so importing a 200-product knxprod takes seconds.
- **Clean-room.** The catalog DB and blobs are user data and are never committed. Tests use hand-written synthetic
  fixtures under a fake manufacturer `M-FFFF`. We do not verify knxprod signatures; we only record the source file hash.
- **DPTs.** `kestrel_knx` has its own DPT registry and codec written from the public KNX spec (vol. 3/7/2). It decides
  sizes and encoding. MasterData only adds names and translations, so the bus monitor works with no master file.

---

## 4. Native project format: `.kestrel`

- **Logical layout** (the same tree is used in two physical forms):
```
kestrel.json                 { "format": "kestrel-project", "formatVersion": 1, "generator": "...", "projectId": "..." }
project.json                 name, settings, GA style, trades, MasterData version needed
buildings.json               building-part tree (children as ordered id lists)
topology.json                areas/lines/segments + ordered device ids per segment
devices/<dev-id>.json        one file per device: header, params (sorted by ref), comObject overrides, links
groupaddresses.json          ranges + GAs, sorted by raw address
catalog/<M-xxxx>/<A-id>.xml  embedded application programs actually used (verbatim) + catalog/products.json subset
catalog/knx_master.xml       copy of required MasterData
history/                     (optional, off by default) nothing in v1
```
- **Physical forms**:
  - (a) `MyHouse.kestrel`: a ZIP for everyday use. It is written deterministically: entries sorted, fixed timestamps,
    deflate level fixed, and XML blobs stored compressed.
  - (b) `MyHouse.kestrel.d/`: an unpacked directory for git users ("Save as folder").
  Both forms use one reader and writer behind a `ContainerFs` interface.
- **Deterministic JSON.** `QJsonObject` already sorts keys. Arrays are sorted by a stable key (id or address), output is
  indented with 2 spaces, it ends with a final newline, and floats are written in canonical form. One file per device
  means renaming one device produces a one-file diff, and parallel edits on different devices merge cleanly. File names
  use ids, not IAs, so readdressing does not rename files.
- **Links are stored with the device** (`"links": {"O-1_R-1": ["ga-…","ga-…"]}`, where list order is sending first),
  because the device file is the unit of diff and of download.
- **Embedded vs. referenced.** Application programs and the MasterData a project uses are **embedded**, so a project is
  self-contained like a knxproj and opens on a machine without that catalog. On open, embedded programs not yet in the
  user catalog are offered for import. Everything else in the catalog (other products) is referenced only by key.
- **Versioning.** `formatVersion` is an integer. Readers run a chain of pure **JSON→JSON migrations** (`v1→v2→…`) on the
  raw document tree before deserializing. Each migration has a golden-file test. A file with a newer version is refused
  with a clear message. We always write the current version.
- **Save** writes through `QSaveFile` (or a temporary directory plus rename for the folder form), serializing a
  **snapshot copy** on a worker thread. **Autosave** writes the folder form to
  `$XDG_STATE_HOME/kestrel/autosave/<projectId>/` every N minutes when the history is dirty. Crash recovery (FR-PRJ-3)
  checks for that directory at startup.

---

## 5. Import of `.knxproj` / `.knxprod`

- **ZIP.** Vendor **minizip-ng** (zlib license) through `FetchContent`, with the system package when available. It
  supports WinZip AES, which password-protected knxproj files need: the inner `P-XXXX.zip` is AES-encrypted. Public
  open-source write-ups (xknxproject, read for knowledge only; it is GPL, so no code copied) describe a PBKDF2-HMAC-SHA256
  password derivation for schema ≥ 21. We implement it from that description, behind FR-PRJ-4 phase 2. Only
  `kestrel_archive` touches minizip-ng.
- **Pipeline**, run on a worker thread with `std::stop_token` and progress callbacks:
  1. `ArchiveReader` opens the archive and finds `knx_master.xml`, `P-*/0.xml`, `P-*/project.xml` and the `M-*` folders.
  2. **Schema detection** reads the root namespace `http://knx.org/xml/project/N`. N in 11..23 is supported. Any other
     value is refused with a diagnostic.
  3. **Phase 1, stream to IR.** `QXmlStreamReader` with an element-dispatch table builds plain structs mirroring the XML
     (`IrDeviceInstance`, `IrGroupAddress`, `IrComObjectInstanceRef`…), with refs still as strings. Version differences
     are handled by a small **quirk table keyed by N** (renamed attributes, moved elements, short or long ref forms).
     Each quirk is added together with the fixture that proves it. The reader never branches on N anywhere else.
  4. **Phase 2, resolve and map.** Resolve RefIds against the program (expand short `O-1_R-2` style refs to full
     catalog keys), allocate Kestrel ids, record `knxId`, and convert GA `Address` integers and `Links` lists into `Link`
     rows. Unresolved references become **diagnostics, not exceptions**: a partially broken project still imports.
  5. Embedded M-folders go through the same knxprod indexer into the user catalog (after user confirmation).
  6. The finished `Project` value is moved to the GUI thread and becomes a fresh document. **Importing is not an undoable
     edit.**
- **Round-trip fidelity.** I propose **attribute-level preservation only**. Each entity has an `AttrBag` of unknown or
  unedited attributes (Puid, ETS flags we don't model, comments, `Status`), which a future exporter re-emits. Unknown
  **element subtrees are dropped and listed in an import report** (for example "12 `<Trade>` children ignored").
  Preserving arbitrary XML subtrees would create stale data that contradicts our edits, and it is a maintenance burden.
  The import report makes the loss visible. Export (FR-PRJ-5) generates XML from the model, not by patching the original.

---

## 6. Validation engine

- The engine is **pure functions**: `using Rule = void(*)(const ValidationInput&, DiagnosticSink&);` where
  `ValidationInput = {const Project&, const CatalogView&, const DptRegistry&}`. Rules have no state and no I/O, which
  makes them easy to unit-test with `UnitTests.hpp` on hand-built projects.
- `Diagnostic { code ("TOP-001"), severity (Error|Warning|Info), messageId + args (translated in UI), subjects:
  [EntityRef], fixIt?: std::function<void(Tx&)> }`. A fix-it is a transaction, so it is undoable for free.
- **Initial rules:**
  - TOP-001 duplicate IA.
  - TOP-002 IA out of range, or device 0 used by a non-coupler.
  - TOP-003 medium mismatch between product and line.
  - GA-001 duplicate GA.
  - GA-002 GA without DPT.
  - GA-003 DPT size mismatch between a GA and a linked object (FR-GA-4).
  - GA-004 GA has no links.
  - GA-005 GA with links but no sender (no linked object has the T or R flag).
  - CO-001 link on a com object that the current parameters hide.
  - CO-002 com object flags mismatch, for example a W-only object that receives nothing.
  - DEV-001 device without an application program.
  - DEV-002 address or association table overflow (limits from the application program).
  - DEV-003 parameter value outside its type's range.
  - BLD-001 device not placed (Info).
- **Scheduling.** Each rule declares the entity kinds it depends on. The app-side scheduler debounces ChangeSets (200 ms)
  and re-runs only the affected rules. A full run on 1000 devices / 10k GAs must stay under 20 ms, and we will add a
  benchmark test for that. If it gets slower, the scheduler runs rules on a snapshot copy on a worker thread.
  Rule code does not change either way.

---

## 7. Directory layout and CMake targets

```
src/core/knx/        kestrel_knx        STATIC  std-only: IndividualAddress, GroupAddress, DptId/registry/codec, ComObjectFlags
src/core/model/      kestrel_model      STATIC  Qt6::Core  -> kestrel_knx    entities, Id<>, Table<>, Project, Tx, History, ChangeSet
src/core/catalog/    kestrel_catalog    STATIC  Qt6::Core Qt6::Sql -> kestrel_knx, kestrel_archive
                                                ApplicationProgram (immutable), XML parser, Evaluator(choose/when), CatalogDb, CatalogService
src/core/archive/    kestrel_archive    STATIC  Qt6::Core + minizip-ng (PRIVATE)  ZipReader/ZipWriter, ContainerFs (zip|dir)
src/core/persist/    kestrel_persist    STATIC  Qt6::Core -> model, archive      native .kestrel read/write, migrations/
src/core/ets/        kestrel_ets        STATIC  Qt6::Core -> model, catalog, archive   knxproj/knxprod import (IR + mapper), export later
src/core/validate/   kestrel_validate   STATIC  -> model, catalog                rules/, Diagnostic, scheduler-free engine
src/app/...          (other architects) links Qt6::Gui/Widgets/Quick; ProjectNotifier, item models, UndoMirror
tests/unit/<lib>/    UnitTests.hpp tests per library;  tests/qt/ Qt Test for persist/ets/catalog
tests/fixtures/synthetic/   hand-written M-FFFF knxprod/knxproj trees, zipped at build time (never real vendor data)
tools/kestrel-cli/   headless CLI: import, validate, convert, dump — proves NFR-3 in CI
```
- Dependencies point only downward: `knx ← model ← {persist, validate}`, `catalog` depends on `knx` but **not** on
  `model`, and `ets` and `validate` join model and catalog. The model knows catalog keys only as strings.
- Each target has `target_compile_features(cxx_std_20)`, warnings as errors in CI, and an ASan/UBSan preset.
  The build must fail if any `src/core/*` target links Qt6::Gui; a CMake check on `LINK_LIBRARIES` enforces this.

---

## My 3 non-negotiable decisions

1. **Value-type model with transaction-only mutation.** There are no QObject entities. Every change goes through `Tx`,
   built from automatically reversible primitive ops, and emits exactly one `ChangeSet`. Undo, the dirty flag,
   autosave, validation and UI refresh all come from that one mechanism. The core never links QtGui, QtWidgets or
   QtQuick.
2. **Two identity systems, deliberately.** KNX string ids are the keys for catalog and master data. Project entities use
   Kestrel random ids, and ETS ids are kept only as `knxId` provenance. Entities refer to each other by id, never by
   pointer.
3. **Open, deterministic, versioned, self-contained format.** Sorted JSON with one file per device, zip or folder form,
   `formatVersion` with tested JSON migrations from v1, and the application programs a project uses embedded verbatim.
   Original vendor XML remains the canonical catalog source, indexed by SQLite and never committed to our repository.
