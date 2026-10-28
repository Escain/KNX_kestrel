# 05 — Test strategy

Status: **v1.0** (2026-10-28). Origin: `brainstorm/round1-quality.md` and the round-2 amendments.

## 1. Principles
- **No hardware in CI (NFR-5).** Every bus feature lands together with a simulator-backed test.
- **Deterministic.** Time, random ids and ports are injected. Unit tests have no sleeps, and real sockets appear only at the Qt-test level, on loopback with port 0.
- **Clean-room test data.** Fixtures in `tests/data/` are hand-written from the public spec (fake manufacturer `M-FFFF`).
  Real vendor files are used only through `KESTREL_PRIVATE_CORPUS` (label `private-corpus`, skipped when the variable is unset).

## 2. Pyramid and tools

| Level | Tool | What | ctest name / label |
|-------|------|------|--------------------|
| Unit | `UnitTests.hpp` (author's library) | T0/T1 pure logic: codecs, state machines (manual clock), model, persist, validation | `unit.<lib>` / `unit` |
| Integration | Qt Test (`QTEST_GUILESS_MAIN`) | Event loop, sockets, threads: `bus` ⇄ `simnet` over loopback UDP, `app` services | `qt.<lib>.<Name>` / `qt` |
| UI component | Qt Test + `QAbstractItemModelTester`; Qt Quick Test with fake view models | view models, item models, QML components | `qt.viewmodels.*`, `qml.*` / `gui` |
| Acceptance | Robot Framework | `kestrel-cli` against a `kestrel-sim` process; later GUI smoke tests through the automation socket | `robot.<suite>` / `robot` |
| Manual HIL | checklist | Real IP interface and devices, before each release | — |

When to choose Qt Test over UnitTests: the code needs an event loop, `QSignalSpy`, timers or sockets.

## 3. UnitTests.hpp rules
- One binary per library (`unit_<lib>`), `main` from `tests/support/unit_main.cpp`.
- `ASSERT_EQ(actual, expected)`: never put a string literal first (it would compare pointers), and wrap expected
  literals in the actual type (`std::string("x")`, `std::uint16_t{3}`). Bytes are compared as `std::vector<uint8_t>` (hex printer).
- Asserts work only inside `UNIT_TEST` bodies; helpers return `bool`/diagnostic strings.
- Put braces around `ASSERT` inside `if`/`else`.
- Include `<Printers.hpp>` / `<QtPrinters.hpp>` before `<UnitTests.hpp>`, and write tests at file scope.
- `MUST_DIE` tests go in a separate `death_<lib>` binary (label `death`, excluded under ASan).
- Suggested upstream improvements for the author: `--list`, JUnit output, exact-match filter, `NO_COLOR`; `1u:0u` in
  `countPassed += …` (`-Wsign-conversion`); guard the `-Wclobbered` pragma with `!defined(__clang__)`. Until then the
  header is included as a SYSTEM header.

## 4. Traceability
- Robot: tag = requirement id (`FR-COM-1`).
- UnitTests: `UNIT_TEST(FR_GA_1__parsesThreeLevel)` (the double underscore ends the id).
- Qt Test: slot `fr_bus_2__reconnects()`.
- QML: `test_FR_DEV_1__…`.
- `tools/trace.py` (M2) generates the requirement → tests matrix and fails on unknown ids.

## 5. Running
```
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
ctest --preset debug -L unit            # one level
./build/debug/tests/unit/model/unit_model -v -f=FR_PRJ_2__     # one test family
python3 -m robot --variable CLI:build/debug/apps/kestrel-cli/kestrel-cli ... tests/robot/suites/x.robot
```
Presets: `debug`, `debug-clang`, `release`, `asan` (clang ASan+UBSan), `headless` (no GUI). All use `-Werror`.

## 6. CI gates (to be set up when a remote exists)
A `debian:trixie` container (same Qt 6.8.2 as the dev box) runs:
- lint: clang-format, forbidden-files hook, qmllint
- gcc-debug: all labels except `hil`/`perf`
- clang-asan
- clang-tidy
- release

Later: coverage (knx ≥ 90 %, other core libraries ≥ 80 %), fuzzing of the frame and XML parsers, a perf job for NFR-8 (1k devices / 10k GAs), and Windows.
