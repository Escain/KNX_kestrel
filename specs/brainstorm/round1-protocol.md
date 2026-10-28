# Brainstorm R1: KNX protocol & domain (architect: protocol expert)

Status: proposal, round 1. Sources: the public KNX Standard (vol. 3: 3/3/x layers, 3/5/1 resources,
3/5/2 management procedures, 3/7/2 DPTs, 3/8/x KNXnet/IP), plus the *structure* of `knx_master.xml`
(ns `project/23`) as shipped with ETS6. No ETS binaries were inspected.

Findings from `knx_master.xml` that drive this design:
- 34 `MaskVersion`s, each with a `ManagementModel` (`Bcu1`, `Bcu2`, `BimM112`, `SystemB`, `PropertyBased`, `None`).
  Each one lists `Resources` (named, with `Location AddressSpace=StandardMemory|SystemProperty|RelativeMemory|Pointer|Constant|LcFilter|…`
  and a `Flavour` such as `AddressTable_SystemB`, `LoadControl_M112`, `GroupObjectTable_M112`) and `Procedures` built from 23 `LdCtrl*` ops.
- `MV-0705` (BIM M112) has **only an Unload procedure** in master. The application's own `Static/LoadProcedures` must supply Load.
  `MV-07B0`/`57B0` (System B) have full Load procedures (`all`, `ap1`, `par`, `grp`) with `LdCtrlMerge MergeId=n` splice points.
  `MV-091A` (coupler) writes `LcFilter` memory and uses `LdCtrlClearLCFilterTable`.
- So the master file is a **machine-readable device-access spec**. We should interpret it rather than hard-code mask behaviour.

## 1. Stack layering

Rule: **sans-IO core**. Every codec and protocol state machine is pure C++20 (`kestrel_knx` static lib, no Qt at all).
State machines take events (`onFrame`, `onTimeout`, `request`) and emit actions (`SendFrame`, `StartTimer`, `Complete`).
Time comes from an injected `IClock`/`ITimerService`. Qt shows up only in thin I/O adapters (`kestrel_knx_qt`).

| Layer | Namespace / key types | Qt? |
|---|---|---|
| Addresses & bytes | `IndividualAddress` (A.L.D, u16), `GroupAddress` (u16, 2/3-level/free *formatting* only), `ByteReader`/`ByteWriter` over `std::span<const std::byte>`, `Result<T>` (C++20 has no `std::expected`) | no |
| cEMI (link) | `cemi::Frame` = `variant<LData, PropRead, PropWrite, PropResponse, ResetReq, FuncPropCommand…>`; `cemi::LData{msgCode(0x11 req/0x2E con/0x29 ind), ctrl1, ctrl2, src, dst, isGroup, hopCount, priority, ackRequest, extendedFrame, tpdu}` | no |
| Transport (3/3/4) | `tl::Tpdu` (`DataGroup`, `DataBroadcast`, `DataSystemBroadcast`, `DataIndividual`, `Connect`, `Disconnect`, `DataConnected{seq}`, `Ack{seq}`, `Nak{seq}`); `tl::ConnectionStateMachine` (CLOSED/OPEN_IDLE/OPEN_WAIT, 6 s idle timeout, 3 s ack timeout, max 3 repetitions, seq mod 16) | no |
| Application (3/3/7) | `ap::Apdu` = `variant<GroupValueRead, GroupValueResponse, GroupValueWrite, IndividualAddressWrite/Read/Response, IndividualAddressSerialNumberRead/Response/Write, DeviceDescriptorRead/Response, MemoryRead/Response/Write, MemoryExtRead/Write (32-bit addr, System B), UserMemory*, PropertyValueRead/Response/Write, PropertyDescriptionRead/Response, FunctionPropertyCommand/StateRead/StateResponse, Restart (basic + master reset w/ erase code), AuthorizeRequest/Response, KeyWrite/Response, AdcRead/Response, DomainAddress* (RF, later)>`; `ap::encode/decode` with 6-bit short payload handling for `GroupValue*` | no |
| Management client | `mgmt::ManagementSession` (one T-connection to one IA; serialises requests, matches responses, chunks memory writes by `maxApdu`), `mgmt::DeviceInfoReader`, `mgmt::IaProgrammer`, `mgmt::DownloadExecutor` (sect. 3) | no |
| Group client | `grp::GroupClient` (read/write/response, monitor stream), feeds `GroupMonitorModel` (Qt, UI layer) | no |
| Link abstraction | `IBusLink { send(cemi::Frame); onReceive(cb); onState(cb); capabilities(): {hasLDataCon, maxApdu, medium} }` | no |
| KNXnet/IP codec (3/8) | `knxip::Header` (0x06 0x10), `Hpai`, `Cri/Crd`, `Dib*` (DeviceInfo, SuppSvcFamilies, IpConfig, TunnellingInfo), services: SEARCH (0x0201/02 + EXT 0x020B/0C), DESCRIPTION (0x0203/04), CONNECT (0x0205/06), CONNECTIONSTATE (0x0207/08), DISCONNECT (0x0209/0A), TUNNELLING_REQUEST/ACK (0x0420/21), DEVICE_CONFIGURATION (0x0310/11), ROUTING_INDICATION/LOST/BUSY (0x0530/31/32) | no |
| KNXnet/IP state machines | `knxip::TunnelClient` (connect w/ `TUNNEL_LINKLAYER`, NAT mode HPAI 0.0.0.0:0 by default, seq counters, 1 s ack timeout + 1 retry, heartbeat 60 s / 120 s fail, waits for `L_Data.con` before next frame), `knxip::RoutingSender` (≤ 50 frames/s pacing, ROUTING_BUSY back-off), `knxip::Discovery` | no |
| I/O adapters | `QtUdpTunnelLink`, `QtUdpRoutingLink` (multicast 224.0.23.12:3671), `QtTcpTunnelLink` (KNXnet/IP v2, needed for IP Secure), `QtDiscoveryService` (iterates `QNetworkInterface`s), `QtTimerService` | **yes** (QtNetwork) |
| USB HID (later) | `usbhid::TransferFrame` codec (KNX USB transfer protocol, 64-byte HID reports, EMI1/EMI2/cEMI selection) pure C++; I/O via `/dev/hidraw` on Linux / hidapi on Win/macOS; device whitelist from `knx_interfaces.xml` (VID/PID → manufacturer) | adapter only |

Threading: one `BusThread` (`QThread`) owns sockets + all state machines; the GUI talks to it by queued signals.
Tests drive the same state machines synchronously with a `ManualClock`, no event loop, no sleeps.

Do **not** model "routing vs tunnelling" in the management layer. Management only sees `IBusLink`.
One caveat must be surfaced to the UI: routing has no `L_Data.con`, and some routers do not forward
connection-oriented point-to-point traffic well. Commissioning should default to tunnelling.

## 2. Application program domain model (.knxprod)

Library `kestrel_catalog` (pure C++, own XML pull-parser adapter; QtCore `QXmlStreamReader` is fine if we allow QtCore in core, see sect. 7).
Immutable after load, shared by all device instances that use the same `ApplicationProgram`.

```
ApplicationProgram { id "M-0083_A-00B0-11-E4D1", appNumber, appVersion, maskVersion "MV-0705",
  programType, loadProcedureStyle {Default|Product|Merged}, peiType, dynamicTableManagement,
  Static, Dynamic, ModuleDefs }
Static:
  CodeSegments: AbsoluteSegment{address,size,data?,mask?} | RelativeSegment{lsmIdx,size,data?}
  ParameterTypes: TypeNumber{sizeInBit, Unsigned|Signed|TwosComplement, min,max, UIHint}
                | TypeRestriction{sizeInBit, base Value|BinaryValue, enum<value,text,binary>}
                | TypeFloat{encoding DPT9|IEEE754Single|IEEE754Double, min,max}
                | TypeText{sizeInBit} | TypeTime | TypeDate | TypeIPAddress | TypeColor
                | TypePicture | TypeNone | TypeRawData   // closed std::variant, unknown → hard import error
  Parameters: Parameter{id, typeRef, defaultValue, access, location}
     location = MemoryLoc{segmentRef, offset, bitOffset} | PropertyLoc{objIdx|objType, pid, offset, bitOffset}
              | UnionMember{unionId, offset, bitOffset, isDefault} | None
  Unions: Union{sizeInBit, location, members}           // several params share the same bytes
  ParameterRefs: ParameterRef{id, paramRef, overrides(text,value,access,tag)}
  ComObjects: ComObject{number, text, functionText, objectSize, flags C R W T U I, priority, dpts}
  ComObjectRefs: ComObjectRef{id, comObjectRef, overrides(any of the above)}
  AddressTable{segmentRef, offset, maxEntries}  AssociationTable{…}  ComObjectTable{segmentRef, offset}
  LoadProcedures: list<LoadProcedure{mergeId?, ldctrls}>
  Scripts / ParameterCalculations / ParameterValidations  (sect. 6, risk)
Dynamic: tree of ChannelIndependentBlock | Channel{textParamRef} | ParameterBlock{inline, layout}
  | ParameterSeparator | ParameterRefRef | ComObjectRefRef | Choose{paramRefId, When{test|default}}
  | Assign{target, source|value} | Rename | Module{defRef, args} | Button | (ETS6 extras: verify against schema)
```

Two evaluation services, both pure C++ and fully unit-testable.
- **`DynamicEvaluator`** takes the device's `ParameterValues` (map ParameterRef→value) and returns
  `ActiveSet{visibleParamRefs, activeComObjectRefs, activeUnionMembers, assigns}`.
  `when test` supports the literal list (`"1 2 3"`), relational `<`, `<=`, `>`, `>=`, `!=`, and `default`.
  It re-evaluates incrementally, because a `choose` subtree depends only on its param. This is what FR-DEV-1/2 render from.
- **`MemoryImageBuilder`**:
  1. Copy the segment base `Data` (or zero-fill).
  2. For each parameter in the *active* set (and each union's active member only), encode the value with its type.
     Numbers are big-endian, bit offset counted from the MSB of the byte, sizes 1..64 bits.
     `TypeFloat DPT9` uses the KNX 16-bit float. Text is padded with zeros.
     Then write it at `segment.base + offset`, masked.
  3. Property-located params become `PropertyWrite{objIdx, pid, start, bytes}` items.
  4. The table builders run (below).

  The output is a `DeviceImage{segments: map<id, bytes>, propertyWrites, tables}`. It is a plain value, so
  diffing two images gives "what changed", and that drives FR-COM-3 partial download and the "needs download" flag.
  **Open question to verify on real devices:** whether ETS writes values of *inactive* non-union params
  (default vs. current value). We settle it empirically with golden memory dumps (sect. 4).
- **Table builders**, one per master `Flavour`, selected via `MaskVersion/Resources`:
  - `AddressTable_Bcu1` (also used by M112): `[N+1][IA hi lo][GA1]…`, GAs **sorted ascending**, max `maxEntries`.
  - `AssociationTable_M112`/`Bcu1`: `[n][gaIdx coNum]…` (1-based index into the address table).
  - `AddressTable_SystemB`: `u16 count` plus `u16` GAs, no IA.
  - `AssociationTable_SystemB`: `u16 count` plus `(u16 TSAP, u16 ASAP)`, sorted.
  - `GroupObjectTable_M112`/`Bcu2`/`SystemB`: one descriptor per CO (config: priority + C/R/W/T/U/I flags + value type).
    The exact bit layouts are copied from 3/5/1, not guessed. Each flavour gets a golden test.
  - Coupler filter tables (`LcFilter`, 091A): a bitmap of passed GAs. This needs **project-wide** knowledge
    (which GAs have a sender/receiver beyond the line), so it is computed by a `TopologyRoutingAnalyzer`, not by the device.

Modules (`ModuleDefs`, `Arguments`, `NumberedArgument`, ID mangling like `…_MD-1_M-3_MI-2_P-17`) are used by most
2020+ products (MDT, Gira, ABB…). They must be **expanded at import time** into a flat ParameterRef/ComObjectRef space,
with an `ExpandedFrom{moduleInstance}` back-pointer for UI grouping. That way the evaluator and image builder never see modules.

## 3. Commissioning state machines

### 3a. Program individual address (FR-COM-1): `IaProgrammer`
```
Idle → CheckTargetFree: T_Connect(target) + A_DeviceDescriptor_Read
        ├ response → AskOverwrite (UI) ─ cancel → Idle
        └ timeout (3 s) → WaitProgMode
WaitProgMode: every 1 s broadcast A_IndividualAddress_Read; collect responders for 1 s window
        ├ 0 → keep waiting (user presses button; UI shows elapsed / cancel)
        ├ ≥2 → Error(MultipleInProgMode) (list the IAs)
        └ 1 → Write: broadcast A_IndividualAddress_Write(target) ×1
Write → Verify: A_IndividualAddress_Read → exactly target responds?  else Error(VerifyFailed)
Verify → ReadInfo: T_Connect(target), DeviceDescriptor_Read (mask must match catalog), PropertyValueRead
        obj0/PID 12 manufacturer, PID 11 serial → store in project (FR-COM-4)
     → Restart (A_Restart basic, leaves prog mode) → Done
```
Variant `SerialNumberIaProgrammer`: `A_IndividualAddressSerialNumber_Write`, with no button press. It is needed for
KNX-IP / Secure devices and for re-addressing devices whose serial is in the project.

### 3b. Download (FR-COM-2/3/5): plan first, then execute
1. **`ProcedureResolver`** (offline, pure):
   (mask, `ProcedureType` Load|Unload, subtype `all|par|grp|ap1|cfg`, `loadProcedureStyle`) → flat `vector<LdCtrl>`.
   - `Product` → the app's `LoadProcedures`.
   - `Default` → master's procedure.
   - `Merged` → master's procedure, with each `LdCtrlMerge MergeId=n` replaced by the app's `LoadProcedure MergeId=n`.
   Then `LdCtrlMapError` sets error-mapping scopes. Partial subtypes that the master does not define fall back
   to a documented derivation (e.g. `par` = only steps touching the app LSM).
2. **`DownloadPlanner`** (offline, pure): `LdCtrl` list + `DeviceImage` + mask `Resources` → `DownloadPlan` =
   `vector<Step>` of concrete management primitives with byte payloads resolved. `LdCtrlWriteMem` without
   `InlineData` pulls bytes from the image. `LdCtrlWriteRelMem ObjIdx=n` gets base = runtime read of
   PID 7 (table reference). The plan is **printable and diffable**. A CLI `kestrel-cli plan dev.json` is our best debugging tool.
3. **`DownloadExecutor`** (sans-IO state machine over one `ManagementSession`), one handler per op:

| LdCtrl | Primitive(s) |
|---|---|
| `Connect` / `Disconnect` | T_Connect, A_DeviceDescriptor_Read (mask check), A_Authorize_Request(key `FFFFFFFF`) if `AuthorizeLevels` > 0; T_Disconnect |
| `Unload/Load/LoadCompleted LsmIdx|ObjType` | M112: A_Memory_Write to the `LoadControl_M112` resource (0x0104), then poll status byte (0xB6EA+n) · Bcu2/SystemB: A_PropertyValue_Write obj n PID 5 (LOAD_STATE_CONTROL) event, then poll PID 5 until Loaded/Unloaded/Error (timeout configurable) |
| `RelSegment` / `AbsSegment` / `TaskSegment` / `TaskPtr` / `TaskCtrl` | PID 5 "additional load control" records (alloc segment, size, mode, fill) |
| `WriteMem` / `WriteRelMem` / `CompareMem` / `LoadImageMem` | A_Memory_Write/Read (or MemoryExt for >16-bit addresses), chunk = maxApdu−3 (12 B standard frame; read PID 56 for extended), verify by read-back if `Verify` and `VerifyMode` |
| `WriteProp` / `CompareProp` / `LoadImageProp` | A_PropertyValue_Write/Read with element count + start index |
| `SetControlVariable`, `MapError`, `Delay` | executor-local state |
| `ClearLCFilterTable` | memory clear or A_FunctionPropertyCommand (`UseFunctionProp`) |
| `Restart` / `MasterReset` | A_Restart (basic, or master reset with erase code + channel) and expect the connection to drop |

Executor states: `Idle → Connecting → Running(step i, retry k) → Restarting → Done | Failed(step, cause) | Cancelled`.
Each step emits a progress event (step i/N plus bytes). Failure always ends with a best-effort `T_Disconnect`.
If a failure happens after `Unload`, the device is flagged "unloaded / needs full download".

## 4. Simulator (NFR-5)

`kestrel_sim` is a pure C++ library with a Qt-adapter executable. It is a first-class product, not a test helper.
- **`sim::VirtualBus`**: deterministic in-process medium. It delivers each frame to all attached endpoints, ordered by a
  `ManualClock` event queue.
  Fault injection: drop / delay / duplicate / corrupt / force NAK / BUSY, per rule (`match(dst, apci) → action`).
- **`sim::VirtualDevice`**: the server side of the same `tl::` and `ap::` codecs, plus:
  - `MemoryImage`: sparse, 32-bit addressable, with ROM/protected ranges and access levels.
  - `InterfaceObjectServer`: objects, properties, PDT, max elements, read/write levels.
  - `LoadStateMachine` per LSM: Unloaded/Loading/Loaded/Error. It reacts to the PID 5 and M112 load-control events,
    and allocates relative segments, so a PID 7 read returns the allocated base.
  - Prog-mode flag, IA, serial, `DeviceDescriptor` (mask), authorize keys/levels, restart semantics.
  - Group objects bound to a decoded table: after a download the simulated device actually reacts to the GAs we
    programmed. This closes the loop: download, then a group write, then the CO value changes.

  A `DeviceProfile` is built from the master `MaskVersion/Resources` (user-local) **or** from a synthetic JSON profile (CI).
- **`sim::KnxIpServer`** (Qt adapter): a real KNXnet/IP server on loopback (configurable port). It supports
  SEARCH/DESCRIPTION/CONNECT/TUNNELLING/CONNECTIONSTATE (+ routing on a non-default multicast group to avoid LAN
  pollution), bridging into `VirtualBus`. It also supports multiple tunnel slots and slot exhaustion (`E_NO_MORE_CONNECTIONS`).
- **`kestrel-simd`** CLI: `kestrel-simd --port 3671 --devices sim.json --control 127.0.0.1:3700`. The control channel is
  JSON-lines (press prog button, dump memory, inject faults, read CO values). A small Python Robot library wraps it.
  The Robot flow: start simd → run `kestrel-cli` / GUI → assert on the simulated device state.
- Test levels:
  - Unit: codecs (byte-exact vectors from the spec examples), state machines driven through `ManualClock`.
  - Integration (Qt Test): real UDP loopback against `sim::KnxIpServer`.
  - Acceptance: Robot + simd.
- **Fixtures are synthetic.** CI cannot ship `knx_master.xml` or vendor `.knxprod` files. We hand-write a minimal
  `test-master.xml` (a few DPTs and the 0705/07B0/091A mask resources, written from the spec) and a fake manufacturer
  `M-FFFF` `.knxprod` exercising every ParameterType, union, choose/when, module and LoadProcedure style.
  Real vendor files are used only in an opt-in local "corpus" test (`KESTREL_CORPUS_DIR`) that checks import + plan without crashing.
- **Golden dumps**: `kestrel-cli dump-memory <ia> <ranges>` against a real device that ETS has programmed. Checked-in
  dumps of *our synthetic* app are fine. Vendor dumps stay local, and they are how we validate `MemoryImageBuilder` byte for byte.

## 5. DPT encoding

Hybrid, with a firm split:
- **Hard-coded codecs (pure C++, `kestrel_dpt`)** for the main types that need real arithmetic or packed fields:
  1, 2, 3, 4, 5 (5.001 scaling 0–100 %, 5.003 angle), 6, 7, 8, 9 (KNX float16: sign, 4-bit exponent, 11-bit mantissa, 0.01 resolution),
  10 (time with weekday), 11 (date with century rule), 12, 13, 14 (IEEE754), 16 (ASCII / 8859-1, 14 B), 17, 18, 19 (date-time
  with flags), 20, 232 (RGB), 242 (xyY), 251 (RGBW), 29 (int64).
  These exist so the group monitor works with **no** master file present (and in CI).
- **Table-driven metadata from master at runtime**: names, `Text`, units, ranges, enum texts (`Enumeration/EnumValue`), and
  the field layout (`Format`: `Bit`, `UnsignedInteger`, `SignedInteger`, `Float Width=16|32`, `Enumeration`, `Reserved`,
  `String`, `RefType`). A generic `FormatInterpreter` decodes any DPT that has no hand codec (the long tail of 3xx types)
  as a struct of named fields. `Float Width=16` maps to the KNX-float codec, never IEEE half.
- API: `DptId{main, sub}`, `DptValue = variant<bool, int64_t, uint64_t, double, string, vector<DptField>>`,
  `DptRegistry::codec(DptId) → const IDptCodec&` with `encode(DptValue) → bytes`, `decode(bytes) → DptValue`,
  `format(DptValue, locale)`, `sizeInBit()`.
  ≤ 6-bit types are flagged so `ap::GroupValueWrite` uses the short form.
- The same registry is reused for `TypeFloat DPT9` parameters and for FR-GA-4 compatibility checks (`sizeInBit` equality first,
  main-number equality as a warning).

## 6. Risks / hard parts (honest)

1. **System B / 07B0 / 57B0 downloads.** Load-state-machine "additional load control" records, relative segment allocation,
   stamps (`DownloadStamp`, `Stamp_SystemB` at PID 27), `MapError` semantics, and vendor-specific merge procedures.
   The spec helps, but expect device quirks. **Mitigation:** plan/execute split, verbose step logs, real-device beta testers.
2. **Calculated parameters & scripts** (`ParameterCalculation`, `Script`, `Button` handlers, i.e. ETS's `CalculatedParameters`
   module). These are JavaScript in the product data. We need a JS engine: `QJSEngine` (QtQml, not QtQuick) behind an
   `IScriptHost` interface in a separate lib, sandboxed (no I/O). Some products will not configure correctly until this exists.
   We detect them at import and warn.
3. **Modules + ID mangling + Arguments** in modern products are large and error-prone. They need the corpus test.
4. **Inactive-parameter write semantics, union defaults, `Assign`.** Small rules that cause byte differences. Golden dumps are the only proof.
5. **KNX Secure.**
   - Data Secure: AES-128-CCM, sequence counters, tool key / FDSK from the device certificate, S-A_Sync.
   - IP Secure: X25519 handshake, session auth, mandatory TCP tunnelling.

   OpenSSL 3 headers are present (`/usr/include/openssl`) and cover both. mbedtls is also there. Even so, Secure is
   a multi-month milestone, and the stored keys (`.knxkeys`, project keyring) need careful handling.
6. **Couplers / line filter tables (091A, 2920)**: these need global GA-routing analysis plus `LcSlave`/`LcFilter` address spaces.
7. **Real-interface flakiness**: tunnelling servers with one slot, dropped `L_Data.con`, `E_SEQUENCE_NUMBER`, devices
   that answer `T_Connect` slowly. Timeouts must be configurable per interface (cf. `Timing="1000 100"` in `knx_interfaces.xml`).
8. **Archives**: `.knxprod`/`.knxproj` are ZIPs, and protected `.knxproj` files use WinZip-AES. There is no libzip/minizip/quazip here.
   We have only zlib + OpenSSL, so we need a small in-house ZIP reader (central directory + inflate + WinZip-AES via PBKDF2/AES-CTR/HMAC-SHA1). Doable, but it is code we own.

### Milestone order (protocol view)
- **P0** Codecs: addresses, cEMI, TPDU/APDU, KNXnet/IP, DPT core. All byte-exact unit tests. `VirtualBus` + `VirtualDevice` skeleton.
- **P1** `TunnelClient`, routing, discovery, `kestrel-simd` KNXnet/IP server, group monitor + read/write (FR-BUS-1/2/3/5/6).
- **P2** `ManagementSession`, device info, IA programming, restart (FR-COM-1/4/5).
- **P3** Catalog Static model, `MemoryImageBuilder`, M112 table builders, `ProcedureResolver`/`Planner`/`Executor` for **0705 Product-style**.
  Green against the sim first, then golden dumps on one real device.
- **P4** `DynamicEvaluator`, modules, partial download + "needs download" diffing.
- **P5** System B (07B0/57B0, Merged style, property-based LSMs, relative segments), then couplers 091A.
- **P6** USB HID. **P7** JS calculated parameters (could move earlier if the corpus shows it is widespread). **P8** KNX Secure.

## 7. Notes for the other architects
- Please allow **QtCore (only)** in the core libs (`QXmlStreamReader`, `QString` for i18n texts), or else agree on a pure-C++ XML
  parser. The protocol libs (`kestrel_knx`, `kestrel_dpt`, `kestrel_sim` core) stay **zero-Qt** either way.
- Device instance state in the project = `{appProgramRef, ParameterValues (only non-default), CO↔GA links, IA, serial,
  lastDownloadedImageHash}`. The memory image is *derived*, never stored as the source of truth.

## My 3 non-negotiable decisions
1. **Sans-IO, zero-Qt protocol core.** Codecs and every state machine (transport, tunnel, management, download) are pure C++20
   driven by events and an injected clock. Qt exists only in socket/timer adapters. This is what makes NFR-3/NFR-5 real.
2. **Downloads are data-driven.** `ProcedureResolver → DownloadPlan → DownloadExecutor` interprets master + application
   `LdCtrl*` procedures and mask `Resources`. There is no per-device or per-mask hard-coded download code beyond the primitive mapping
   per `ManagementModel`, and the plan is always inspectable and diffable.
3. **The simulator is in-repo, deterministic and first-class, with synthetic fixtures.** Every bus feature lands with a sim-backed
   test. CI uses only hand-written spec-derived XML (`test-master.xml`, fake `M-FFFF` product), never proprietary files.
