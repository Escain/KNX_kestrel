# ADR-001 — Three dependency tiers

Status: Accepted (2026-10-28)

**Context.** NFR-3 requires a headless core. The protocol architect wanted a Qt-free core, while the model architect wanted
QtCore (QString, XML, JSON, QSaveFile) so that we do not rewrite UTF-16 and XML handling.

**Decision.**
- T0 (std only): `base`, `knx`, `dpt`, `sim`, and `archive` (std plus system zlib, see ADR-004).
- T1 (QtCore, plus QtNetwork/Xml/Sql where needed): `model`, `persist`, `validate`, `bus`, `simnet`, `app`, `catalog`, and later `ets`.
- T2 (GUI): `viewmodels`, the QML modules, `shell`.

`cmake/KestrelLayering.cmake` fails the configure step on any violation.

**Consequences.** Protocol code compiles anywhere, and the same code can later run in firmware or a fuzzer. T0 logs through an injected callback.
Data flows from T1 down to T0 as plain structs.
