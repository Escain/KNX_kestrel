#!/usr/bin/env python3
"""ABB product index: every ETS4+ product file (.knxprod, or a zip of them) in the ABB Library.

The ABB Library (library.abb.com) has an unauthenticated search API. The file list comes from two official sources:

1. Searches of the API (POST api/public/documents). Free text "knxprod" (all document kinds: some Spanish copies
   are filed as "Technical description"), and "KNX", "ETS", "application", "Applikation", "ETS Application"
   restricted to the document kind "Software". Every hit whose file suffix is "knxprod" is a candidate; a "zip" is a
   candidate only when its title says it holds ETS4+ product data (firmware, VD files, plugins, projects, CAD and
   tools are not). One document number can have several parts (knxprod, vd5, vd2...): only the knxprod part counts.
2. ABB's "Software Repository" workbook (document 9AKK107992A3591, an .xlsm in the same library), which lists the
   ETS applications of ABB (DESTO), Busch-Jaeger (DEBJE) and others with their library links. It adds a few files
   the search no longer returns but the library still serves without login.

Download URL: the document's latestRevisionUrl exactly as the API gives it. Download.aspx needs the document's
whole set of LanguageCode values (and its DocumentPartId); a single language that is not the document's set
redirects to the document's web page instead of the file. Page: https://library.abb.com/d/<DocumentID>.

Each file is downloaded once (in memory only) and read with knxindex.entries(). The state file keeps every file's
entries for all manufacturer ids, so busch_jaeger.py (same library, same enumeration) reuses this script's state
instead of downloading again, and the other way round. Files without products (VD files, firmware, tools) are
recorded and skipped. "Mega-files" (ABB's ETS4 / ETS5-6 "Applications ABB i-bus KNX" archives, the Spanish and
Finnish range databases, complete databanks: 50+ order numbers, or such a title) only contribute products found in
no other file. When knxindex.py changes (its SHA-256 is kept in the state), cached entries are recomputed.

Entries are labelled by the file's manufacturer id: M-0002 → ABB (this script), M-0007 → Busch-Jaeger
(busch_jaeger.py); another id in a dedicated (non-range) ABB document is an ABB group company (ABB S.p.A. M-000E,
the curtain motor's M-0225) → ABB. ABB-branded variants of Busch-Jaeger products (order numbers ending in -500)
carry M-0002 and are ABB.

usage: python3 -I abb.py [--out OUT.json] [--state STATE.json] [--limit N]
"""

import argparse
import hashlib
import io
import json
import os
import re
import sys
import time
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile

sys.dont_write_bytecode = True  # keep the tools folder clean
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

API = "https://discoveryapi.library.abb.com/api/public/documents"
DOWNLOAD = "https://search.abb.com/library/Download.aspx"
PAGE = "https://library.abb.com/d/%s"
REPOSITORY_ID = "9AKK107992A3591"
OUT_DIR = knxindex.OUT_DIR
MIN_INTERVAL = 1.1  # seconds between requests (a download is two: Download.aspx → signed file URL)
PAGE_SIZE = 1000

# (free text, document kind or None for all kinds)
QUERIES = [("knxprod", None), ("KNX", "Software"), ("ETS", "Software"), ("ETS Application", "Software"),
           ("application", "Software"), ("Applikation", "Software")]

ZIP_WANTED = re.compile(r"knxprod|\bets ?[456]\b|ets[ -]appli|ets files|databank|datenbank", re.I)
ZIP_UNWANTED = re.compile(r"firmware|\.vd\d?\b|\(\.vd|vd5|plug-?in|webinar|knxproj|\.exe|(?<![a-z0-9])ets ?[23](?![0-9])|\bcad\b|dwg|"
                          r"tool|reducer|\.img", re.I)
MEGA = re.compile(r"complete|databank|datenbank|collection|all products|alle produkte|gesamt|base de datos disponible|"
                  r"tuotevalikoima|applications abb|applications busch", re.I)
MEGA_PRODUCTS = 50  # a file with this many order numbers is a range file (single applications have <= 12)
DERIVED = ("ok", "no products", "unreadable")  # statuses whose record depends on knxindex.py

BRANDS = {"M-0002": "ABB", "M-0007": "Busch-Jaeger"}
SCRIPT_OF = {"ABB": "abb", "Busch-Jaeger": "busch_jaeger"}

_last_request = [0.0]


def polite_fetch(url, data=None, headers=None):
    wait = _last_request[0] + MIN_INTERVAL - time.monotonic()
    if wait > 0:
        time.sleep(wait)
    try:
        return knxindex.fetch(url, data=data, headers=headers, delay=2.0)
    finally:
        _last_request[0] = time.monotonic()


# --- enumeration ---------------------------------------------------------------------------------------------

def search(text, kind):
    """Every document (latest revision metadata) of one API search."""
    found, page = [], 1
    while True:
        filters = [{"Criteria": "havingFreeText", "Origin": "filter", "Values": [text]}]
        if kind:
            filters.append({"Criteria": "ofDocumentKind", "Origin": "filter", "Values": [kind]})
        body = {"Filters": filters,
                "ResultsControl": {"PageNumber": page, "PageSize": PAGE_SIZE,
                                   "Sort": [{"SortBy": "DocumentNumber", "SortOrder": "Ascending"}]}}
        data, _, _ = polite_fetch(API, json.dumps(body).encode(), {"Content-Type": "application/json",
                                                                    "Origin": "https://library.abb.com"})
        result = json.loads(data)
        documents = [d["metadata"] for d in result.get("documents", [])]
        found += documents
        if not documents or page * PAGE_SIZE >= result.get("numberOfAllHits", 0):
            return found
        page += 1


def _revision_rank(metadata):
    """Newest record of a document: highest revision, then latest publication (one revision can be listed twice,
    with different language sets)."""
    revision = metadata["identification"].get("revision") or ""
    return (len(revision), revision, metadata.get("publishedDate") or "")


def search_candidates(log):
    """{(document id, part): candidate} from the API searches."""
    seen = {}
    for text, kind in QUERIES:
        documents = search(text, kind)
        log("search %r (%s): %d hits" % (text, kind or "all kinds", len(documents)))
        for m in documents:
            ident = m["identification"]
            key = (ident["documentNumber"].strip(), (ident.get("part") or "").strip())
            if key not in seen or _revision_rank(m) > _revision_rank(seen[key]):
                seen[key] = m
    out = {}
    for key, m in seen.items():
        suffix = (m.get("fileSuffix") or "").lower()
        title = (m.get("displayTitle") or {}).get("title") or ""
        if suffix == "knxprod" or (suffix == "zip" and ZIP_WANTED.search(title) and not ZIP_UNWANTED.search(title)):
            out[key] = {"url": m["latestRevisionUrl"], "doc": key[0], "part": key[1], "title": title,
                        "kind": m.get("documentKind"), "suffix": suffix, "source": "search"}
    return out


# Minimal .xlsx reader (shared strings, cell values, hyperlinks) for the Software Repository workbook.
_NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
_REL = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"


def _sheets(data):
    book = zipfile.ZipFile(io.BytesIO(data))
    names = book.namelist()
    strings = []
    if "xl/sharedStrings.xml" in names:
        strings = ["".join(t.text or "" for t in si.iter(_NS + "t"))
                   for si in ET.fromstring(book.read("xl/sharedStrings.xml")).iter(_NS + "si")]
    for name in sorted(n for n in names if re.match(r"xl/worksheets/sheet\d+\.xml$", n)):
        rels_name = name.replace("worksheets/", "worksheets/_rels/") + ".rels"
        rels = {}
        if rels_name in names:
            rels = {r.get("Id"): r.get("Target") for r in ET.fromstring(book.read(rels_name))}
        root = ET.fromstring(book.read(name))
        links = {h.get("ref"): rels.get(h.get(_REL), "") for h in root.iter(_NS + "hyperlink")}
        rows = []
        for row in root.iter(_NS + "row"):
            cells = {}
            for c in row.iter(_NS + "c"):
                v = c.find(_NS + "v")
                value = v.text if v is not None and v.text else ""
                if c.get("t") == "s" and value:
                    value = strings[int(value)]
                ref = c.get("r")
                cells[re.match(r"[A-Z]+", ref).group(0)] = (str(value).strip(), links.get(ref, ""))
            rows.append(cells)
        yield rows


def repository_links(log):
    """[(document id, part, url, sheet title)] of the ETS4+ applications in ABB's Software Repository workbook."""
    url = "%s?DocumentID=%s&LanguageCode=en&DocumentPartId=&Action=Launch" % (DOWNLOAD, REPOSITORY_ID)
    data, _, _ = polite_fetch(url)
    links = {}
    for rows in _sheets(data):
        columns = None
        for cells in rows:
            texts = {col: value for col, (value, _) in cells.items()}
            if "File Type" in texts.values() and "Software Type" in texts.values():
                columns = {value: col for col, value in texts.items()}
                continue
            if not columns:
                continue
            file_type = texts.get(columns["File Type"], "").lower()
            software = texts.get(columns["Software Type"], "")
            if file_type != "knxprod" or not software.upper().startswith("ETS"):
                continue
            col = columns.get("Software/Application Link")
            value, link = cells.get(col, ("", "")) if col else ("", "")
            link = (link or value).strip()
            query = urllib.parse.parse_qs(urllib.parse.urlsplit(link).query)
            # Some cells hold a link pasted into another link: the last DocumentID is the document.
            ids = re.findall(r"DocumentID=([^&#\s]+)", urllib.parse.unquote(link), re.I)
            doc = ids[-1].strip() if ids else ""
            if not doc:
                continue
            part = (query.get("DocumentPartId") or [""])[0].strip()
            languages = [code.strip() for code in query.get("LanguageCode", []) if code.strip()] or ["en"]
            clean = DOWNLOAD + "?" + urllib.parse.urlencode(
                [("DocumentID", doc)] + [("LanguageCode", code) for code in languages] +
                [("DocumentPartId", part), ("Action", "Launch")])
            links.setdefault((doc, part), (clean, texts.get(columns.get("File-name", ""), "")))
    log("software repository: %d ETS4+ application documents" % len(links))
    return [[doc, part, url, name] for (doc, part), (url, name) in sorted(links.items())]


# --- files ---------------------------------------------------------------------------------------------------

def _file_name(final_url, fallback):
    name = urllib.parse.unquote(urllib.parse.urlsplit(final_url).path.rsplit("/", 1)[-1])
    return name or fallback


def process(candidate):
    """State record of one candidate: its entries (all manufacturer ids) or why it has none."""
    record = {"doc": candidate["doc"], "title": candidate["title"], "source": candidate["source"],
              "page": PAGE % candidate["doc"], "mega": bool(MEGA.search(candidate["title"])), "entries": []}
    try:
        data, final_url, headers = polite_fetch(candidate["url"])
    except Exception as error:  # noqa: BLE001 - every failure is recorded, the run goes on
        record["status"] = "error: %s" % error
        return record
    host = urllib.parse.urlsplit(final_url).netloc
    if "login" in final_url.lower() or host == "library.abb.com":
        record["status"] = "not a file (%s)" % final_url[:120]
        return record
    name = _file_name(final_url, candidate["doc"])
    record["file"] = name
    record["size"] = len(data)
    if data[:4] not in (b"PK\x03\x04", b"PK\x05\x06"):
        record["status"] = "not a zip/knxprod (%s)" % headers.get("Content-Type", headers.get("content-type", "?"))
        return record
    try:
        entries = knxindex.entries(data, candidate["url"], "", record["page"], name)
    except (zipfile.BadZipFile, ET.ParseError, KeyError, ValueError) as error:
        record["status"] = "unreadable: %s" % error
        return record
    for entry in entries:
        entry["manufacturer"] = BRANDS.get(entry["manufacturerId"], "")
    record["entries"] = entries
    record["status"] = "ok" if entries else "no products"
    return record


# --- state ---------------------------------------------------------------------------------------------------

def load(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def save(path, data):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False)
    os.replace(temporary, path)


def is_final(record):
    """True when a state record needs no new download (transient errors are retried on a rerun; 404 is final)."""
    status = record.get("status", "") if record else ""
    return bool(record) and (not status.startswith("error") or "HTTP Error 404" in status)


# --- main ----------------------------------------------------------------------------------------------------

def run(brand, argv=None):
    slug = SCRIPT_OF[brand]
    parser = argparse.ArgumentParser(description="%s product index from the ABB Library" % brand)
    parser.add_argument("--out", default=os.path.join(OUT_DIR, "%s.json" % slug))
    parser.add_argument("--state", default=os.path.join(OUT_DIR, "state-%s.json" % slug))
    parser.add_argument("--limit", type=int, default=0, help="process at most N new files (testing)")
    args = parser.parse_args(argv)
    os.makedirs(os.path.dirname(os.path.abspath(args.state)), exist_ok=True)

    def log(text):
        print(text, file=sys.stderr, flush=True)

    state = load(args.state)
    state.setdefault("files", {})
    with open(knxindex.__file__, "rb") as handle:
        module = hashlib.sha256(handle.read()).hexdigest()
    if state.get("knxindex") != module:  # entries made by another knxindex.py are recomputed
        stale = [u for u, r in state["files"].items() if r.get("status", "").startswith(DERIVED)]
        for url in stale:
            del state["files"][url]
        if stale:
            log("knxindex.py changed: %d cached files will be read again" % len(stale))
        state["knxindex"] = module
        save(args.state, state)
    others = [os.path.join(os.path.dirname(os.path.abspath(args.state)), "state-%s.json" % s)
              for s in SCRIPT_OF.values() if s != slug]

    candidates = search_candidates(log)
    if "repository" not in state:
        state["repository"] = repository_links(log)
        save(args.state, state)
    known_docs = {doc for doc, _ in candidates}
    added = 0
    for doc, part, url, name in state["repository"]:
        if doc in known_docs:  # the search already gave this document's current knxprod (or it has none)
            continue
        candidates[(doc, part)] = {"url": url, "doc": doc, "part": part, "title": name or doc, "kind": "",
                                   "suffix": "knxprod", "source": "repository"}
        added += 1
    log("%d candidate files (%d only in the software repository)" % (len(candidates), added))

    by_url = {}
    for candidate in candidates.values():
        by_url.setdefault(candidate["url"], candidate)
    todo = [c for url, c in sorted(by_url.items()) if not is_final(state["files"].get(url))]
    log("%d already processed, %d to go" % (len(by_url) - len(todo), len(todo)))

    other_files = {}
    for path in others:
        other = load(path)
        if other.get("knxindex") == module:  # only entries made by this knxindex.py
            other_files.update({u: r for u, r in other.get("files", {}).items() if is_final(r)})
    done = 0
    for index, candidate in enumerate(todo, 1):
        url = candidate["url"]
        if url in other_files:
            state["files"][url] = other_files[url]
            continue
        if args.limit and done >= args.limit:
            break
        record = process(candidate)
        done += 1
        state["files"][url] = record
        save(args.state, state)
        log("[%d/%d] %s %s: %s, %d entries" % (index, len(todo), candidate["doc"], candidate["title"][:60],
                                             record["status"], len(record["entries"])))
    save(args.state, state)

    # Products of the current candidates only; mega-files add only products found nowhere else.
    records = [state["files"][url] for url in by_url if url in state["files"]]
    for r in records:
        r["mega"] = bool(MEGA.search(r.get("title", ""))) or len(
            {knxindex._fold(e["orderNumber"]) for e in r["entries"]}) >= MEGA_PRODUCTS
    regular = [r for r in records if not r.get("mega")]
    covered = {(e["manufacturerId"], knxindex._fold(e["orderNumber"])) for r in regular for e in r["entries"]}
    entries = [e for r in regular for e in r["entries"]]
    for r in records:
        if r.get("mega"):
            entries += [e for e in r["entries"]
                        if (e["manufacturerId"], knxindex._fold(e["orderNumber"])) not in covered]
    # Other manufacturer ids in a dedicated ABB Library document are ABB group companies: ABB. Other ids that
    # only occur in range files (Theben, a BIM dummy) are not indexed.
    for r in records:
        for e in r["entries"]:
            e["manufacturer"] = BRANDS.get(e["manufacturerId"]) or ("" if r["mega"] else "ABB")
    unbranded = sorted({e["manufacturerId"] for e in entries if not e["manufacturer"]})
    products = knxindex.dedupe([e for e in entries if e["manufacturer"] == brand])
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")

    statuses = {}
    for r in records:
        statuses[r["status"].split(":")[0].split(" (")[0]] = statuses.get(r["status"].split(":")[0].split(" (")[0],
                                                                          0) + 1
    files = {p["url"] for p in products}
    log("files: %s" % ", ".join("%s %d" % kv for kv in sorted(statuses.items())))
    if unbranded:
        log("manufacturer ids not indexed (neither ABB nor Busch-Jaeger): %s" % ", ".join(unbranded))
    log("%s: %d products from %d files → %s" % (brand, len(products), len(files), args.out))
    pending = len([c for c in by_url if not is_final(state["files"].get(c))])
    if pending:
        log("%d files still pending (errors or --limit): run again to continue" % pending)
    return 0


if __name__ == "__main__":
    sys.exit(run("ABB"))
