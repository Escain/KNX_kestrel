#!/usr/bin/env python3
"""Siemens product index: every ETS4+ product file listed on the Siemens KNX technical data site.

The site's search page (search_find_en.htm) is driven by one JavaScript array, l10n/db_en.js: 18 strings per
row (release state "ok"/"old", family, order number, product text, VD file, ETS file, ...). Column 5 is the ETS
product file; the page's own script (sf.js) links it as product_data/ets_zip/ (.zip), product_data/ets5/ (name
contains ETS5), product_data/ets6/ (name contains ETS6) or product_data/ets4/ (anything else). Archived ("old")
products stay downloadable and are indexed too. VD files (pre-ETS4, column 4), PDFs and firmware are ignored.

Each file is downloaded once (in memory only). Progress is kept in a state file, so a rerun continues.

usage: python3 -I siemens.py [--out OUT.json] [--state STATE.json] [--limit N]
"""

import argparse
import hashlib
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

SITE = "https://knx-td.sihub.siemens.com/"
DB_URL = SITE + "l10n/db_en.js"
SEARCH_PAGE = SITE + "search_find_en.htm"
STRIDE = 18
COL_STATE, COL_FAMILY, COL_ORDER, COL_TEXT, COL_VD, COL_ETS = 0, 1, 2, 3, 4, 5
OUT_DIR = knxindex.OUT_DIR
MIN_INTERVAL = 1.1  # seconds between requests

_last_request = [0.0]


def polite_fetch(url):
    wait = _last_request[0] + MIN_INTERVAL - time.monotonic()
    if wait > 0:
        time.sleep(wait)
    try:
        return knxindex.fetch(url, delay=2.0)
    finally:
        _last_request[0] = time.monotonic()


def rows_of(js):
    """The db array of db_en.js as rows of 18 strings (JavaScript string escapes decoded)."""
    body = js[js.index("["):js.rindex("]") + 1]
    items = [json.loads('"%s"' % s) if "\\" in s else s for s in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
    rows = [items[k:k + STRIDE] for k in range(0, len(items) - len(items) % STRIDE, STRIDE)]
    return [r for r in rows if r[COL_STATE] in ("ok", "old")]


def candidates(name):
    """(file name, licence page or "", download URLs to try) for one listed ETS file.

    The search page's own folder rule comes first, then the other folders. Some files are listed behind a
    click-through licence page ("eula_dali_.htm?file=X.zip", "licd_en.htm?file=X.knxprod"); X sits next to that
    page. The file store is case-sensitive and a few listed names differ from the stored ones only in case or by a
    doubled first letter ("n152.knxprod" is N152.knxprod, "Nn350.knxprod" is N350.knxprod), so those spellings
    are tried last."""
    gate = ""
    match = re.match(r"^([^?/]+\.html?)\?file=(.+)$", name)
    if match:
        gate, name = match.group(1), match.group(2)
    upper = name.upper()
    if name.lower().endswith(".zip"):
        first = "ets_zip"
    elif upper.rfind("ETS5") > 0:
        first = "ets5"
    elif upper.rfind("ETS6") > 0:
        first = "ets6"
    else:
        first = "ets4"
    licence = SITE + "product_data/%s/%s?file=%s" % (first, gate, urllib.parse.quote(name)) if gate else ""
    folders = [first] + [f for f in ("ets4", "ets5", "ets6", "ets_zip") if f != first]
    urls = [SITE + "product_data/%s/%s" % (folder, urllib.parse.quote(name)) for folder in folders]
    stem, dot, extension = name.rpartition(".")
    variants = [stem.upper() + dot + extension.lower(), re.sub(r"(?i)ets([56])", r"ETS\1", name)]
    typo = re.match(r"^([A-Za-z])\1(.*)$", name, re.I)
    if typo:
        variants.append(typo.group(1) + typo.group(2))
    for variant in variants:
        url = SITE + "product_data/%s/%s" % (first, urllib.parse.quote(variant))
        if variant != name and url not in urls:
            urls.append(url)
    return name, licence, urls


def drop_bogus(entries):
    """Drop the bogus entries knxindex gives for one .knxprod.

    knxindex._knxprod_products() also takes the <Hardware> list element of Hardware.xml for a hardware and gives
    every product of the file the file's newest application. Those entries carry the file-wide newest
    application; where an order number also has another entry (from its own hardware), drop them. Once knxindex
    is fixed this changes nothing."""
    newest, count = {}, {}
    for e in entries:
        key = (e["_applicationVersion"], e["applicationId"])
        newest[e["manufacturerId"]] = max(newest.get(e["manufacturerId"], key), key)
        count[(e["manufacturerId"], e["orderNumber"])] = count.get((e["manufacturerId"], e["orderNumber"]), 0) + 1
    return [e for e in entries if count[(e["manufacturerId"], e["orderNumber"])] == 1
            or e["applicationId"] != newest[e["manufacturerId"]][1]]


def entries_of(download, url, name):
    """knxindex.entries() of each .knxprod in the download (see drop_bogus); size, hash and format of the download."""
    fmt = "zip"
    if knxindex._is_zip(download):
        if any(re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in zipfile.ZipFile(io.BytesIO(download)).namelist()):
            fmt = "knxprod"
    digest = hashlib.sha256(download).hexdigest()
    out = []
    for knxprod_name, data in knxindex._knxprods(download, name):
        for e in drop_bogus(knxindex.entries(data, url, "Siemens", "", knxprod_name)):
            e.update(size=len(download), sha256=digest, format=fmt)
            out.append(e)
    return out


def fold(text):
    return "".join(c for c in (text or "").lower() if c.isalnum())


def search_page(order):
    return SEARCH_PAGE + "?" + urllib.parse.urlencode({"db": "both", "te1": order})


def load_state(path):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    return {"files": {}}


def save_state(path, state):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(state, handle, ensure_ascii=False)
    os.replace(tmp, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=os.path.join(OUT_DIR, "siemens.json"))
    parser.add_argument("--state", default=os.path.join(OUT_DIR, "state-siemens.json"))
    parser.add_argument("--limit", type=int, default=0, help="process at most N new files (testing)")
    args = parser.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(args.state)), exist_ok=True)

    js = polite_fetch(DB_URL)[0].decode("utf-8", "replace")
    rows = rows_of(js)
    files = {}  # ETS file name as listed → rows
    for row in rows:
        if row[COL_ETS].strip():
            files.setdefault(row[COL_ETS].strip(), []).append(row)
    print("db_en.js: %d rows, %d with an ETS file, %d distinct files" %
          (len(rows), sum(len(v) for v in files.values()), len(files)), flush=True)

    state = load_state(args.state)
    done = state["files"]
    new = 0
    for index, (listed, file_rows) in enumerate(sorted(files.items())):
        if listed in done:
            continue
        if args.limit and new >= args.limit:
            break
        new += 1
        name, licence, urls = candidates(listed)
        record = {"url": "", "licence": licence, "entries": [], "error": "", "fixed": True}
        for url in urls:
            try:
                download = polite_fetch(url)[0]
            except urllib.error.HTTPError as error:
                if error.code in (403, 404):
                    record["error"] = "HTTP %d" % error.code
                    continue
                record["error"] = "HTTP %d" % error.code
                break
            except Exception as error:  # network trouble: leave the file for a rerun
                record["error"] = "%s: %s" % (type(error).__name__, error)
                break
            record["url"], record["error"] = url, ""
            try:
                record["entries"] = entries_of(download, url, name)
            except Exception as error:  # unreadable archive: record it, don't retry forever
                record["error"] = "unreadable: %s: %s" % (type(error).__name__, error)
            record["size"] = len(download)
            del download
            break
        print("[%d/%d] %s → %s %s" % (index + 1, len(files), listed, record["url"] or "-",
                                     record["error"] or "%d products" % len(record["entries"])), flush=True)
        if record["error"] and not record["error"].startswith(("HTTP 4", "unreadable")):
            continue  # transient: retry on the next run
        done[listed] = record
        save_state(args.state, state)

    # Build the index: a human page per product (the search page filtered on its order number).
    products = []
    for listed, record in done.items():
        if listed not in files:
            continue  # no longer listed
        if record.get("licence"):
            continue  # behind a click-through licence: Kestrel must not download it without that page
        orders = {fold(r[COL_ORDER]): r[COL_ORDER] for r in files[listed]}
        # Records of an earlier version of this script hold knxindex.entries() of the whole download.
        for entry in record["entries"] if record.get("fixed") else drop_bogus(record["entries"]):
            order = orders.get(fold(entry["orderNumber"])) or files[listed][0][COL_ORDER]
            entry = dict(entry)
            entry["page"] = search_page(order)
            products.append(entry)
    products = knxindex.dedupe(products)
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")

    with_products = sum(1 for k, r in done.items() if k in files and r["entries"])
    missing = sorted(k for k in files if k not in done)
    errors = sorted((k, r["error"]) for k, r in done.items() if k in files and r["error"])
    print("%d products from %d files (%d files processed, %d with errors, %d not yet processed) → %s" %
          (len(products), with_products, len(done), len(errors), len(missing), args.out))
    for name, error in errors:
        print("  error: %s: %s" % (name, error))


if __name__ == "__main__":
    main()
