# 07 — Product files and the catalog model

Status: **v0.1** (2026-10-28), M2 first slice. Libraries: `kestrel::archive` (T0, std + zlib) and
`kestrel::catalog` (T1, QtCore). Scope: what "link a group address to a com object, then download the tables" needs.

## 1. `.knxprod` layout
A manufacturer product file is a ZIP of KNX project-schema XML (root `<KNX xmlns="http://knx.org/xml/project/NN">`;
any `http://knx.org/xml/project/*` namespace is accepted, NN seen from 11 to 23):

```
M-XXXX/Hardware.xml            Hardware → Products, Hardware2Programs → ApplicationProgramRef   (required)
M-XXXX/Catalog.xml             CatalogSection (nested) → CatalogItem(ProductRefId, Hardware2ProgramRefId)
M-XXXX/M-XXXX_A-....xml        ApplicationPrograms (one or more files)
M-XXXX/Baggages.xml, M-XXXX/Baggages/...    ignored
knx_master.xml                 master data: presence noted, not parsed yet
```
Each file may carry `Languages/Language@Identifier/TranslationUnit@RefId/TranslationElement@RefId/
Translation@AttributeName@Text`.

## 2. `archive`: own ZIP reader (ADR-004)
`ZipReader::open(Bytes, ZipLimits)` parses the end of central directory (comment included; ZIP64 locator/record),
the central directory and every local header up front; `read(name)` inflates one entry (method 0 or raw deflate 8)
and checks its CRC-32. Names are UTF-8 (flag bit 11, Info-ZIP 0x7075, else CP437), `\` becomes `/`.

| Input | Outcome |
|-------|---------|
| truncated / corrupt structures, bad signatures, out-of-bounds offsets | `open` → `ParseError` |
| `..` component, absolute path, drive letter, NUL, duplicate name | `open` → `ParseError` ("unsafe entry name") |
| declared uncompressed total > `maxTotalSize` (1 GiB), > `maxEntries` | `open` → `Unsupported` |
| multi-disk archive | `open` → `Unsupported` |
| encrypted entry (flag bit 0 or 6, WinZip-AES method 99) | `read` → `Unsupported` "encrypted entry" (AES: M7) |
| other compression method | `read` → `Unsupported` |
| entry > `maxEntrySize` (256 MiB) | `read` → `Unsupported`, before inflating |
| inflated size ≠ declared, CRC mismatch, corrupt deflate | `read` → `ParseError` |

Output buffers are sized from the declared size and inflate never writes past it, so a lying header cannot cause
an allocation larger than the limits.

## 3. `catalog`: model
Plain copyable values with `operator==`/`operator<<` (`Model.hpp`). KNX string ids stay ids; every element keeps
its raw `AttributeMap` so later milestones can read more without re-parsing.

```
ProductDatabase   { manufacturerId, schemaNamespace, hardware[], catalogItems[], applicationPrograms[],
                    translations (hardware/catalog texts), files[], hasMasterData }
Hardware          { id, name, serialNumber, versionNumber, products[Product{id,text,orderNumber}],
                    hardware2Programs[{id, applicationProgramRefs[]}] }
CatalogItem       { id, name, number, productRefId, hardware2ProgramRefId, sectionPath[] }
ApplicationProgram{ id, name, applicationNumber, applicationVersion, programType, maskVersion,
                    loadProcedureStyle, peiType, dynamicTableManagement, defaultLanguage, attributes,
                    absoluteSegments[{id,address,size,data?}], relativeSegments[{id,name,offset,size,loadStateMachine}],
                    parameterTypes[{kind Number|Restriction|Text|Float|None|Unsupported, element, sizeInBit, bounds,
                                    enumeration[]}],
                    parameters[{id,name,parameterType,text,value,access, memory?|property?, unionMember?}],
                    unions[{sizeInBit, memory?, parameters[]}], parameterRefs[{id,refId, text?,value?,access?}],
                    comObjectTable?, comObjects[], comObjectRefs[] (every field an optional override),
                    addressTable?, associationTable? {codeSegment, offset, maxEntries?},
                    loadProcedures[{mergeId?, controls[LoadControl{element, attributes, children}]}],
                    dynamic (DynamicNode tree), channels[], hasModuleDefs, translations }
```
- **Effective com objects** (`effectiveComObjects(language)`): each `ComObjectRef` merged over its `ComObject`
  (text, function text, size, C/R/W/T/U/I flags, DPT), texts translated, plus the channel that references it.
  Ordered by number then ref id; several refs per object number are normal (the dynamic evaluator picks one).
- **Dynamic**: the whole `<Dynamic>` section is kept as a `DynamicNode` tree (`choose ParamRefId` / `when test|default`
  included, unknown elements as `Other`) for the later `DynamicEvaluator`. `channels` is a flat index: the
  channel-independent part first (all `ChannelIndependentBlock`s and refs outside channels), then each `Channel`
  with its parameter blocks and every `ComObjectRefRef` / `ParameterRefRef` below it, conditions ignored. This is
  how a user finds "channel F, switch object".
- **Translations**: `text(id, attribute, language)` tries the exact language (case-insensitive), then another
  region of the same language, then the element's own attribute (its file's `DefaultLanguage`). Translation units
  whose `RefId` starts with an application-program id go to that program, the others to the database.
- `memoryOf(parameter)` resolves union members: union memory + member `Offset`/`BitOffset`.

## 4. Tolerance rules
- Unknown elements and attributes are skipped (attributes stay in `attributes`); unknown `Type*` parameter types
  become `Unsupported` with their element name.
- Warnings (`LoadedDatabase::warnings`, file + line, capped at 1000): invalid numbers or flags, unknown `ObjectSize`,
  invalid base64, dangling `RefId`s (ComObjectRef, ParameterRef, Hardware2Program → missing program, CatalogItem →
  product), missing `Catalog.xml`, namespace differing between files, `ModuleDefs` present (not interpreted yet),
  nesting deeper than 256 levels, other `M-XXXX/*.xml` files that are not KNX documents.
- Errors: no `M-XXXX/Hardware.xml` (`NotFound`); malformed XML, a root other than `<KNX>` or a foreign namespace in
  Hardware, Catalog or an application program (`ParseError`); archive errors from `archive`.

## 5. CLI
`kestrel-cli catalog inspect <file.knxprod> [--language de-DE] [--json]` prints products, application programs
(mask, number, version), channels and effective com objects (number, channel, text, function, size, flags, DPT).
Warnings go to stderr (text) or the `warnings` array (JSON). Exit code 3 when the file cannot be read or parsed.

## 6. Product index (FR-CAT-4)

Users search for a product ("merten 670804") and download its product file from the manufacturer with one
click, without hunting through manufacturers' websites.

- **What ships:** `src/catalog/data/product-index.json`, a list of products with the manufacturer's official
  download (built in as a resource; `catalog::builtinProductIndex()`). Never the files (ADR-009): manufacturers
  allow downloading their product files, not redistributing them.
  ```json
  {"version": 1, "products": [{"manufacturer": "Merten (Schneider Electric)", "manufacturerId": "M-0064",
    "orderNumber": "MTN670804", "product": "Push-button interface, 4-gang plus", "category": "binary input",
    "url": "https://download.se.com/files?p_Doc_Ref=…", "page": "…", "format": "zip", "size": 459336,
    "sha256": "…", "applicationId": "M-0064_A-122A-12-E6CB-O000C", "mask": "MV-0021", "verified": "2026-10-29"}]}
  ```
  Each entry was checked by downloading it: the product text, order number, application and mask come from
  the file itself, and size and SHA-256 are of the download on the `verified` date.
- **Search** (`searchProductIndex`): every word must match the manufacturer, product, order number or
  category. Order numbers match without punctuation and case ("mtn-670804"); words of five letters or more
  also match one edit away ("mergten"). Ranked: order number, then the sum of the fields' best matches.
- **Fetch** (`app::ProductFetcher`): HTTP(S) GET with redirects (also `file://` for tests), 256 MB limit, a
  browser-like User-Agent (some servers refuse others). A download whose SHA-256 differs from the index is
  still used (the manufacturer updated the file) and reported. `extractProductFiles` takes the `.knxprod` out
  of it: the download itself, the `.knxprod` entries of a zip, or of a zip inside it.
- **GUI:** the Catalog panel's search also lists matching index products ("From manufacturers") with
  Download, progress and Cancel, "In catalog" once imported (same manufacturer id and order number), and the
  device family of the mask (Kestrel downloads to System B and System 2 devices). The imported file is
  handed to `CatalogService::importFile`.
- **CLI:** `kestrel-cli catalog search <words>` and `catalog fetch <order number> [--to dir]`; `--index` uses
  another index file.
- **Generators** (`tools/product-index/`, standard-library Python): one script per brand lists every KNX product
  file the manufacturer publishes (ABB, Busch-Jaeger, Gira, Hager, Jung, MDT, Schneider/Merten, Siemens,
  Theben, Zennio), downloads each file once in memory, reads every product in it and writes the entries;
  `knxindex.py merge` keeps one entry per manufacturer id and order number (newest application, then the
  smaller download). Only ETS4+ files are listed (no VD1–VD5); files behind a click-through licence are left
  out, since Kestrel's Download would skip the licence. When a product is only in a manufacturer's complete
  database, the entry points to that (large) file.
- Links go stale when manufacturers reorganise their sites or rename files on a new version: re-run the
  generators before a release and compare the counts.

## 7. Test data (ADR-009)
`tests/data/catalog/M-FFFF/*.xml` are hand-written for the fake manufacturer M-FFFF: a 2-fold push button (MV-07B0,
relative segments, a union, enumerations, choose/when per channel, de-DE defaults with en-US/de-DE translations) and
an 8-fold switch actuator (MV-0705, channels A–H, absolute segments, product load procedure).
`tests/data/archive/make_fixtures.py` zips them into `M-FFFF.knxprod` (committed; `.gitignore` re-includes
`tests/data/**/*.knxprod`) and writes the ZIP fixtures. A unit test fails if the committed `.knxprod` is stale.

## 8. Open points
- `ModuleDefs`/`Module` instantiation (channels defined in modules, `{{argCH}}` text placeholders) is not resolved.
- `knx_master.xml` (mask versions, DPT names, medium types) is not parsed.
- `ComObjectRef` overrides are applied attribute by attribute; text parameters (`TextParameterRefId`) are kept raw.
- Real-world files have only been exercised synthetically so far; validation against the private corpus is pending.
