# 04 — KNX protocol stack, bus access & simulator

Status: **v1.0** (2026-10-28). Libraries: `kestrel::knx`, `kestrel::dpt`, `kestrel::sim` (tier T0, std-only),
`kestrel::bus`, `kestrel::simnet` (tier T1, QtNetwork). Origin: `brainstorm/round1-protocol.md` and the round-2 amendments.
Sources: the public KNX Standard v2.1 (3/3/x layers, 3/5/x management, 3/7/2 DPTs, 3/8/x KNXnet/IP). Also cross-read: the
open-source projects knxd, Calimero and xknx, for understanding only (no code copied).

## 1. Layering (all T0, sans-IO)

| Layer | Header | Content |
|-------|--------|---------|
| Addresses | `knx/Address.hpp` | `IndividualAddress{raw}` (`A.L.D`, 4/4/8 bits) and `GroupAddress{raw}` (3-level `M/Mi/S` 5/3/8, 2-level `M/S` 5/11, free `0..65535`). Both have `parse`, `toString`, `operator<=>`, `operator<<` |
| cEMI | `knx/Cemi.hpp` | `cemi::LData{ messageCode (LDataReq 0x11, LDataCon 0x2E, LDataInd 0x29), frameType standard/extended, repeat, broadcast, priority, ackRequest, confirmError, groupDestination, hopCount, source, destination (u16), tpdu (Bytes) }`; `encode`/`decode` (additional info skipped on decode, none on encode) |
| Transport + application | `knx/Apdu.hpp` | `Tpdu{ type: DataGroup/DataBroadcast/DataIndividual/DataConnected/Connect/Disconnect/Ack/Nak, seq, apdu }`, `Apdu{ apci (enum), data }` with short-data handling (≤ 6 bits in the APCI byte). Builders: `groupValueRead/Response/Write`, `individualAddressRead/Response/Write`, `deviceDescriptorRead/Response`, `memoryRead/Response/Write`, `propertyValueRead/Response/Write`, `restart` |
| KNXnet/IP | `knx/KnxIp.hpp` | Header 06 10 + service type + total length; `Hpai`; `SearchRequest/Response`, `DescriptionRequest/Response` (DIB device info + supported service families), `ConnectRequest/Response` (CRI/CRD tunnel link layer), `ConnectionStateRequest/Response`, `DisconnectRequest/Response`, `TunnellingRequest/Ack`, `RoutingIndication`; `knxip::decode(bytes) → Result<Frame>` (variant), `encode(frame)` |
| Tunnel client | `knx/TunnelClient.hpp` | Sans-IO state machine (§2) |
| Link interface | `knx/BusLink.hpp` | `IBusLink`: `send(cemi::LData)`, callbacks `onFrame`, `onState` — implemented by `bus::UdpTunnelLink` and `sim::SimLink` |
| Management | `knx/IaProgrammer.hpp`, `knx/ManagementSession.hpp` | Sans-IO procedures (§3) |

Time: `using Clock = std::chrono::steady_clock; using TimePoint = Clock::time_point;`. State machines never read the clock themselves.

## 2. Tunnel client state machine
```
Disconnected --connect(now)--> Connecting   [send CONNECT_REQUEST on control endpoint, timeout 10 s]
Connecting --CONNECT_RESPONSE ok--> Connected (channel id, assigned IA)   | error/timeout --> Disconnected(reason)
Connected:
  sendFrame(L_Data.req): TUNNELLING_REQUEST(seq=outSeq) ; wait TUNNELLING_ACK 1 s, 1 retry, then connection lost
                         one outstanding frame; queue the rest; also wait L_Data.con (3 s) before the next frame
  TUNNELLING_REQUEST in: if seq == inSeq → ACK + deliver + inSeq++ ; if seq == inSeq-1 → ACK only (duplicate); else drop
  heartbeat: CONNECTIONSTATE_REQUEST every 60 s; 1 request + 3 repeats (10 s timeout each) → DISCONNECT_REQUEST → Disconnected(HeartbeatLost)
  disconnect(now) → DISCONNECT_REQUEST → Disconnected (a CONNECT_RESPONSE that arrives after a cancel is answered with DISCONNECT_REQUEST so the server slot is not leaked) ; DISCONNECT_REQUEST from server → DISCONNECT_RESPONSE → Disconnected
```
API: `connect(now)`, `disconnect(now)`, `send(LData, now)`, `onDatagram(bytes, now)`, `tick(now)`,
`takeActions() → vector<Action>`. `Action = variant<SendControl{bytes}, SendData{bytes}, Connected{channel, ia},
Disconnected{reason}, FrameReceived{LData}, SendConfirmed{ok}>`. NAT mode is used (HPAI 0.0.0.0:0), so there is one UDP
socket for both endpoints.

## 3. Commissioning (M1: programming the individual address)
`IaProgrammer` (sans-IO, uses `IBusLink`-level frames via actions):
```
Start(target) → WaitProgMode: every 1 s broadcast A_IndividualAddress_Read; collect responses within the 1 s window
   0 responders → keep waiting until the user cancels or the timeout expires (default 60 s)
   ≥2 responders → Failed(MultipleDevicesInProgMode)
   1 responder  → CheckAddress (skipped if the responder already has `target`)
CheckAddress: T_Connect(target) + A_DeviceDescriptor_Read (KNX 3/5/2), timeout 3 s
   any answer from target → Failed(AddressInUse), unless allowOverwrite
   timeout → Write: broadcast A_IndividualAddress_Write(target)
Write → Verify: A_IndividualAddress_Read (up to verifyAttempts windows; stray late answers tolerated) → `target` responds → Restart: connect-less A_Restart to target → Done
```
`ManagementSession` handles connection-oriented point-to-point traffic: T_Connect, DataConnected with seq/ack, 3 s ack timeout,
3 repetitions, and a 6 s idle disconnect. It provides `deviceDescriptorRead`, `memoryRead/Write` (chunked to 12 bytes),
`propertyValueRead` and `restart`. In M1 it is used for "device info". In M3 it becomes the download executor's transport.

## 4. DPT (`kestrel::dpt`)
`DptId{main, sub}` parses `"9.001"`, `"DPT-9"`, `"DPST-9-1"`. `DptValue = variant<bool, int64_t, uint64_t, double, std::string>`.
`encode(DptId, DptValue) → Result<Bytes>`, `decode(DptId, ByteView) → Result<DptValue>`,
`format(DptId, DptValue) → std::string` (with unit), `parse(DptId, std::string_view) → Result<DptValue>`, `sizeInBit(DptId)`.
Hand-written codecs: 1, 2, 3, 5 (5.001 % scaling, 5.003 angle), 6, 7, 8, 9 (KNX float16), 10, 11, 12, 13, 14 (IEEE754), 16, 17,
18, 20 (raw u8), 232 (RGB). Values of 6 bits or fewer travel in the APCI byte (short form).
Master data (names, enum texts) is added in M2 from the user's `knx_master.xml`.

## 5. Simulator (`kestrel::sim`, T0)
- `VirtualBus`: deterministic medium. Attached endpoints receive every frame (except the sender). It keeps a telegram log and
  supports fault rules (drop / duplicate by predicate).
- `VirtualDevice`: IA (default 15.15.255), serial, mask version (default 0x0705), prog-mode flag, sparse memory,
  properties (object 0: PID 11 serial, PID 12 manufacturer), and group objects `{ga, value Bytes, flags}`. It answers:
  IA read (only in prog mode), IA write (only in prog mode), device descriptor read, memory read/write, property read, restart
  (clears prog mode), group read (if flag R) and group write (if flag W, updates the value).
  Transport-layer connections are handled for point-to-point services.
- `TunnelServer`: sans-IO KNXnet/IP server for one or more tunnel slots. It handles SEARCH, DESCRIPTION, CONNECT,
  CONNECTIONSTATE, DISCONNECT and TUNNELLING_REQUEST (ACK + L_Data.con + forward to the bus), and forwards bus frames to clients as
  L_Data.ind. The server's own tunnel IA defaults to 1.1.255.
- `simnet::KnxIpServer` (T1): a `QUdpSocket` on 127.0.0.1 with port 0 (random) or a chosen port. It drives `TunnelServer`.
- `kestrel-sim` executable: `kestrel-sim --port 0 --scenario file.json`. On startup it prints `{"event":"ready","port":N}`.
  It reads JSON-lines commands on stdin (`press-prog-button`, `device`, `memory`, `group-value`, `telegrams`, `quit`), answers
  each with one JSON line, and emits every bus telegram as a JSON event line.

Scenario file:
```json
{ "interfaceAddress": "1.1.255",
  "devices": [ { "name": "switch-a", "address": "15.15.255", "serial": "0083:00001234", "progMode": false,
                 "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" } ] } ] }
```

## 6. Bus I/O (`kestrel::bus`, T1)
`UdpTunnelLink : QObject, IBusLink` owns a `QUdpSocket` and a `QTimer` (100 ms tick) and wraps `knx::TunnelClient`.
`Discovery` sends SEARCH_REQUEST to 224.0.23.12:3671 on each multicast-capable interface (and unicast for a given host) and
collects responses for 3 s. Bus objects live on a bus thread owned by `app::BusSession`.

## 7. Later milestones
Full download (data-driven `LdCtrl*` interpretation, ADR-008), System B, couplers and filter tables, routing pacing,
USB HID, KNX Data/IP Secure (OpenSSL), and a JavaScript host for calculated parameters.
