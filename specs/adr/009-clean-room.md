# ADR-009 — Clean-room policy

Status: Accepted

- No decompiling or disassembling of ETS. The ETS6 installer is used only for UX inspiration and to read the structure of public data files.
- `knx_master.xml`, `.knxprod`, `.knxproj` and `.exe` files are never committed (`.gitignore`, plus a pre-commit hook in M2).
  At runtime the master data is loaded from the user's own archives.
- Open-source KNX stacks are read for understanding only. Their code is not copied when the license is incompatible.
- Test fixtures are hand-written from the public spec (fake manufacturer `M-FFFF`).
