# 03 — Domain model & persistence

Status: **v1.0** (2026-10-28). Libraries: `kestrel::model`, `kestrel::persist`, `kestrel::validate` (tier T1: QtCore allowed,
never QtGui). Origin: `brainstorm/round1-model.md` and the round-2 amendments.

## 1. Principles
1. **Value types, not QObjects.** `Project` and its entities are copyable values. Large tables use copy-on-write.
   Snapshots for workers are plain copies.
2. **References by id, never by pointer.** Reverse indexes are derived and never persisted.
3. **Mutation only through transactions** (`Project::edit`). Every commit, undo and redo emits one ordered `ChangeSet`.
4. **Two identity systems.** Catalog and master data keep KNX string ids (`M-0083_A-…`, `DPST-9-1`). Project
   entities get Kestrel ids: random 64-bit values written as `<prefix>-<13 base32 chars>`, e.g. `dev-7f3k2m9qv1abc`.
   The generator is injectable so tests are deterministic.
5. **Derived, never stored:** "needs download" (hash comparison), parameter and com-object visibility, and individual addresses
   (area.line.device numbers are stored, and the IA is computed from them).

## 2. Entities (milestone M1 scope)

```
Id<Tag>             strong 64-bit id; prefixes: bp (BuildingPart), area, line, dev, co (ComObject), gr (GroupRange),
                    ga (GroupAddress), lnk (Link)
ProjectSettings     { name, gaStyle: ThreeLevel|TwoLevel|Free, contentLanguage ("en-US") }
BuildingPart        { id, parent?: Id<BuildingPart>, kind: Building|Floor|Room|Corridor|Stairway|DistributionBoard,
                      name, description }
Area                { id, number 0..15, name, medium: TP|IP|RF|PL }
Line                { id, area, number 0..15, name, medium }
Device              { id, line, number?: 0..255 (nullopt = not yet addressed), name, description,
                      place?: Id<BuildingPart>, productRef (catalog key string, may be empty),
                      appProgramRef (catalog key string, may be empty),
                      params: map<QString paramRefId, QString canonicalValue>  (non-default values only) }
ComObject           { id, device, number, name, function, sizeInBit, flags {C,R,W,T,U,I}, dpt (e.g. "DPST-1-1"),
                      catalogRef (empty for manually defined objects) }
GroupRange          { id, parent?: Id<GroupRange>, name, start u16, end u16 }   (main group / middle group folders)
GroupAddress        { id, range?: Id<GroupRange>, raw u16, name, dpt (QString, may be empty), description }
Link                { id, comObject, groupAddress, sending: bool }   (at most one sending link per com object)
DeviceState         { iaProgrammed: bool, serial: QString, downloadedHash: QString }  (commissioning facts, not undoable)
```

Notes:
- In M1 a device's com objects can be defined manually (no catalog yet). Later they are instantiated from the
  application program, with `catalogRef` set.
- Sibling order is the table insertion order. Tables are ordered (vector + id→index map).
- Address helpers: `Project::individualAddress(devId) → std::optional<knx::IndividualAddress>`,
  `GroupAddress::raw` uses the same encoding as `knx::GroupAddress`.

## 3. API

```cpp
namespace kestrel::model {

class Project {
public:
	explicit Project(IdGenerator gen = IdGenerator::random());
	const ProjectSettings& settings() const;
	// Read access: const tables + lookups
	const Table<BuildingPart>& buildingParts() const;   // also areas(), lines(), devices(), comObjects(),
	                                                     // groupRanges(), groupAddresses(), links()
	const DeviceState& deviceState(Id<Device>) const;
	// Derived queries
	std::vector<Id<Line>> linesOf(Id<Area>) const;  std::vector<Id<Device>> devicesOf(Id<Line>) const;
	std::vector<Id<Link>> linksOfGroupAddress(Id<GroupAddress>) const;  std::vector<Id<Link>> linksOfComObject(Id<ComObject>) const;
	std::optional<knx::IndividualAddress> individualAddress(Id<Device>) const;

	// Mutation
	Status edit(QString label, const std::function<Status(Tx&)>& body, std::optional<MergeKey> merge = {});
	void recordState(Id<Device>, DeviceState state);        // bypasses undo, emits ChangeSet, marks dirty
	History& history();                                     // undo(), redo(), canUndo(), undoText(), setClean(), isClean()
	Subscription onChanged(std::function<void(const ChangeSet&)>);
};

class Tx {   // records reversible primitive ops; aborting (non-ok Status or exception) rolls back everything
public:
	template <class T> Id<T> insert(T entity);         // assigns id if null
	template <class T> void erase(Id<T>);              // no implicit cascade: operations cascade explicitly
	template <class T, class F> void set(Id<T>, F T::*field, F value);
	void setSettings(ProjectSettings);
	void setParam(Id<Device>, QString ref, std::optional<QString> value);
};

struct Change { enum Kind { Added, Removed, Modified } kind; EntityRef ref; bool structural; };
// structural = parent/owner changed (moves). Removals of a cascade are ordered children-first.
struct ChangeSet { std::vector<Change> changes; bool fromUndo = false; bool stateOnly = false; };
}
```

High-level **operations** (`model/Operations.hpp`) are free functions that open exactly one transaction and validate
their input. Each returns `Result<Id<…>>` or `Status`:
`addBuildingPart`, `addArea`, `addLine`, `addDevice`, `setDeviceNumber`, `removeDevice` (cascades com objects and links),
`addComObject`, `addGroupRange`, `addGroupAddress` (range check, duplicate check), `removeGroupAddress` (cascades links),
`link(co, ga)` (the first link becomes the sending one), `unlink`, `rename(EntityRef, name)`, `placeDevice(dev, bp?)`, `moveDevice(dev, line)`.

Merging: consecutive edits with the same `MergeKey` within 1 s fold into one undo step (spin boxes, typing).

## 4. Native file format (`persist`)

Folder form for M1. A ZIP container (`.kestrel`) comes with `archive` in M2 and uses the same logical tree:

```
kestrel.json           { "format": "kestrel-project", "formatVersion": 1, "generator": "Kestrel 0.1.0" }
project.json           settings
buildings.json         building parts (ordered)
topology.json          areas, lines (ordered); devices listed per line by id
devices/<dev-id>.json  device + its com objects + links (sending first) + params + state
groupaddresses.json    ranges + group addresses
```

- **Deterministic:** keys sorted (QJsonObject does this), stable array order, 2-space indent, final newline.
  Writing the same project twice must produce byte-identical files. Writes are atomic per file (`QSaveFile`), and
  stale `devices/*.json` files are removed.
- **Versioned:** loading runs a chain of JSON→JSON migrations (`v1→v2…`). A newer `formatVersion` is refused with a clear error.
- **Loading never trusts input:** dangling references produce a `Result` error that names the file and the id.
- API: `persist::save(const Project&, const QString& dir) → Status`, `persist::load(const QString& dir) → Result<Project>`.

## 5. Validation (`validate`)

Rules are pure functions `void rule(const Project&, DiagnosticSink&)`. Each produces
`Diagnostic { code, severity (Error|Warning|Info), message (English, also used as a translation key), subjects: [EntityRef] }`.

| Code | Severity | Rule |
|------|----------|------|
| TOP-001 | Error | Duplicate individual address |
| TOP-002 | Error | Device number 0 used by a non-coupler device (M1: any device with number 0) |
| TOP-003 | Info | Device has no individual address yet |
| TOP-004 | Error | Duplicate area number, or duplicate line number within an area |
| GA-001 | Error | Duplicate group address raw value |
| GA-002 | Warning | Group address without DPT |
| GA-003 | Error | Linked com object size differs from the GA's DPT size |
| GA-004 | Warning | Group address without links |
| GA-005 | Warning | Group address with links but no sender (no linked com object with the **T** flag) |
| GA-006 | Error | Group address 0/0/0 (reserved) |
| GA-007 | Error | Group address outside its group range, group range outside its parent range, or overlapping sibling ranges |
| CO-001 | Warning | Com object with more than one sending link (should be impossible via operations; guards imported data) |
| CO-002 | Error | Com object linked twice to the same group address |
| CO-003 | Error | Duplicate com object number on one device |
| BLD-001 | Info | Device not placed in a building part |

TOP-004, GA-006, GA-007, CO-001..003 guard states that operations refuse but a hand-edited or imported file may
contain: loading stays lenient (only syntax, value ranges, unique ids and dangling references are load errors) so the
project opens and these problems are shown.

`validate::run(const Project&) → std::vector<Diagnostic>` returns diagnostics sorted by severity, then code, then subject.

## 6. Later (M2+)
The product-database model read from `.knxprod` files is described in `07-catalog.md` (M2, first slice).

`archive` (own ZIP reader/writer + WinZip-AES), `catalog` (user-level SQLite index + verbatim compressed application XML,
lazy parsed, LRU-cached immutable `ApplicationProgram`), `ets` (knxproj import: stream to IR → map → diagnostics,
schema 11..23 quirk table), and the import report for elements we drop.
