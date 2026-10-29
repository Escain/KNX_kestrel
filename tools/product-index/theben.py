#!/usr/bin/env python3
"""Theben product index: every KNX product of Theben's full ETS catalogue and of each single-product database.

Theben has no list of its KNX databases, so the script reads every product page of the English product sitemap
(https://www.theben.de/ocs_sitemaps/de/en/product_sitemap.xml). Each page carries a schema.org Product
(application/ld+json) whose "subjectOf" lists the page's downloads; KNX databases are the ones named
"... KNX Database (Single Product)" / "(All Products)" (file names ...KNXDatenbank.ZIP / ...KNXGesamtdatenbank.zip).

Every distinct database, and the full catalogue (all products, about 22 MB), is downloaded once, in memory only.
All their products go into the index; knxindex.dedupe() keeps, per order number, the newest application, then
the smaller download, so a product points to its own database unless the full catalogue has a newer application.
Products only found in the full catalogue point to it. "page" is the product page whose article number (sku)
matches the order number, else the page that linked the database.

Progress (pages read, databases read) is kept in a state file, so a rerun continues.

usage: python3 -I theben.py [--out OUT.json] [--state STATE.json] [--limit N]
"""

import argparse
import hashlib
import html
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

SITE = "https://www.theben.de/"
SITEMAP = SITE + "ocs_sitemaps/de/en/product_sitemap.xml"
FULL_FALLBACK = SITE + "ocsmedia/optimized/full/o27748v146_KNXprod_KNXGesamtdatenbank.zip"
OUT_DIR = knxindex.OUT_DIR
MIN_INTERVAL = 1.1  # seconds between requests
STATE_VERSION = 1

_last_request = [0.0]


def polite_fetch(url):
    url = urllib.parse.quote(url, safe=":/?&=%#~+,;@!$'()*[]")  # the sitemap has a zero-width space in a URL
    wait = _last_request[0] + MIN_INTERVAL - time.monotonic()
    if wait > 0:
        time.sleep(wait)
    try:
        return knxindex.fetch(url, delay=2.0)
    finally:
        _last_request[0] = time.monotonic()


def entries_of(download, url, name):
    """knxindex.entries() of each .knxprod in the download, without the bogus per-file entries.

    knxindex._knxprod_products() also takes the <Hardware> list element of Hardware.xml for a hardware and gives
    every product of the file the file's newest application. Those entries carry the file-wide newest
    application; where an order number also has another entry (from its own hardware), drop them. Once knxindex
    is fixed this changes nothing. Size, hash and format are the download's."""
    fmt = "zip"
    if knxindex._is_zip(download):
        if any(re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in zipfile.ZipFile(io.BytesIO(download)).namelist()):
            fmt = "knxprod"
    digest = hashlib.sha256(download).hexdigest()
    out = []
    for knxprod_name, data in knxindex._knxprods(download, name):
        part = knxindex.entries(data, url, "Theben", "", knxprod_name)
        newest, count = {}, {}
        for e in part:
            key = (e["_applicationVersion"], e["applicationId"])
            newest[e["manufacturerId"]] = max(newest.get(e["manufacturerId"], key), key)
            count[(e["manufacturerId"], e["orderNumber"])] = count.get((e["manufacturerId"], e["orderNumber"]), 0) + 1
        for e in part:
            if count[(e["manufacturerId"], e["orderNumber"])] > 1 and \
                    e["applicationId"] == newest[e["manufacturerId"]][1]:
                continue
            e.update(size=len(download), sha256=digest, format=fmt)
            out.append(e)
    return out


def fold(text):
    return "".join(c for c in (text or "").lower() if c.isalnum())


def page_info(body):
    """{sku, name, category, databases: [[name, url]]} of a product page (schema.org Product), or None."""
    text = body.decode("utf-8", "replace")
    for match in re.finditer(r'<script type="application/ld\+json">(.*?)</script>', text, re.S):
        try:
            data = json.loads(match.group(1))
        except ValueError:
            continue
        if data.get("@type") != "Product":
            continue
        databases = []
        for doc in data.get("subjectOf") or []:
            url, name = doc.get("url") or "", html.unescape(doc.get("name") or "")
            file_name = url.rsplit("/", 1)[-1]
            if re.search(r"KNX ?Database|KNX-?Datenbank", name, re.I) or \
                    re.search(r"KNX_?(Gesamt)?Datenbank\.zip$", file_name, re.I):
                databases.append([name, url])
        return {"sku": str(data.get("sku") or ""), "name": data.get("name") or "",
                "category": data.get("category") or "", "databases": databases}
    return None


def load_state(path):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            state = json.load(handle)
        if state.get("version") == STATE_VERSION:
            return state
    return {"version": STATE_VERSION, "pages": {}, "downloads": {}}


def save_state(path, state):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(state, handle, ensure_ascii=False)
    os.replace(tmp, path)


def order_keys(order):
    """Folded article numbers an order number may stand for ("1409200 / 1409203" → both)."""
    return [fold(part) for part in re.split(r"\s*/\s*", order) if fold(part)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=os.path.join(OUT_DIR, "theben.json"))
    parser.add_argument("--state", default=os.path.join(OUT_DIR, "state-theben.json"))
    parser.add_argument("--limit", type=int, default=0, help="read at most N new pages and N new databases (testing)")
    args = parser.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(args.state)), exist_ok=True)
    state = load_state(args.state)

    # 1. Product pages from the sitemap.
    sitemap = polite_fetch(SITEMAP)[0].decode("utf-8", "replace")
    pages = [html.unescape(u).strip() for u in re.findall(r"<loc>([^<]+)</loc>", sitemap)]
    print("sitemap: %d product pages" % len(pages), flush=True)
    new = 0
    for index, page in enumerate(pages):
        if page in state["pages"]:
            continue
        if args.limit and new >= args.limit:
            break
        new += 1
        try:
            body = polite_fetch(page)[0]
        except urllib.error.HTTPError as error:
            if error.code not in (403, 404, 410):
                print("[%d/%d] %s: HTTP %d (retry on next run)" % (index + 1, len(pages), page, error.code))
                continue
            state["pages"][page] = {"error": "HTTP %d" % error.code, "databases": []}
        except Exception as error:
            print("[%d/%d] %s: %s (retry on next run)" % (index + 1, len(pages), page, error))
            continue
        else:
            state["pages"][page] = page_info(body) or {"error": "no schema.org Product", "databases": []}
        info = state["pages"][page]
        if info["databases"] or info.get("error"):
            print("[%d/%d] %s: %s" % (index + 1, len(pages), page,
                                      info.get("error") or ", ".join(n for n, _ in info["databases"])), flush=True)
        save_state(args.state, state)
    missing_pages = [p for p in pages if p not in state["pages"]]

    # 2. Distinct databases: single-product ones, and the full catalogue (the most linked "All Products" one).
    singles, fulls = {}, {}
    for page in pages:
        for name, url in state["pages"].get(page, {}).get("databases", []):
            if re.search(r"All Products|Gesamtdatenbank", name + " " + url, re.I):
                fulls[url] = fulls.get(url, 0) + 1
            else:
                singles.setdefault(url, []).append(page)
    full_url = max(fulls, key=fulls.get) if fulls else FULL_FALLBACK
    downloads = sorted(singles) + [full_url]
    print("%d single-product databases, full catalogue %s (linked from %d pages)" %
          (len(singles), full_url, fulls.get(full_url, 0)), flush=True)

    # 3. Download each once.
    new = 0
    for index, url in enumerate(downloads):
        if url in state["downloads"]:
            continue
        if args.limit and new >= args.limit:
            break
        new += 1
        record = {"entries": [], "error": ""}
        try:
            download = polite_fetch(url)[0]
            record["size"] = len(download)
            record["entries"] = entries_of(download, url, url.rsplit("/", 1)[-1])
            del download
        except urllib.error.HTTPError as error:
            record["error"] = "HTTP %d" % error.code
        except Exception as error:
            record["error"] = "%s: %s" % (type(error).__name__, error)
        print("[%d/%d] %s → %s" % (index + 1, len(downloads), url,
                                    record["error"] or "%d products" % len(record["entries"])), flush=True)
        if record["error"] and not record["error"].startswith(("HTTP 4", "BadZipFile")):
            continue  # transient: retry on the next run
        state["downloads"][url] = record
        save_state(args.state, state)

    # 4. Index: a product page per entry.
    by_sku = {}
    for page in pages:
        sku = fold(state["pages"].get(page, {}).get("sku"))
        if sku:
            by_sku.setdefault(sku, page)

    def page_for(order, linked):
        for key in order_keys(order):
            if key in by_sku:
                return by_sku[key]
        for key in order_keys(order):  # "208 9 00*", "2059***", "500 9 2xx": wildcard article numbers
            if re.search(r"[*x]$", key):
                pattern = re.compile("^" + re.sub(r"[*x]", r"[0-9a-z]", key) + "$")
                for sku in sorted(by_sku):
                    if pattern.match(sku):
                        return by_sku[sku]
        return linked[0] if linked else ""

    products = []
    for url in downloads:
        record = state["downloads"].get(url)
        if not record:
            continue
        for entry in record["entries"]:
            if not fold(entry["orderNumber"]):
                continue  # "-": the ETS dummy device
            entry = dict(entry)
            entry["page"] = page_for(entry["orderNumber"], singles.get(url, []))
            products.append(entry)
    products = knxindex.dedupe(products)
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")

    used = {p["url"] for p in products}
    errors = sorted((u, r["error"]) for u, r in state["downloads"].items() if u in downloads and r["error"])
    empty = sorted(u for u, r in state["downloads"].items() if u in downloads and not r["error"] and not r["entries"])
    print("%d products from %d files (%d of %d downloads read, %d errors, %d without products, "
          "%d pages not yet read; %d products point to the full catalogue) → %s" %
          (len(products), len(used), sum(1 for u in downloads if u in state["downloads"]), len(downloads),
           len(errors), len(empty), len(missing_pages), sum(1 for p in products if p["url"] == full_url), args.out))
    for url, error in errors:
        print("  error: %s: %s" % (url, error))
    for url in empty:
        print("  no products: %s" % url)


if __name__ == "__main__":
    main()
