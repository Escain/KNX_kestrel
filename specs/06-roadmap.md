# 06 — Roadmap

| Milestone | Content | Requirements |
|-----------|---------|--------------|
| **M0 Foundations** ✔ | CMake/presets/layering check, base (`Result`, bytes), addresses, test infrastructure, specs + ADRs | NFR-1,3,4,6 |
| **M1 Vertical slice** ✔ (2026-10-28; open items below) | knx codecs + tunnel client + IA programming + management session; DPT core; simulator + `kestrel-sim`; model/undo/persist/validate; `kestrel-cli`; GUI shell with Buildings/Topology/Group-address/Properties/Problems docks; bus connection + group monitor; Robot suites against the simulator | FR-PRJ-1,2 · FR-BLD-1,2 · FR-TOP-1,2 · FR-GA-1..4 · FR-BUS-1,2,5,6 · FR-COM-1,4 · NFR-2,5,7 |
| M2 Catalog & import | own ZIP reader, `.knxprod` import, user catalog (SQLite), application-program model, `DynamicEvaluator`, parameter editor; `.knxproj` import (unprotected); `.kestrel` ZIP container; autosave | FR-CAT-1..3 · FR-DEV-1..3 · FR-PRJ-3,4 |
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
