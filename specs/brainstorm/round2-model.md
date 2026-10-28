# Brainstorm round 2: Data model and persistence architect

Read: round1-protocol/ui/quality. Re-checked the dev box: no `tl/expected.hpp` and no zip library headers.
OpenSSL 3 (`evp.h`), zlib, `7z`, `zip`/`unzip` and python3 are present.

## 1. What I endorse
- **Protocol:** sans-IO zero-Qt core with an injected clock, plan/execute downloads, a derived `DeviceImage`, modules expanded at parse
  time, hard-coded DPT codecs with master data only for metadata, and synthetic `M-FFFF`/`test-master.xml` fixtures.
- **UI:** one shared engine, VMs in `kestrel_viewmodels` (no Quick or Widgets), row-level signals with no `modelReset` on edits,
  `EntityRef` across the QML boundary, and parameter-page diffing by ref id.
- **Quality:** per-layer STATIC targets with link-closure guards, `-Wconversion`, ASan/UBSan, the forbidden-files hook,
  `operator<<` on every value type, `kestrel-cli`+`kestrel-sim` as the Robot backbone, and requirement ids in test names.
- All three of us independently settled on storing non-default params, links and a download hash, and deriving the rest. This is settled.

## 2. Rulings on the known tensions
1. **Qt-free protocol vs. QtCore in core: both, split by target.**
   - **Zero-Qt:** `kestrel_base` (Result, Error, Id<>, assert), `kestrel_knx` (codecs, DPT, state machines, `DeviceImage`, `LdCtrl` structs)
     and the `kestrel_sim` core. This overrides the quality table, which listed knx and base as QtCore.
   - **QtCore allowed:** `kestrel_model`, `kestrel_catalog`, `kestrel_formats` and `kestrel_validate`.
   - **Bridge:** catalog parses master/app `LdCtrl*`, Resources and parameter locations into **std-only structs declared in knx**. The image
     builder and evaluator live in catalog, the planner and executor in knx, so data flows only from Qt code to std code.
   - Zero-Qt libs log through an injected `LogSink`. A Qt adapter maps it to `QLoggingCategory`.
2. **ZIP: I withdraw minizip-ng. Ruling: own `kestrel_archive`** (zero-Qt, about 1.2 kLOC) on zlib plus OpenSSL EVP (PBKDF2-HMAC-SHA1,
   AES-CTR, HMAC-SHA1 for WinZip-AES AE-1/AE-2). OpenSSL is needed for KNX Secure anyway.
   - Reader: central directory, ZIP64, methods 0/8/99, data descriptors.
   - Writer: deterministic (sorted entries, fixed DOS time, no extras), so `.kestrel` determinism comes for free.
   - Hardening: zip-slip checks, size and ratio limits, CRC checks, and a libFuzzer harness from day one.
   - Oracles: checked-in *synthetic* AES archives made once with `7z a -tzip -mem=AES256`, and `unzip -t` on our writer's output.
   - Offline, we cannot FetchContent anything, and vendoring 15 kLOC to read 4 record types is the worse trade-off.
3. **Undo: one stack, chosen by the host. Transactions are the commands.**
   - Mutation stays `project.edit(label, [](Tx&){…})`, built from reversible primitive ops, with nested folding and `mergeKey`.
     This replaces the UI proposal's hand-written `core::Command apply/revert`.
   - A committed transaction goes to an `IUndoSink`. The CLI and tests use the core `LinearHistory`. The GUI uses `QUndoStackSink`
     (in viewmodels), which pushes a `TxUndoCommand`: its first `redo()` is skipped and `mergeWith` calls `Tx::tryMerge`.
   - The GUI keeps `createUndoAction`, cleanIndex and QUndoView, and there is never a second stack.
   - **`beginMacro` is banned.** Batching is nested `edit()` inside `kestrel_app` services, so the CLI and GUI produce identical undo steps.
4. **Errors: own `kestrel::Result<T, E = Error>` in base** (about 200 lines, the `std::expected` API subset: `has_value`, `value`, `error`,
   `*`, `->`, `and_then`, `transform`, `or_else`, `value_or`, `kestrel::unexpected`).
   - tl::expected cannot be fetched offline and is 2.4 kLOC to audit. C++23 becomes a `using` swap; file an ADR after checking MSVC and
     Apple libc++.
   - `Error` is zero-Qt: `{ErrorCode code; std::string detail; std::vector<std::string> context}`. UI text comes from `code` via `tr()`,
     never from `detail`. This corrects the quality proposal, which used `QString`.
5. **Ids: random 64-bit stays the identity. Readability belongs in tooling.**
   - Copying labels ("1/2/3 Kitchen") into referencing files would make one GA rename touch N device files, which is exactly the merge
     churn we designed away.
   - Instead: short type-prefixed ids (`ga-3k9x2m7v1abqz`), each entity's own record carrying its address and name, and
     **`kestrel-cli textconv`** as a git diff driver (`*.kestrel diff=kestrel`) that renders ids as `1/2/3 "Kitchen light"`.
   - UI `EntityRef{kind, uuid}` becomes `{kind, quint64 id}`, the same value as on disk. No `QUuid`.

## 3. Other conflicts and resolutions
- **Targets:** I accept quality's `kestrel_formats` (my persist+ets as subdirectories) and add `kestrel_archive`, `kestrel_catalog` and
  `kestrel_validate`. `kestrel_app` hosts the edit services shared by the CLI and GUI.
- **"Unknown ParameterType → hard import error"** is too strict. Instead the program is marked *unsupported* with a diagnostic, its devices
  get read-only params, and the rest of the knxproj still imports.
- **Module expansion is in-memory only.** Stored refs stay ETS-mangled (`…_MD-1_M-3_MI-2_P-17`), and the embedded XML stays verbatim,
  so import and export round-trip.
- **Download hash cost:** the image hash needs the program parsed, but the tree badges 1000 devices. Store `downloadedInputsHash`
  (params, links, IA; cheap, drives the badge) and `downloadedImageHash` (authoritative, checked at load or before a download).
  If the inputs differ but the image is equal, the badge clears.

## 4. Missing pieces that will bite us
1. **Qt model ordering:** `beginInsertRows` must run *before* the change, but the ChangeSet arrives *after* commit. Each model keeps a
   **shadow index** (ids and rows) mutated between begin and end. `data()` reads `Project` by id. Ordered ChangeSet ops carry `{parent, row}`.
2. **Non-undoable state:** IA programmed, download hashes and serial come from the bus, and undo cannot un-program a device.
   `project.recordState(Tx)` emits a ChangeSet, marks the project dirty and **bypasses the undo sink**. Undoing a param edit then shows
   "needs download" again.
3. **Threading contract:** `Project` is mutated only on the GUI thread. Workers (import, validation, save, planning) use copy-on-write
   snapshots. Bus results return as queued `recordState`. Add a TSan preset with a model-only test.
4. **Calculated params (QJSEngine) and `Assign`** must write inside the same transaction as the triggering edit, or undo leaves them
   inconsistent. Script output must be deterministic, with a test per fixture.
5. **Content language:** we must specify `ProjectSettings.contentLanguage`, the fallback order (project → product default → en-US →
   first available), and the FTS indexing language.
6. **Untrusted input:** archives and vendor XML (BOM, huge files, deep nesting) need limits, fuzz harnesses and no external entities.
7. **The format spec is a deliverable:** `specs/formats/kestrel-v1/` with JSON Schemas validated in tests, plus golden files for each
   migration. Without them, "open format" means nothing.
