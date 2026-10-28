# Kestrel

An open, cross-platform configuration tool for KNX home and building automation installations,
written in C++20 with Qt 6 (Widgets shell + QML views) and CMake.

> Working name. KNX is a trademark of the KNX Association; this project is not affiliated with it.

## Status
M1 (vertical slice) done; M2/M3 under way: see [`specs/06-roadmap.md`](specs/06-roadmap.md).

On real hardware so far (a Gira KNX/IP interface and a MEAN WELL KAA-8R System B actuator), Kestrel can:
- find the interface, tunnel, monitor the bus, and read and write group values;
- program individual addresses and read device information;
- read a configured device back into a Kestrel project;
- generate its configuration from the manufacturer's product file, byte-identical to ETS6
  ([`specs/09-memory-images.md`](specs/09-memory-images.md));
- download that configuration.

Only System B devices (mask 07B0) can be configured so far.

## Build
Requirements: CMake ≥ 3.24, Ninja, a C++20 compiler (GCC 14 / Clang 19), Qt ≥ 6.8
(Core, Network, Gui, Widgets, Qml, Quick, QuickControls2, QuickWidgets, Test), Python 3 + Robot Framework for acceptance tests.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Presets: `debug`, `debug-clang`, `release`, `asan`, `headless` (core + CLI only).

## Configuring a device from the command line
Product files (`.knxprod`) come from the device manufacturer. Keep them outside the repository.

```sh
CLI=build/debug/apps/kestrel-cli/kestrel-cli
IF=--interface 192.168.1.10          # the KNXnet/IP interface (kestrel-cli bus discover finds it)

$CLI catalog inspect product.knxprod                      # products, applications, com objects
$CLI project new house --name "My house"
AREA=$($CLI topology add-area house 1 Home)
LINE=$($CLI topology add-line house $AREA 1 Main)
DEV=$($CLI topology add-device house $LINE "Actuator" --number 2 --product <product id> --application <app id>)
$CLI device param house $DEV <ParameterRef id> 1          # parameters (ids from the product file)
$CLI device link house $DEV 107 1/0/6 --knxprod product.knxprod   # com object number → group address
$CLI device program-ia 1.1.2 $IF                          # press the device's programming button
$CLI project download house $DEV --knxprod product.knxprod --dry-run   # review the steps
$CLI project download house $DEV --knxprod product.knxprod $IF         # writes to the device
$CLI group write 1/0/6 1.001 on $IF
```

To take over a device that another tool already configured:
`$CLI device import 1.1.2 --knxprod product.knxprod --project house $IF`. This is read-only.

`kestrel-sim` simulates an installation, including System B devices, for trying all of this without hardware.
Set `QT_LOGGING_RULES="kestrel.bus.frames.debug=true"` to trace every KNXnet/IP frame.

## Layout
| Path | Content |
|------|---------|
| `specs/` | requirements, architecture, ADRs, brainstorm notes |
| `src/<lib>/` | libraries (see `specs/02-architecture.md` for tiers): `knx` protocol, `catalog` product files, `appconfig` parameters and memory images, `download` load procedures, `app` bus session, GUI layers |
| `apps/` | `kestrel` (GUI), `kestrel-cli`, `kestrel-sim` (KNXnet/IP simulator) |
| `tests/` | `unit/` (UnitTests.hpp), `qt/` (Qt Test), `robot/` (Robot Framework), `data/` (synthetic fixtures) |
| `third_party/` | vendored `UnitTests.hpp` |

## Clean-room policy
No ETS binaries are decompiled and no proprietary data files are committed. See [`specs/adr/009-clean-room.md`](specs/adr/009-clean-room.md).
