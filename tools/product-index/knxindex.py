#!/usr/bin/env python3
"""Shared helpers for the product index generators (specs/07-catalog.md §6).

Each brand script lists the manufacturer's KNX product files, downloads each one once with fetch(), reads its
products with products_in() and writes a JSON list of index entries. The files themselves are never kept
(ADR-009). `python3 -I knxindex.py merge <out.json> <brand.json>...` builds src/catalog/data/product-index.json.

Standard library only. Run downloaded data through these functions, never execute it.
"""

import datetime
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

USER_AGENT = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36 "
              "Kestrel-product-index")
MAX_DOWNLOAD = 700 << 20
TODAY = datetime.date.today().isoformat()
# Where the brand scripts keep their state and results (KESTREL_INDEX_OUT, else a folder in the temp dir).
OUT_DIR = os.environ.get("KESTREL_INDEX_OUT") or os.path.join(tempfile.gettempdir(), "kestrel-index")


def fetch(url, data=None, headers=None, retries=4, delay=1.0, timeout=180):
    """GET (or POST with `data`) → (bytes, final_url, response headers). Retries 429/5xx with backoff."""
    request_headers = {"User-Agent": USER_AGENT, "Accept": "*/*"}
    request_headers.update(headers or {})
    for attempt in range(retries + 1):
        try:
            request = urllib.request.Request(url, data=data, headers=request_headers)
            with urllib.request.urlopen(request, timeout=timeout) as response:
                body = response.read(MAX_DOWNLOAD + 1)
                if len(body) > MAX_DOWNLOAD:
                    raise IOError("download larger than %d MB: %s" % (MAX_DOWNLOAD >> 20, url))
                return body, response.geturl(), dict(response.headers)
        except urllib.error.HTTPError as error:
            if error.code in (429, 500, 502, 503, 504) and attempt < retries:
                time.sleep(delay * (2 ** attempt) * (5 if error.code == 429 else 1))
                continue
            raise
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            if attempt < retries:
                time.sleep(delay * (2 ** attempt))
                continue
            raise
    raise IOError("unreachable")


# --- product files -------------------------------------------------------------------------------------------

def _is_zip(data):
    return data[:4] in (b"PK\x03\x04", b"PK\x05\x06")


def _local(tag):
    return tag.rsplit("}", 1)[-1]


def _knxprods(data, name, depth=0):
    """(name, bytes) of every .knxprod in a download: itself, the .knxprod entries of a zip, nested zips."""
    if not _is_zip(data):
        return []
    archive = zipfile.ZipFile(io.BytesIO(data))
    names = archive.namelist()
    if any(n == "knx_master.xml" or re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in names):
        return [(name, data)]
    found = []
    for entry in names:
        lower = entry.lower()
        if lower.endswith(".knxprod"):
            found.append((entry.rsplit("/", 1)[-1], archive.read(entry)))
        elif lower.endswith(".zip") and depth < 2:
            found += _knxprods(archive.read(entry), entry.rsplit("/", 1)[-1], depth + 1)
    return found


def _translations(root, language="en-US"):
    """{(element id, attribute): text} of one language's translations in a manufacturer XML file."""
    out = {}
    for lang in root.iter():
        if _local(lang.tag) != "Language" or lang.get("Identifier") != language:
            continue
        for element in lang.iter():
            if _local(element.tag) != "TranslationElement":
                continue
            ref = element.get("RefId")
            for translation in element:
                if _local(translation.tag) == "Translation":
                    out[(ref, translation.get("AttributeName"))] = translation.get("Text")
    return out


CATEGORIES = [
    ("ip interface", r"ip[ -]?router|ip[ -]?interface|ip[ -]?schnittstelle|knxnet|ip[ -]?gateway|\bip\b.*(interface|router)"),
    ("dimming actuator", r"dimm|dimmer|variateur|regulador"),
    ("blind actuator", r"jalousie|blind|shutter|rollladen|rolladen|volet|store|persiana|louvre|sunblind"),
    ("heating actuator", r"heizungs|heating actuator|valve actuator|stellantrieb|ventil|fan coil|fancoil"),
    ("switch actuator", r"schaltaktor|switch(ing)? actuator|actionneur|switch/blind|schalt-|actuator.*switch|aktor"),
    ("binary input", r"binary input|binäreingang|binaereingang|tasterschnittstelle|push[- ]button interface|universal interface|universal-schnittstelle|entrée|entrada binaria|input"),
    ("presence detector", r"präsenz|presence|motion|bewegung|detector|melder|détecteur|detector"),
    ("room controller", r"thermostat|room controller|raumtemperatur|room temperature|\brtr\b|raumregler|climate|climat"),
    ("push button", r"push[- ]?button|tastsensor|taster|pushbutton|touch|bouton|pulsador|glass|switch(es)? sensor|wippe"),
    ("power supply", r"spannungsversorgung|power supply|netzteil|alimentation"),
    ("display", r"display|panel|bedienzentrale|visu"),
    ("gateway", r"gateway|dali|modbus|bacnet|enocean|coupler|koppler|line coupler"),
    ("sensor", r"sensor|wetter|weather|lux|co2|humidity|feuchte"),
]


def categorize(*texts):
    text = " ".join(t for t in texts if t).lower()
    for category, pattern in CATEGORIES:
        if re.search(pattern, text):
            return category
    return "other"


def _application_header(archive, path):
    """Attributes of <ApplicationProgram> in an application file, without parsing the (large) file."""
    with archive.open(path) as handle:
        head = handle.read(262144).decode("utf-8", "replace")
    match = re.search(r"<ApplicationProgram\s([^>]*)>", head)
    if not match:
        return {}
    return dict(re.findall(r'(\w+)="([^"]*)"', match.group(1)))


def _knxprod_products(data, file_name):
    archive = zipfile.ZipFile(io.BytesIO(data))
    names = archive.namelist()
    hardware_files = [n for n in names if re.match(r"^M-[0-9A-Fa-f]{4}/Hardware\.xml$", n)]
    catalog_files = [n for n in names if re.match(r"^M-[0-9A-Fa-f]{4}/Catalog\.xml$", n)]
    applications = {}
    for n in names:
        match = re.match(r"^(M-[0-9A-Fa-f]{4})/(M-[0-9A-Fa-f]{4}_A-[^/]+)\.xml$", n)
        if match:
            applications[match.group(2)] = _application_header(archive, n)

    # Catalog sections of each product (for the kind).
    sections = {}
    for path in catalog_files:
        root = ET.fromstring(archive.read(path))
        texts = _translations(root)

        def walk(element, trail):
            for child in element:
                tag = _local(child.tag)
                if tag == "CatalogSection":
                    name = texts.get((child.get("Id"), "Name")) or child.get("Name") or ""
                    walk(child, trail + [name])
                elif tag == "CatalogItem":
                    sections.setdefault(child.get("ProductRefId"), " ".join(trail))
                else:
                    walk(child, trail)

        walk(root, [])

    products = []
    for path in hardware_files:
        manufacturer = path.split("/", 1)[0]
        root = ET.fromstring(archive.read(path))
        texts = _translations(root)
        for hardware in root.iter():
            # Only the hardware items: the list that holds them is also called <Hardware> and has no Id.
            if _local(hardware.tag) != "Hardware" or not hardware.get("Id"):
                continue
            programs = []
            for element in hardware.iter():
                if _local(element.tag) == "ApplicationProgramRef":
                    header = applications.get(element.get("RefId"), {})
                    programs.append((int(header.get("ApplicationVersion", "0") or 0), element.get("RefId"), header))
            programs.sort(key=lambda p: p[0])
            program = programs[-1] if programs else (0, "", {})
            for product in hardware.iter():
                if _local(product.tag) != "Product":
                    continue
                pid = product.get("Id")
                text = (texts.get((pid, "Text")) or product.get("Text") or "").strip()
                order = (product.get("OrderNumber") or "").strip()
                if not text or not order:
                    continue
                products.append({
                    "manufacturerId": manufacturer,
                    "orderNumber": order,
                    "product": text,
                    "category": categorize(sections.get(pid, ""), text, hardware.get("Name", "")),
                    "applicationId": program[1] or "",
                    "applicationVersion": program[0],
                    "mask": program[2].get("MaskVersion", ""),
                    "file": file_name,
                })
    return products


def products_in(download, name="download"):
    """Every product of every .knxprod in a download (a .knxprod, a zip, or a zip in a zip)."""
    out = []
    seen = set()
    for knxprod_name, data in _knxprods(download, name):
        try:
            for product in _knxprod_products(data, knxprod_name):
                key = (product["manufacturerId"], product["orderNumber"], product["applicationId"])
                if key not in seen:
                    seen.add(key)
                    out.append(product)
        except (zipfile.BadZipFile, ET.ParseError, KeyError) as error:
            print("  cannot read %s: %s" % (knxprod_name, error), file=sys.stderr)
    return out


def entries(download, url, manufacturer, page="", name="download"):
    """Index entries for the products of one download."""
    digest = hashlib.sha256(download).hexdigest()
    fmt = "zip"
    if _is_zip(download):
        names = zipfile.ZipFile(io.BytesIO(download)).namelist()
        if any(re.match(r"^M-[0-9A-Fa-f]{4}/", n) for n in names):
            fmt = "knxprod"
    out = []
    for product in products_in(download, name):
        out.append({
            "manufacturer": manufacturer,
            "manufacturerId": product["manufacturerId"],
            "orderNumber": product["orderNumber"],
            "product": product["product"],
            "category": product["category"],
            "url": url,
            "page": page,
            "format": fmt,
            "size": len(download),
            "sha256": digest,
            "applicationId": product["applicationId"],
            "mask": product["mask"],
            "verified": TODAY,
            "_applicationVersion": product["applicationVersion"],
        })
    return out


# --- merging -------------------------------------------------------------------------------------------------

FIELDS = ["manufacturer", "manufacturerId", "orderNumber", "product", "category", "url", "page", "format", "size",
          "sha256", "applicationId", "mask", "verified"]


def _fold(text):
    return "".join(c for c in text.lower() if c.isalnum())


def dedupe(products):
    """One entry per (manufacturer id, order number): the newest application wins, then the smaller download."""
    best = {}
    for p in products:
        key = (p["manufacturerId"], _fold(p["orderNumber"]))
        rank = (p.get("_applicationVersion", 0), -p.get("size", 0))
        if key not in best or rank > best[key][0]:
            best[key] = (rank, p)
    return [entry for _, entry in best.values()]


def merge(out_path, inputs):
    products = []
    for path in inputs:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        products += data["products"] if isinstance(data, dict) else data
    products = dedupe(products)
    products.sort(key=lambda p: (p["manufacturer"].lower(), p["category"], p["product"].lower(), p["orderNumber"]))
    # One product per line: an update of the index reads as a diff of products.
    lines = [json.dumps({f: p.get(f, "") for f in FIELDS}, ensure_ascii=False) for p in products]
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write('{"version": 1, "products": [\n' + ",\n".join(lines) + "\n]}\n")
    print("%d products → %s" % (len(products), out_path))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "merge":
        merge(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) == 3 and sys.argv[1] == "inspect":
        with open(sys.argv[2], "rb") as handle:
            for entry in products_in(handle.read(), sys.argv[2].rsplit("/", 1)[-1]):
                print(json.dumps(entry, ensure_ascii=False))
    else:
        print(__doc__)
        print("usage: knxindex.py merge <out.json> <brand.json>... | inspect <download>")
