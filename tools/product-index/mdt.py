#!/usr/bin/env python3
"""Product index entries for every MDT KNX product file (specs/07-catalog.md §6).

Enumeration: the "ETS product data" download pages of MDT's sites (mdt-group.com first, then mdt.de and the
country sites, which sometimes list a file the others do not) are tables with one row per product series; each row
has a category ("Productdatabase ETS5/6", "Device Configuration App (DCA) ETS5/6", "Firmwareupdates", …) and a link
to /fileadmin/…/<file>.knxprod|.zip. Every .knxprod is read; a .zip only when its row is not a DCA or firmware row
and its name does not look like firmware. A file listed on several sites is read once, from the first site.

The "complete" database MDT_KP_Datenbank.zip is read last and only contributes products that no single file has.
.etsapp (DCA plug-ins), firmware updates and PDFs are skipped.

Each file is downloaded once, kept in memory only, and forgotten. Resumable: the state file keeps the entries of
every file processed.

usage: python3 -I mdt.py [--state FILE] [--out FILE]
"""

import html as htmllib
import json
import os
import re
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

SCRATCH = knxindex.OUT_DIR
LISTINGS = [
    "https://www.mdt-group.com/for-professionals/downloads/ets-product-data.html",
    "https://www.mdt.de/downloads/produktdatenbanken-enthaelt-die-datenbanken-und-die-dcs-apps.html",
    "https://www.mdt-group.nl/voor-professionals/downloads/productdatabases.html",
    "https://www.mdt.in/for-professionals/downloads/ets-product-data.html",
    "https://www.mdt.ch/downloads/produktdatenbanken.html",
    "https://www.mdt.uk/downloads/ets-product-data.html",
    "https://www.mdt.us/for-professionals/downloads/ets-product-data.html",
]
PAGE = LISTINGS[0]
MANUFACTURER = "MDT"
INTERVAL = 1.1  # seconds between requests
COMPLETE = re.compile(r"MDT_KP_Datenbank", re.I)
NOT_PRODUCT_DATA = re.compile(r"\bDCA\b|Device Configuration App|Firmware|Update", re.I)
FIRMWARE_NAME = re.compile(r"(^|[_-])(fwu|firmware|update)([_-]|$)", re.I)

_last = [0.0]


def get(url):
    wait = _last[0] + INTERVAL - time.time()
    if wait > 0:
        time.sleep(wait)
    try:
        return knxindex.fetch(url, delay=2.0)
    finally:
        _last[0] = time.time()


def load(path):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    return {}


def save(path, state):
    with open(path + ".tmp", "w", encoding="utf-8") as handle:
        json.dump(state, handle, ensure_ascii=False)
    os.replace(path + ".tmp", path)


def links(page_url, html):
    """[(absolute url, row category)] of the product-data links of one listing page."""
    out = []
    for chunk in re.split(r"<tr\b", html):
        category = re.search(r'class="categorytitle"[^>]*>(.*?)</td>', chunk, re.S)
        category = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", category.group(1))).strip() if category else ""
        for href in re.findall(r'href="([^"]*/fileadmin/[^"]*\.(?:knxprod|zip))"', chunk, re.I):
            out.append((urllib.parse.urljoin(page_url, htmllib.unescape(href)), category))
    return out


def wanted(url, category):
    name = urllib.parse.unquote(url.rsplit("/", 1)[-1])
    if name.lower().endswith(".knxprod"):
        return True
    if COMPLETE.search(name):
        return True
    return not NOT_PRODUCT_DATA.search(category) and not FIRMWARE_NAME.search(name)


def enumerate_files(state, state_path):
    """{path: (url, listing page)} in listing order; skipped links go to state['skippedLinks']."""
    if "listing" not in state:
        files, skipped = {}, {}
        for page in LISTINGS:
            try:
                body, final, _ = get(page)
            except Exception as error:  # noqa: BLE001 - a country site may be down
                print("  listing %s: %s" % (page, error), file=sys.stderr, flush=True)
                continue
            for url, category in links(final, body.decode("utf-8", "replace")):
                path = urllib.parse.urlsplit(url).path
                if not wanted(url, category):
                    skipped.setdefault(path, category)
                elif path not in files:
                    files[path] = [url, page]
        state["listing"] = files
        state["skippedLinks"] = skipped
        save(state_path, state)
    return state["listing"]


def main(argv):
    state_path = os.path.join(SCRATCH, "state-mdt.json")
    out_path = os.path.join(SCRATCH, "mdt.json")
    if "--state" in argv:
        state_path = argv[argv.index("--state") + 1]
    if "--out" in argv:
        out_path = argv[argv.index("--out") + 1]
    os.makedirs(os.path.dirname(state_path), exist_ok=True)
    state = load(state_path)
    state.setdefault("files", {})

    files = enumerate_files(state, state_path)
    # The complete database last: it only fills gaps.
    order = sorted(files.values(), key=lambda f: bool(COMPLETE.search(f[0])))
    print("%d product files listed, %d other links skipped" % (len(files), len(state["skippedLinks"])), flush=True)

    for n, (url, page) in enumerate(order):
        done = state["files"].get(url)
        if done and done.get("status") in ("ok", "empty"):
            continue
        try:
            data, final, _ = get(url)
        except Exception as error:  # noqa: BLE001
            print("  %s: %s" % (url, error), file=sys.stderr, flush=True)
            state["files"][url] = {"status": "error", "error": str(error)}
            save(state_path, state)
            continue
        name = urllib.parse.unquote(final.rsplit("/", 1)[-1])
        entries = knxindex.entries(data, url, MANUFACTURER, page, name)
        state["files"][url] = {"status": "ok" if entries else "empty", "size": len(data), "entries": entries}
        del data
        save(state_path, state)
        print("  [%d/%d] %s: %d products" % (n + 1, len(order), name, len(entries)), flush=True)

    single, complete = [], []
    for url, page in order:
        record = state["files"].get(url, {})
        (complete if COMPLETE.search(url) else single).extend(record.get("entries", []))
    covered = {(p["manufacturerId"], knxindex._fold(p["orderNumber"])) for p in single}
    extra = [p for p in complete if (p["manufacturerId"], knxindex._fold(p["orderNumber"])) not in covered]
    products = knxindex.dedupe(single + extra)
    products = [{**p, "page": PAGE} if not p.get("page") else p for p in products]
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")

    records = [state["files"].get(u, {}) for u, _ in order]
    print("%d products (%d only in the complete database) from %d files (%d without products, %d errors) → %s" % (
        len(products), sum(1 for p in products if COMPLETE.search(p["url"])),
        sum(1 for r in records if r.get("status") == "ok"), sum(1 for r in records if r.get("status") == "empty"),
        sum(1 for r in records if r.get("status") == "error"), out_path))


if __name__ == "__main__":
    main(sys.argv[1:])
