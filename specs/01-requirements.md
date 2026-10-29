# 01 — Vision & Requirements

Status: **Draft v0.1** (2026-10-28)
Working name: **Kestrel** — an open configuration tool for KNX installations.
(The name is a placeholder; it avoids using the "KNX" trademark as a product name.)

## 1. Problem

KNX is an open standard (EN 50090 / ISO/IEC 14543-3) for home and building automation.
In practice you can only commission KNX devices (assign individual addresses, download
application programs, parameters, group-address tables) with **ETS**, the KNX Association's
proprietary, Windows-only, paid tool. Hobbyists, small integrators and Linux users have
no open alternative that covers the full commissioning workflow.

## 2. Goal

A cross-platform (Linux first, Windows, macOS) desktop application, written in **C++20 / Qt 6 /
CMake**, that lets a user:

1. Model a house: buildings, floors, rooms (topology of *places*).
2. Model the bus: areas, lines, devices (topology of *the network*).
3. Import manufacturer device descriptions (`.knxprod`) and existing ETS projects (`.knxproj`).
4. Configure devices: parameters (dynamic parameter dialogs), communication objects.
5. Define group addresses (3-level / 2-level / free) and link them to communication objects.
6. Connect to a real bus (KNXnet/IP tunnelling/routing, later USB HID) and:
   - program individual addresses (programming-button flow),
   - download application program, parameters, address/association tables,
   - monitor group telegrams and read/write group values (group monitor).
7. Save everything in an open, diff-friendly native project format.

## 3. Non-goals (for now)

- Being a drop-in replacement for every ETS feature (plugins/DCA apps, ETS Apps, reports, labels).
- Signing or creating `.knxprod` files (manufacturer tooling).
- Online catalog download from the KNX Association.
- Cloud features.

## 4. Clean-room policy (mandatory)

- **No decompilation / disassembly** of ETS binaries. The ETS6 installer in the repo root is used
  only as UX inspiration (screens, workflows, module decomposition by file names).
- Proprietary data files (e.g. `knx_master.xml`, `.knxprod`, `.knxproj`) are **never committed**
  (see `.gitignore`). The application loads `knx_master.xml` at runtime from the user's own
  `.knxproj`/`.knxprod` archives, which embed it.
- Protocol knowledge comes from public sources: the KNX Standard (free download after registration),
  EN 50090, and open-source implementations used as references (knxd, Calimero, xknx) — respecting
  their licenses (no copying of GPL code into this project unless the license is compatible with ours).

## 5. Functional requirements

IDs are referenced by tests and the traceability matrix (`specs/05-test-strategy.md`).

### Project
- **FR-PRJ-1** Create / open / save / close a project in the native format.
- **FR-PRJ-2** Undo/redo for every model mutation.
- **FR-PRJ-3** Autosave and crash recovery.
- **FR-PRJ-4** Import `.knxproj` (unencrypted first; password-protected ZIP later).
- **FR-PRJ-5** Export `.knxproj`-compatible XML (stretch goal).

### Buildings (places)
- **FR-BLD-1** Tree of Building → Floor → Room → (Distribution board / Corridor / Stairway…).
- **FR-BLD-2** Assign devices to rooms (a device has exactly one place, optional).

### Topology
- **FR-TOP-1** Areas (0–15), lines (0–15), devices (0–255); individual address `A.L.D`.
- **FR-TOP-2** Validate address uniqueness, medium type (TP, IP, RF) per line.
- **FR-TOP-3** Couplers / IP routers as line members.

### Catalog
- **FR-CAT-1** Import `.knxprod` (ZIP of manufacturer XML + embedded `knx_master.xml`).
- **FR-CAT-2** Browse/search the catalog (manufacturer, product, application program version).
- **FR-CAT-3** Instantiate a product into the topology.
- **FR-CAT-4** Find common products by brand, product or order number in a built-in index and download their
  product file from the manufacturer (the index ships, the files do not; ADR-009).

### Device configuration
- **FR-DEV-1** Render the parameter dialog from the application program description
  (ParameterBlocks, ParameterSeparators, `choose/when` dynamic sections, parameter types:
  enum, number, float, text, time, IP, color, checkbox…).
- **FR-DEV-2** Communication objects become visible/active according to parameter state.
- **FR-DEV-3** Per-device state: "parameters modified", "needs download", "address programmed".

### Group addresses
- **FR-GA-1** 3-level (main/middle/sub), 2-level and free-level styles.
- **FR-GA-2** Main / middle groups as folders with names.
- **FR-GA-3** Link GA ↔ communication object (many-to-many, first link = sending address).
- **FR-GA-4** DPT assignment and compatibility checking (size & flags).

### Bus access
- **FR-BUS-1** Discover KNXnet/IP interfaces (SEARCH_REQUEST multicast).
- **FR-BUS-2** KNXnet/IP tunnelling connection (cEMI L_Data).
- **FR-BUS-3** KNXnet/IP routing (multicast 224.0.23.12).
- **FR-BUS-4** USB HID interfaces (later milestone).
- **FR-BUS-5** Group monitor: live telegram list, decode with DPT, filter, export CSV.
- **FR-BUS-6** Group read / write / response from the UI.

### Commissioning
- **FR-COM-1** Program individual address (programming-mode flow).
- **FR-COM-2** Full download (application, parameters, address table, association table).
- **FR-COM-3** Partial download (parameters only / tables only).
- **FR-COM-4** Device info read (mask version, manufacturer, serial, program version).
- **FR-COM-5** Unload / restart device.
- **FR-COM-6** KNX Data Secure / IP Secure (later milestone).

## 6. Non-functional requirements

- **NFR-1** C++20, CMake ≥ 3.24, Qt ≥ 6.8 (needed for Qt Quick TreeView maturity; dev box: Qt 6.8.2, GCC 14; Debian trixie ships 6.8.2).
- **NFR-2** UI = `QMainWindow` with dock widgets; each dock/central widget hosts a QML scene
  (`QQuickWidget`). Business logic is **not** in QML.
- **NFR-3** Core library (model, KNX protocol, file formats) must not depend on QtWidgets/QtQuick,
  so it is testable headless and reusable from a CLI.
- **NFR-4** Testing: unit tests with `UnitTests.hpp` (author's library) for pure C++ code;
  Qt Test for Qt-dependent code (models, QML via Qt Quick Test); Robot Framework for
  high-level acceptance tests.
- **NFR-5** Every bus operation can run against a simulated bus/device so it is testable in CI
  without hardware.
- **NFR-6** Builds warning-free with `-Wall -Wextra -Wpedantic`; ASan/UBSan presets.
- **NFR-7** UI responsive: bus I/O and file import never block the GUI thread.
- **NFR-8** Large projects: 1,000 devices / 10,000 group addresses stay fluid.
- **NFR-9** Internationalisation-ready (Qt `tr()`/`qsTr()`), English first.
- **NFR-10** License: to be chosen by the author (MIT suggested, matching `UnitTests.hpp`).
