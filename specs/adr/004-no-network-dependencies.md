# ADR-004 — No fetched third-party dependencies; own Result and ZIP

Status: Accepted

**Context.** The build machine has no zip library and no network for FetchContent. It has zlib, libdeflate, zstd, OpenSSL 3 and sqlite3.

**Decision.**
- Our own `kestrel::Result<T>` follows the `std::expected` subset, so a later C++23 switch is mechanical.
- Our own ZIP reader and writer (`archive`, M2) over zlib, with WinZip-AES over OpenSSL. The writer must be byte-deterministic.
  It gets fuzzing and a cross-check against Python `zipfile`.
- Vendored code is limited to `third_party/escain_unittests`.
- `archive` is a std-only (T0, `STD_ONLY`) library that links nothing but the system zlib (`find_package(ZLIB)`,
  `ZLIB::ZLIB`); OpenSSL joins it for WinZip-AES in M7. The reader landed in M2 (2026-10-28); the writer follows.

**Considered later (2026-10-28, after M1).** Offline copies of `tl::expected` (CC0, header-only) turned up outside
`/usr/include` (Qt Creator and KiCAD source trees). Not adopted: the own `Result<T>` is already implemented and
tested in every library, and `tl::expected` would add no capability. Both are `std::expected`-shaped, so the C++23
switch costs the same either way. Revisit only if `Result` needs monadic operations (`and_then`, `transform`).
