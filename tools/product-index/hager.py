#!/usr/bin/env python3
"""Product index entries for Hager (including Berker products sold by Hager): every ETS4+ product file linked
from a hager.com product page.

Listing: https://www.hager.com/sitemap-index.xml (advertised in robots.txt) names one catalogue sitemap per
country site (…/deep/sitemap_<cc>.xml). Country sites sell different ranges, so all of them are read. A product
page names its order code first in the URL (…/tybs704a-flush-mounted-…). The codes that can be KNX devices (a
KNX word in the page name in any language, or a Hager KNX code prefix) are read on one country site (a KNX-named
code on up to three: sites do not all link the same files); the page links the product's files on
https://assets.hager.com/step-content/P/HA_<id>/OT/std.lang.all/<file>. The sitemaps leave out part of the
catalogue (TYBS704A, TYFS120, TYA624C are in none), so the related products linked from every KNX device page
(one page per product file and country site) are added to the candidates until no new code turns up. Every
.knxprod (or product .zip) found is downloaded once, read in memory with knxindex.entries() and dropped.

    python3 -I -B hager.py [--state FILE] [--out FILE]

Resumable: the state file keeps the product codes, the file links found on each page and the entries of every
processed download, so a rerun only asks for what is missing. Delete it to start over.
"""

import argparse
import gzip
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

SCRATCH = knxindex.OUT_DIR
SITEMAP_INDEX = "https://www.hager.com/sitemap-index.xml"
MIN_INTERVAL = 1.1  # seconds between two requests

# Page names (any language) of products that can be KNX devices.
KNX_WORDS = re.compile(r"knx|eib|instabus|tebis|domovea|quicklink|b-iq|tastsens|tasterschn|aktor|actuat|actionn|"
                       r"binaer|binary|dali", re.I)
# Hager KNX order code prefixes (the page name does not always say KNX: tybs704a-flush-mounted-input-device-4-fold).
KNX_CODES = re.compile(r"^(ty|tx|tc|tj|tg|wkt|tr[a-z]?\d)", re.I)
# A strong hint that the page should link a product file; such pages are tried on more country sites.
STRONG = re.compile(r"knx|tebis", re.I)
# Covers, frames and other accessories of KNX devices: read once, not on more sites.
ACCESSORY = re.compile(r"(^|-)(abd|abdeckung|abdeckrahmen|rahmen|wippe|wippen|cover|covers|frame|plaque|enjoliveur|"
                       r"doigts?|touches?|cache|kappe|placca|tasti|tecla|teclas|afdekking|blende|zubehoer|"
                       r"accessory|accessoire)(-|$)", re.I)
# Preferred country sites for the page (English first).
SITES = ["uk", "au", "nz", "middle-east", "sg", "my", "en-tr", "in", "de", "at", "de-ch", "fr", "nl", "it", "es",
         "intl-en", "ie"]
# Sites whose product pages often leave out the product file that other sites link; a try there does not count.
PARTIAL_SITES = {"intl-en", "ie"}
PRODUCT_FILE = re.compile(r"https?://assets\.hager\.com/step-content/[^\"'<>\s]+/OT/[^\"'<>\s]+\.(?:knxprod|zip)",
                          re.I)
# Zips that are not product data (a .knxprod always is).
NOT_PRODUCT = re.compile(r"firmw|[_-]fw[_-]|update|plug-?in|\.vd\d|ets3|setup|install|label|beschrift|template|"
                         r"vorlage|(^|[\W_])(cad|dwg|bim|revit|eplan|macro)([\W_]|$)", re.I)
FIRMWARE_PARTS = re.compile(r"\.(bin|hex|fw|dfu|exe|msi|img|srec|s19)$", re.I)

_last = [0.0]


def get(url):
    """knxindex.fetch with at most one request per MIN_INTERVAL seconds; gzip-compressed transfer."""
    wait = _last[0] + MIN_INTERVAL - time.time()
    if wait > 0:
        time.sleep(wait)
    try:
        body, final, headers = knxindex.fetch(url, headers={"Accept-Encoding": "gzip"}, delay=2.0)
    finally:
        _last[0] = time.time()
    if body[:2] == b"\x1f\x8b":
        body = gzip.decompress(body)
    return body, final, headers


def load(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError:
        return {}


def save(path, state):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(state, handle, ensure_ascii=False)
    os.replace(tmp, path)


def site_of(url):
    return url.split("/")[3]


def product_code(url):
    """Order code of a product page URL, or ''."""
    slug = url.rstrip("/").rsplit("/", 1)[-1]
    match = re.match(r"^([a-z0-9.]*\d[a-z0-9.]*)-", slug, re.I)
    return match.group(1).lower() if match else ""


def mostly_firmware(data):
    """True for a zip whose bulk is firmware or installers rather than product data."""
    if not data[:2] == b"PK":
        return False
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        return False
    if any(re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in archive.namelist()):
        return False
    firmware = sum(i.file_size for i in archive.infolist() if FIRMWARE_PARTS.search(i.filename))
    total = sum(i.file_size for i in archive.infolist()) or 1
    return firmware > total / 2


def brand(manufacturer_id):
    return "Berker (Hager)" if manufacturer_id == "M-0006" else "Hager"


def candidate(url):
    code = product_code(url)
    return bool(code) and bool(KNX_WORDS.search(url.rsplit("/", 1)[-1]) or KNX_CODES.match(code))


def candidate_codes(state):
    """{order code: {page url}} from the sitemaps and from related-product links."""
    codes = {}
    for urls in list(state["sitemaps"].values()) + list(state["related"].values()):
        for url in urls:
            codes.setdefault(product_code(url), set()).add(url)
    return codes


RELATED = re.compile(r"https://hager\.com/[a-z-]+/[a-z]+/[a-z-]+/[a-z0-9.]*\d[a-z0-9.]*-[^\"'<>\s?#/]*", re.I)


def harvest(page, body, state, path):
    """Keep the candidate product pages a KNX device page links to."""
    found = sorted({u for u in RELATED.findall(body.decode("utf-8", "replace")) if candidate(u)})
    state["related"][page] = found
    state["scanned"].append(page)
    save(path, state)


def read_code(code, urls, state, path):
    """Read a code's product page(s) until one links a product file. Country sites do not all link the same
    files: a code is read on one site, a KNX-named code (not an accessory) on up to three; tries on PARTIAL_SITES
    do not count."""
    record = state["pages"].get(code) or {"tried": [], "links": [], "page": ""}
    if record["links"]:
        return
    urls = sorted(urls, key=lambda u: (SITES.index(site_of(u)) if site_of(u) in SITES else 99, u))
    names = [u.rsplit("/", 1)[-1] for u in urls]
    strong = any(STRONG.search(n) for n in names) and not any(ACCESSORY.search(n) for n in names)
    limit = 3 if strong else 1
    sites_tried = {site_of(u) for u in record["tried"]}
    changed = False
    for url in urls:
        if len([u for u in record["tried"] if site_of(u) not in PARTIAL_SITES]) >= limit:
            break
        if site_of(url) in sites_tried or url in record["tried"]:
            continue
        try:
            body, _, _ = get(url)
        except urllib.error.HTTPError as error:
            print("page %s: HTTP %d" % (url, error.code), file=sys.stderr)
            body = b""
        except (urllib.error.URLError, IOError) as error:
            print("page %s: %s" % (url, error), file=sys.stderr)
            break
        record["tried"].append(url)
        sites_tried.add(site_of(url))
        changed = True
        links = sorted(set(PRODUCT_FILE.findall(body.decode("utf-8", "replace"))))
        if links:
            record["links"] = links
            record["page"] = url
            state["pages"][code] = record
            harvest(url, body, state, path)
            break
    if not changed:
        return
    state["pages"][code] = record
    if record["links"]:
        print("page %s: %s" % (code, " ".join(l.rsplit("/", 1)[-1] for l in record["links"])), flush=True)
    save(path, state)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--state", default=os.path.join(SCRATCH, "state-hager.json"))
    parser.add_argument("--out", default=os.path.join(SCRATCH, "hager.json"))
    args = parser.parse_args()
    os.makedirs(os.path.dirname(args.state), exist_ok=True)
    state = load(args.state)
    for key in ("sitemaps", "pages", "files"):
        state.setdefault(key, {})
    # Entries made by another version of knxindex.py are discarded (their files are read again).
    with open(knxindex.__file__, "rb") as handle:
        module = hashlib.sha256(handle.read()).hexdigest()
    if state.get("module") != module:
        state["files"] = {}
        state["module"] = module

    # 1. Country catalogue sitemaps → candidate product pages per order code.
    if not state.get("index"):
        body, _, _ = get(SITEMAP_INDEX)
        state["index"] = [u for u in re.findall(r"<loc>([^<]+)</loc>", body.decode("utf-8", "replace"))
                          if re.search(r"/(deep|media)/sitemap_[^/]+\.xml$", u)]
        save(args.state, state)
    for sitemap in state["index"]:
        if sitemap in state["sitemaps"]:
            continue
        try:
            body, _, _ = get(sitemap)
        except (urllib.error.URLError, IOError) as error:
            print("sitemap %s: %s" % (sitemap, error), file=sys.stderr)
            continue
        urls = re.findall(r"<loc>([^<]+)</loc>", body.decode("utf-8", "replace"))
        kept = [url for url in urls if candidate(url)]
        state["sitemaps"][sitemap] = kept
        print("%s: %d pages, %d candidates" % (sitemap, len(urls), len(kept)), flush=True)
        save(args.state, state)

    # 2. Product pages → product file links, then more product pages. The sitemaps leave out part of the
    # catalogue (tybs704a is in no sitemap), so the product pages linked from a KNX device page (related products,
    # successors, accessories) that pass the same filter are read too, until no new code turns up.
    state.setdefault("related", {})
    state.setdefault("scanned", [])
    while True:
        codes = candidate_codes(state)
        print("%d candidate product codes" % len(codes), flush=True)
        before = len(codes)
        for code in sorted(codes):
            read_code(code, codes[code], state, args.state)
        # Country sites link different related products: scan one page per product file and country site.
        scanned = set(state["scanned"])
        done = set()
        for code, record in state["pages"].items():
            for url in codes.get(code, ()):
                if record["links"] and url in scanned:
                    done.add((tuple(record["links"]), site_of(url)))
        for code in sorted(state["pages"]):
            record = state["pages"][code]
            if not record["links"]:
                continue
            for url in sorted(codes.get(code, ())):
                key = (tuple(record["links"]), site_of(url))
                if key in done or url in scanned:
                    continue
                done.add(key)
                try:
                    body, _, _ = get(url)
                except urllib.error.HTTPError as error:
                    print("page %s: HTTP %d" % (url, error.code), file=sys.stderr)
                    body = b""
                except (urllib.error.URLError, IOError) as error:
                    print("page %s: %s" % (url, error), file=sys.stderr)
                    continue
                harvest(url, body, state, args.state)
                scanned.add(url)
        if len(candidate_codes(state)) == before:
            break

    # 3. Product files → entries.
    pages_of = {}
    for code in sorted(state["pages"]):
        record = state["pages"][code]
        for link in record["links"]:
            pages_of.setdefault(link, []).append((code, record["page"]))
    for link in sorted(pages_of):
        if link in state["files"]:
            continue
        name = urllib.parse.unquote(link.rsplit("/", 1)[-1])
        if not name.lower().endswith(".knxprod") and NOT_PRODUCT.search(name):
            state["files"][link] = {"skip": "not an ETS4+ product file"}
            continue
        try:
            data, _, _ = get(link)
        except urllib.error.HTTPError as error:
            state["files"][link] = {"skip": "HTTP %d" % error.code}
            save(args.state, state)
            continue
        except (urllib.error.URLError, IOError) as error:
            print("download %s: %s" % (link, error), file=sys.stderr)
            continue
        if mostly_firmware(data):
            state["files"][link] = {"skip": "mostly firmware", "size": len(data)}
        else:
            # The page of a product in the file, else the first page that links the file.
            pages = pages_of[link]
            found = []
            for entry in knxindex.entries(data, link, "", page=pages[0][1], name=name):
                order = knxindex._fold(entry["orderNumber"])
                for code, page in pages:
                    if knxindex._fold(code) == order:
                        entry["page"] = page
                entry["manufacturer"] = brand(entry["manufacturerId"])
                found.append(entry)
            state["files"][link] = {"entries": found} if found else {"skip": "no ETS4+ products in file",
                                                                      "size": len(data)}
            print("file %s: %d products" % (name, len(found)), flush=True)
        del data
        save(args.state, state)

    products = []
    for record in state["files"].values():
        products += record.get("entries", [])
    products = knxindex.dedupe(products)
    products.sort(key=lambda p: (p["manufacturer"], p["orderNumber"]))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")
    files = sum(1 for r in state["files"].values() if r.get("entries"))
    print("%d products from %d files → %s" % (len(products), files, args.out))


if __name__ == "__main__":
    main()
