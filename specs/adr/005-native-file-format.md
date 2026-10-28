# ADR-005 — Native project format

Status: Accepted

**Decision.**
- Deterministic, sorted JSON with one file per device, in folder form (M1) or as a ZIP `.kestrel` (M2).
- An integer `formatVersion` with tested JSON→JSON migrations.
- Application programs in use are embedded verbatim (M2).
- Random 64-bit ids, which avoid merge conflicts for git users. Readable diffs come later via a `kestrel-cli textconv` git driver.
