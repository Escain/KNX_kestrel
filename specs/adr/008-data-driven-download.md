# ADR-008 — Data-driven device download

Status: Accepted (implementation M3)

**Decision.** The download pipeline is:
1. `ProcedureResolver` interprets the load procedures from master data and the application program (`LdCtrl*`).
2. `DownloadPlanner` produces a printable, diffable `DownloadPlan` of concrete management steps, filled from a derived `DeviceImage`.
3. `DownloadExecutor` runs that plan through a sans-IO `ManagementSession`.

There is no per-device hard-coded download code. The memory image is derived from the parameters and never stored.
