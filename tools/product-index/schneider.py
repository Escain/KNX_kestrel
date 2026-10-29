#!/usr/bin/env python3
"""Product index entries for Schneider Electric (including Merten): every ETS4+ product file on download.se.com.

Listing: www.se.com advertises one download sitemap per country site in its robots.txt
(https://www.se.com/<cc>/<lang>/download/sitemapindex/). Those sitemaps list every public document reference.
The script reads all of them, keeps the references that can be KNX product data (ETS or KNX in the name), reads
each candidate's document page (the page embeds the document's type, file name and product references), and
downloads the files typed "Software - Data Library" (or "Software - Release", except the eConfigure ETS app)
from https://download.se.com/files?p_Doc_Ref=<reference>. Each download is read in memory with
knxindex.entries() and dropped. Entries made by an older knxindex.py are discarded (checked by its sha256).

www.se.com answers "Access Denied" to clients that claim to be a browser but are not one; the sitemaps and
document pages are served to a client that names itself honestly, so those requests use BOT_AGENT.

    python3 -I -B schneider.py [--state FILE] [--out FILE] [--no-download]

Resumable: the state file keeps the sitemap listing, the document metadata and the entries of every processed
download, so a rerun only asks for what is missing. Delete it to start over.
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
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

SCRATCH = knxindex.OUT_DIR
BOT_AGENT = "Kestrel-product-index/1.0 (open-source KNX configuration tool; reads public sitemaps)"
ROBOTS = "https://www.se.com/robots.txt"
DOWNLOAD = "https://download.se.com/files?p_Doc_Ref=%s"
MIN_INTERVAL = 1.1  # seconds between two requests

# Document references that may hold KNX product data. Checked against the document page afterwards.
CANDIDATE = re.compile(r"ets|knx", re.I)
# Not product data whatever the page says: ETS3 / VD formats, firmware, certificates, tools, ETS apps.
# eConfigure KNX is an ETS app (80-380 MB releases), not product data.
NOT_PRODUCT = re.compile(r"ets3|_vd\d|firmw|dfu|pkic(er|rl)|_app$|plug-?in|manual|brochure|flyer|ausschreiben|"
                         r"econfigure", re.I)
# Preferred country sites for the human page (English first).
PAGE_SITES = ["ww/en", "uk/en", "ie/en", "de/de", "ch/de", "at/de", "fr/fr", "nl/nl", "be/en", "ae/en", "sg/en",
              "au/en", "in/en"]

_last = [0.0]


def get(url, bot=False, gz=False):
    """knxindex.fetch with at most one request per MIN_INTERVAL seconds."""
    wait = _last[0] + MIN_INTERVAL - time.time()
    if wait > 0:
        time.sleep(wait)
    headers = {}
    if bot:
        headers["User-Agent"] = BOT_AGENT
    if gz:
        headers["Accept-Encoding"] = "gzip"
    try:
        body, final, response_headers = knxindex.fetch(url, headers=headers, delay=2.0)
    finally:
        _last[0] = time.time()
    if body[:2] == b"\x1f\x8b":
        body = gzip.decompress(body)
    return body, final, response_headers


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


# --- listing -------------------------------------------------------------------------------------------------

def sitemap_indexes():
    body, _, _ = get(ROBOTS, bot=True)
    return re.findall(r"Sitemap:\s*(https://www\.se\.com/[^/\s]+/[^/\s]+/download/sitemapindex/)", body.decode())


def list_site(index_url):
    """{reference: page url} of every document in one country site's download sitemaps."""
    body, _, _ = get(index_url, bot=True, gz=True)
    found = {}
    for sitemap in re.findall(r"<loc>([^<]+)</loc>", body.decode("utf-8", "replace")):
        data, _, _ = get(sitemap, bot=True, gz=True)
        for page in re.findall(r"<loc>([^<]+/download/document/([^<]+?)/?)</loc>", data.decode("utf-8", "replace")):
            found[page[1]] = page[0]
    return found


# --- document pages ------------------------------------------------------------------------------------------

def _field(text, name):
    match = re.search(r"\b%s:\"((?:[^\"\\]|\\.)*)\"" % name, text)
    return match.group(1) if match else ""


def _list(text, name):
    match = re.search(r"\b%s:\[([^\]]*)\]" % name, text)
    return re.findall(r"\"((?:[^\"\\]|\\.)*)\"", match.group(1)) if match else []


def document(page):
    """Metadata of one document from its page (documentDetailsData)."""
    body, _, _ = get(page, bot=True, gz=True)
    text = body.decode("utf-8", "replace")
    start = text.find("documentDetailsData:{")
    if start < 0:
        return {"missing": True}
    text = text[start:start + 20000]
    return {
        "type": _field(text, "enDocType"),
        "subTypes": _list(text, "subTypes"),
        "fileName": _field(text, "fileName"),
        "extension": _field(text, "extension"),
        "size": _field(text, "size"),
        "products": _list(text, "productReferences"),
        "ranges": re.findall(r"\{id:\d+,label:\"([^\"]*)\"", text.split("parentDocuments", 1)[0]),
        "files": len(re.findall(r"downloadFileURL:", text)),
    }


def wanted(meta):
    """Why a document is skipped, or '' when it should be downloaded."""
    if meta.get("missing"):
        return "no document page"
    if meta["type"] not in ("Software - Data Library", "Software - Release"):
        return "type " + (meta["type"] or "?")
    name = meta["fileName"].lower()
    if NOT_PRODUCT.search(name):
        return "not an ETS4+ product file (%s)" % meta["fileName"]
    if not re.search(r"\.(zip|knxprod)$", name):
        return "file " + meta["fileName"]
    return ""


FIRMWARE_PARTS = re.compile(r"\.(bin|hex|fw|dfu|exe|msi|img|srec|s19)$", re.I)


def mostly_firmware(data):
    """True for a zip whose bulk is firmware or installers rather than product data."""
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        return False
    if any(re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in archive.namelist()):
        return False
    firmware = sum(i.file_size for i in archive.infolist() if FIRMWARE_PARTS.search(i.filename))
    return firmware > sum(i.file_size for i in archive.infolist()) / 2


def brand(reference, order):
    """Older Merten-numbered products keep the Merten name."""
    if re.match(r"^MTN\d{6}", order) or re.match(r"^MTN\d{6}", reference):
        return "Merten (Schneider Electric)"
    if re.search(r"-0\d_ETS4_20(11|12)_\d+_ALL_8$", reference) and order.upper().startswith("MTN"):
        return "Merten (Schneider Electric)"
    return "Schneider Electric"


def site_rank(page):
    """Sort key of a document page: English and large sites first."""
    for rank, site in enumerate(PAGE_SITES):
        if "/%s/" % site in page:
            return (rank, page)
    return (len(PAGE_SITES), page)


def page_for(pages):
    return min(pages, key=site_rank)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--state", default=os.path.join(SCRATCH, "state-schneider.json"))
    parser.add_argument("--out", default=os.path.join(SCRATCH, "schneider.json"))
    parser.add_argument("--no-download", action="store_true", help="stop after reading the document pages")
    args = parser.parse_args()
    os.makedirs(os.path.dirname(args.state), exist_ok=True)
    state = load(args.state)
    state.setdefault("sites", {})
    state.setdefault("docs", {})
    state.setdefault("files", {})
    # Entries made by another version of knxindex.py are discarded (their files are read again).
    with open(knxindex.__file__, "rb") as handle:
        module = hashlib.sha256(handle.read()).hexdigest()
    if state.get("module") != module:
        state["files"] = {}
        state["module"] = module

    # 1. Every country site's sitemap → candidate references.
    if not state.get("indexes"):
        state["indexes"] = sitemap_indexes()
        save(args.state, state)
    for index in state["indexes"]:
        if index in state["sites"]:
            continue
        try:
            refs = list_site(index)
        except (urllib.error.URLError, IOError) as error:
            print("sitemap %s: %s" % (index, error), file=sys.stderr)
            continue
        state["sites"][index] = {ref: page for ref, page in refs.items() if CANDIDATE.search(ref)}
        print("%s: %d documents, %d candidates" % (index, len(refs), len(state["sites"][index])), flush=True)
        save(args.state, state)

    candidates = {}
    for refs in state["sites"].values():
        for ref, page in refs.items():
            candidates.setdefault(ref, set()).add(page)
    print("%d candidate references" % len(candidates), flush=True)

    # 2. Document pages → type and file. A page that fails is tried on up to two other country sites.
    for ref in sorted(candidates):
        if NOT_PRODUCT.search(ref):
            continue
        meta = state["docs"].get(ref)
        tried = (meta.get("tried") or [meta["page"]]) if meta else []
        while (meta is None or meta.get("missing")) and len(tried) < 3:
            pages = sorted((p for p in candidates[ref] if p not in tried), key=site_rank)
            if not pages:
                break
            page = pages[0]
            try:
                meta = document(page)
            except urllib.error.HTTPError as error:
                meta = {"missing": True, "error": error.code}
            except (urllib.error.URLError, IOError) as error:
                print("page %s: %s" % (page, error), file=sys.stderr)
                break
            tried.append(page)
            meta["page"] = page
            meta["tried"] = tried
            state["docs"][ref] = meta
            print("doc %s: %s %s" % (ref, meta.get("type"), meta.get("fileName")), flush=True)
            save(args.state, state)

    if args.no_download:
        return

    # 3. Downloads → entries.
    for ref in sorted(state["docs"]):
        meta = state["docs"][ref]
        url = DOWNLOAD % ref
        if url in state["files"]:
            continue
        reason = wanted(meta)
        if reason:
            state["files"][url] = {"skip": reason}
            continue
        try:
            data, _, headers = get(url)
        except urllib.error.HTTPError as error:
            state["files"][url] = {"skip": "HTTP %d" % error.code}
            save(args.state, state)
            continue
        except (urllib.error.URLError, IOError) as error:
            print("download %s: %s" % (url, error), file=sys.stderr)
            continue
        if mostly_firmware(data):
            state["files"][url] = {"skip": "mostly firmware", "size": len(data)}
            save(args.state, state)
            continue
        found = []
        for entry in knxindex.entries(data, url, "", page=meta["page"], name=meta["fileName"] or ref):
            entry["manufacturer"] = brand(ref, entry["orderNumber"])
            found.append(entry)
        del data
        state["files"][url] = {"entries": found} if found else {"skip": "no ETS4+ products in file"}
        print("file %s: %d products" % (ref, len(found)), flush=True)
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
