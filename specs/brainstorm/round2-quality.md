# Round 2 — Quality architect: endorsements, rulings, gaps

Evidence: I ran a spike in the scratchpad on 2026-10-28 with GCC 14, clang 19 and Qt 6.8.2. It built a QObject/QAbstractListModel/QML_ELEMENT
target, a `qt_add_qml_module`, a QDockWidget hosting a QQuickWidget, and a Qt Test, all with my full warning set plus `-Werror`.
Both compilers built everything cleanly (moc output included) and `all_qmllint` passed. `-Wsign-conversion` fires inside Qt headers only via a
plain `-I`; imported Qt targets are SYSTEM includes, so they stay clean. `UnitTests.hpp` fails under `-Werror` as non-SYSTEM (GCC:
`-Wsign-conversion` at `countPassed += …?1:0`; clang: `-Wunknown-warning-option` on the `-Wclobbered` pragma). `MUST_DIE(std::abort())` passes under ASan+UBSan.

## 1. Endorsed (adopted as-is)
- **Protocol:** sans-I/O state machines with a `ManualClock`. I also endorse the plan → execute split, with `kestrel-cli plan` output usable as **golden files**.
  Further: synthetic `test-master.xml` and fake manufacturer `M-FFFF` fixtures, an opt-in `KESTREL_CORPUS_DIR`, golden memory dumps,
  and fault rules in `VirtualBus`.
- **Model:** value types and `Tx` with automatically reversible ops, which make undo testable headless with UnitTests. Also: one `ChangeSet`;
  stateless validation rules (`Rule(const Input&, Sink&)`), the ideal unit-test shape; and deterministic JSON with JSON→JSON migrations, each with a golden test.
- **UI:** `QAbstractItemModelTester` on every model; VM fakes deriving from abstract `QML_UNCREATABLE` bases, for Qt Quick Test;
  `objectName` + `Accessible.name` on every interactive item, CI-checked; interaction through `QAccessibleInterface` rather than pixels;
  `QT_QPA_PLATFORM=offscreen` + `QT_QUICK_BACKEND=software` in CI; JSON-RPC 2.0 over `QLocalServer` with `app.waitIdle`
  (replaces my ad-hoc JSON protocol; its method list is adopted).
- All four proposals agree that **Robot tests drive `kestrel-cli` + the simulator first and GUI automation stays thin**, so that is settled.
## 2. Conflicts and rulings
1. **Qt-free core vs QtCore in core → three tiers, each enforced by a CMake link-closure check (`kestrel_check_tier(<target> <tier>)`):**
   - **T0, std-only:** `kestrel_knx` covers addresses, cEMI/TPDU/APDU, KNXnet/IP, DPT, tunnel/management/download state machines, and the
     `MemoryImageBuilder`/`Planner`. Alongside it, `kestrel_sim` holds `VirtualBus` and `VirtualDevice`.
   - **T1, QtCore (+Xml, Sql for the catalog index, Network for adapters only):** model, catalog, archive, persist, ets, validate,
     `kestrel_app` services, `kestrel_bus_qt`, `kestrel_sim_qt`.
   - **T2, Gui/Qml/Quick/Widgets:** viewmodels (no Quick or Widgets), qml modules, shell, automation.

   Consequence for the protocol/model seam: T1 catalog code flattens an `ApplicationProgram` into a T0 **`DeviceLoadSpec`**
   (segments, typed parameter locations and values, tables, resolved `LdCtrl` list). Image building and download are then unit-testable from hand-built specs, with no XML.
   Also: `Result`/`Error` move to T0. `Error` holds a code, a UTF-8 message and context, and is translated in T1+ by code. This retracts my QString `Error`.
2. **Dependencies (offline box: no tl/expected, no minizip or libzip headers; only zlib, libdeflate, zstd, OpenSSL, sqlite3):**
   **no FetchContent in the default build, ever** (reproducible, offline, distro-friendly).
   - **Own `kestrel::Result<T,E>`** (T0, about 200 lines on `std::variant`). It mirrors the `std::expected` API subset we use (`has_value`, `value`, `error`,
     `*`, `->`, `and_then`, `transform`, `or_else`), so the C++23 swap is an alias change. It gets exhaustive unit tests and an `operator<<`.
   - **Own ZIP in `kestrel_archive` over zlib** (raw inflate/deflate, central directory, zip64 read, CRC). It is also needed for the model's
     deterministic writer (fixed timestamps and order). WinZip-AES via OpenSSL comes later, behind FR-PRJ-4 phase 2. Because we own a parser
     of hostile input, it **must** ship with a libFuzzer harness and be cross-checked against Python's independent `zipfile`.
     Python builds the fixtures, and a ctest verifies that Python reads our output.
   - Qt Sql (QSQLITE) is allowed only in `kestrel_catalog`. The CI image needs `libqt6sql6-sqlite`. Windows risk: zlib is not provided by Qt,
     so it is planned via vcpkg or a vendored source.
3. **Simulator naming and location → `kestrel-sim`** (not `simd`: it is a foreground child of a test harness, not a daemon). It lives in
   `apps/kestrel-sim/`, built on `src/core/sim` (T0) plus `src/core/sim_qt` (T1 `KnxIpServer`). Defaults: `--port 0` with a `{"event":"ready","port":N}`
   line on stdout, and control by JSON-lines on **stdin/stdout** (dies with its parent, no port allocation). `--control tcp:127.0.0.1:0` is optional, for manual use.
   The GUI gains `--simulated-bus=<scenario.json>` (in-process `VirtualBus`), which backs `bus.sim.inject`/`bus.sim.sent` in GUI Robot tests and demos.
4. **`-Wconversion -Wsign-conversion -Werror` from day one: yes,** as verified above. Rules:
   Qt is always linked through imported targets, never as raw include paths; `escain::unittests` is a SYSTEM INTERFACE target (mandatory).
   Use `qsizetype` with Qt containers plus a checked `kestrel::narrow<T>()`; a bare narrowing `static_cast` needs a comment.
   Upstream fixes for the UnitTests.hpp author: `1u:0u`, and guard the pragma with `!defined(__clang__)`.
   `-Werror` stays behind `KESTREL_WARNINGS_AS_ERRORS` (ON in presets and CI).
5. **Undo:** the model's `History` is authoritative and the UI's per-project `QUndoStack` is rejected. Undo is tested with UnitTests at T1,
   and the UI binds `QAction`s to `History`.
6. **Qt minimum:** `qt_standard_project_setup(REQUIRES 6.8)` (UI) wins over my 6.5. An untested minimum is fiction, and CI (Debian trixie) has 6.8.2.
   NFR-1 should be amended to Qt ≥ 6.8.
7. **Layout (merged):** `src/core/{knx,sim,sim_qt,model,catalog,archive,persist,ets,validate,app,bus_qt}`, `src/ui/{viewmodels,qml,shell,automation}`,
   `apps/{kestrel,kestrel-cli,kestrel-sim}` (the CLI is not under `tools/`), `tests/{unit/<lib>,qt/<lib>,qml,robot,fixtures,fuzz}`.
   Robot libraries: `KestrelCli.py`, `KestrelSim.py`, `KestrelGui.py` (renamed from the UI's `KestrelLibrary.py`).
   Diagnostic codes follow the model's `TOP-001` scheme; my sketch's `TOP-DUP-IA` is dropped.
8. **Automation server:** a compile-time `KESTREL_ENABLE_AUTOMATION` (ON in dev/CI presets, **OFF in the release preset and packages**) *and* the
   runtime `--automation=` flag. That is stricter than the UI's environment-variable escape hatch.
9. **Logging at T0:** `QLoggingCategory` cannot be used there. T0 exposes `kestrel::log::setSink(fn(level, category, msg))`, and T1 installs a
   sink forwarding to `QLoggingCategory` under the same `kestrel.*` names. Robot attaches the `kestrel.bus.telegram` output on failure.
## 3. Missing pieces that will bite us
- **Hidden nondeterminism** breaks golden and Robot tests. The model's *random* ids need an injectable `IdGenerator` (seeded in tests,
  `kestrel-cli --deterministic-ids`). `History`'s 1 s merge window, the 200 ms validation debounce, autosave and monitor ticks must all take the
  injected clock. Wall time is banned in T0/T1 (grep for `steady_clock::now` / `QDateTime::currentDateTime` outside `Clock` impls).
- **Golden-file workflow:** one helper (`tests/support/Golden`) with a canonical JSON/hex diff and `KESTREL_UPDATE_GOLDEN=1` regeneration,
  reviewed in PRs. It is used for plans, memory images, migrations and `.kestrel` output.
- **Fixture ownership:** `M-FFFF` `.knxprod`/`.knxproj` and `test-master.xml` sit on the critical path of P3, the catalog and import. They need one owner, a generator in
  `tools/fixtures/` (Python, independent of our ZIP code) and a schema-coverage checklist (every ParameterType, union, choose/when, module, LoadProcedure style).
- **Fuzzing is not optional:** we own parsers for ZIP, XML IR, cEMI/KNXnet/IP frames and the JSON project format. Add a `fuzz` clang preset, seed corpora from
  unit vectors, a 5-minute nightly run and a 30 s PR smoke.
- **Performance budgets** are spread across proposals (validation < 20 ms at 1k/10k, 5 000 tel/s with < 50 ms latency, window < 1 s).
  They need generated large fixtures, a `perf` label, and a nightly run on one fixed runner with trend output. **Never gate PRs on timing.**
- **Concurrency:** BusThread, the SPSC telegram ring, worker import and snapshot autosave run on separate threads. Add a nightly **TSan** job on T0/T1 tests only
  (Qt is uninstrumented, so GUI tests stay out), plus a stress test that hammers the ring.
- **JS calculated parameters** (protocol risk 2) pull in `QJSEngine`. They belong in their own T1 library, `kestrel_script` (Qt6::Qml, never Quick),
  an explicit tier exception, with a sandbox test proving there is no file or network access.
- **Real-hardware gap:** the simulator encodes *our reading* of the spec. Keep a `hil` Robot suite (`${SIM_URL}` swapped for a real interface),
  a per-release checklist, and golden ETS-programmed dumps (the only oracle for "inactive parameter" semantics).
  **Windows:** `MUST_DIE` on MSVC (C2712, `/EHa`), zlib, `QLocalServer` naming. Add a non-blocking MSVC job from milestone 1.
