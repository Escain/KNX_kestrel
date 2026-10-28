# 06 — Roadmap

| Milestone | Content | Requirements |
|-----------|---------|--------------|
| **M0 Foundations** ✔ | CMake/presets/layering check, base (`Result`, bytes), addresses, test infrastructure, specs + ADRs | NFR-1,3,4,6 |
| **M1 Vertical slice** ✔ (2026-10-28; open items below) | knx codecs + tunnel client + IA programming + management session; DPT core; simulator + `kestrel-sim`; model/undo/persist/validate; `kestrel-cli`; GUI shell with Buildings/Topology/Group-address/Properties/Problems docks; bus connection + group monitor; Robot suites against the simulator | FR-PRJ-1,2 · FR-BLD-1,2 · FR-TOP-1,2 · FR-GA-1..4 · FR-BUS-1,2,5,6 · FR-COM-1,4 · NFR-2,5,7 |
| **M2 Catalog & import** (in progress, see below) | own ZIP reader ✔ (reader; writer open), `.knxprod` import ◐ (reader + application-program model + `catalog inspect`), user catalog (SQLite), application-program model ◐, `DynamicEvaluator`, parameter editor; `.knxproj` import (unprotected); `.kestrel` ZIP container; autosave | FR-CAT-1..3 · FR-DEV-1..3 · FR-PRJ-3,4 |
| M3 Download (BIM M112 / 0705) | memory-image builder, table builders, data-driven `ProcedureResolver → DownloadPlan → DownloadExecutor`, partial download, "needs download"; golden memory dumps | FR-COM-2,3,5 |
| M4 System B & couplers | 07B0/57B0 merged procedures, relative segments, 091A filter tables | FR-TOP-3, FR-COM-2 |
| M5 Routing, USB | routing with pacing, USB HID interfaces | FR-BUS-3,4 |
| M6 Calculated parameters | QJSEngine-based sandboxed script host | FR-DEV-1 |
| M7 KNX Secure | Data Secure + IP Secure (OpenSSL), keyring; password-protected knxproj | FR-COM-6, FR-PRJ-4 |
| M8 Export | `.knxproj` export | FR-PRJ-5 |

## M1 open items (carried forward)
- GUI: drag and drop (catalog → topology, GA → com object), preview tabs, named workspaces, GUI automation server + Robot GUI suites.
- Model: dedicated operations for renumbering areas/lines and editing com-object DPT/flags/size (the view models use `Tx::set` today).
- `tools/trace.py` traceability matrix; CI workflow; pre-commit hook for forbidden files; clang-format not installed on the dev box.
- Refs cross into QML as text ids; revisit a `Q_GADGET` `EntityRef` if it becomes a bottleneck.
- `tst_BusSession` takes about 35 s because of real protocol timeouts. Consider injecting shorter timeouts.
- IA address check: treat a negative L_Data.con as "address free" and finish early (see 04 field notes).
- GUI: a "devices in programming mode" action backed by `BusSession::readProgrammingMode`.

## M2 progress
- [x] `archive`: own in-memory ZIP reader over zlib (stored/deflate, ZIP64, CRC-32, UTF-8/CP437 names, data
      descriptors; encryption, unknown methods, path traversal, size lies and zip bombs fail cleanly). Fixtures from
      `tests/data/archive/make_fixtures.py`, 1200 deterministic mutations per run.
- [x] `catalog`: `.knxprod` → `ProductDatabase` (hardware, products, catalog items, application programs with
      segments, parameter types, parameters/unions, parameter refs, com objects/refs, tables, load procedures, Dynamic
      tree + per-channel index, translations); merged com-object view; `kestrel-cli catalog inspect` (`07-catalog.md`).
- [ ] `ModuleDefs` (module-based channels) and `knx_master.xml` (mask versions, DPTs) are noted but not parsed.
- [ ] Validation against real vendor files through `KESTREL_PRIVATE_CORPUS` (MEAN WELL M-0162 actuator, push button).
- [ ] ZIP writer (byte-deterministic), `.kestrel` ZIP container; user catalog (SQLite); `DynamicEvaluator`;
      instantiating a product into the topology; parameter editor; `.knxproj` import; autosave.

## M3/M4 progress (2026-10-28)
- [x] `download` library: Plan, Resolver (mask procedure + MergedProcedure merges + images), sans-IO Executor
      (authorize, load-state checks, verified memory writes, PID 27 memory control blocks).
- [x] knx_master.xml mask procedures parsed by `catalog`; `kestrel-cli device download` (with `--dry-run`).
- [x] Simulator: System B load state machines, relative allocation, authorization.
- [x] Real hardware: MEAN WELL KAA-8R reloaded with its ETS6 images; channel F switches (specs/04 field notes).
- [ ] Generate the images from the product file and a project: parameter image (DynamicEvaluator for
      choose/when), group object table, address and association tables. Acceptance test: the bytes are
      identical to the ETS6 images.
- [ ] Push-button sensor: catalog, links to 1/0/6 and 2/0/6, download.
