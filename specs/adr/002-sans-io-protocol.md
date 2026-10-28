# ADR-002 — Sans-IO protocol state machines

Status: Accepted

**Decision.** Codecs are pure functions. State machines (tunnel client and server, transport connection, IA programmer, management
session, and later the download executor) take events plus `now` and return actions. Sockets and timers live only in Qt adapters (`bus`, `simnet`).

**Consequences.** Every timeout, retry and duplicate path can be unit-tested deterministically, without an event loop. The adapters are thin.
