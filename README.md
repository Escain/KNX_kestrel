# Kestrel

An open, cross-platform configuration tool for KNX home and building automation installations,
written in C++20 with Qt 6 (Widgets shell + QML views) and CMake.

> Working name. KNX is a trademark of the KNX Association; this project is not affiliated with it.

## Status
Milestone **M1 (vertical slice)** in progress: see [`specs/06-roadmap.md`](specs/06-roadmap.md).

## Build
Requirements: CMake ≥ 3.24, Ninja, a C++20 compiler (GCC 14 / Clang 19), Qt ≥ 6.8
(Core, Network, Gui, Widgets, Qml, Quick, QuickControls2, QuickWidgets, Test), Python 3 + Robot Framework for acceptance tests.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Presets: `debug`, `debug-clang`, `release`, `asan`, `headless` (core + CLI only).

## Layout
| Path | Content |
|------|---------|
| `specs/` | requirements, architecture, ADRs, brainstorm notes |
| `src/<lib>/` | libraries (see `specs/02-architecture.md` for tiers) |
| `apps/` | `kestrel` (GUI), `kestrel-cli`, `kestrel-sim` (KNXnet/IP simulator) |
| `tests/` | `unit/` (UnitTests.hpp), `qt/` (Qt Test), `robot/` (Robot Framework), `data/` (synthetic fixtures) |
| `third_party/` | vendored `UnitTests.hpp` |

## Clean-room policy
No ETS binaries are decompiled and no proprietary data files are committed. See [`specs/adr/009-clean-room.md`](specs/adr/009-clean-room.md).
