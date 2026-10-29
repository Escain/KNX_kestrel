#!/usr/bin/env python3
"""Product index entries for every Zennio KNX product file (specs/07-catalog.md §6).

Enumeration, two official listings:

1. Product pages. The WordPress product sitemaps (sitemap_index.xml → producto-sitemap*.xml) list every product
   page; each English page (www.zennio.com/product/<slug>/) links its current application programs as
   https://www.zennio.com/documents/application_program_<…>/, which answers a plain GET with a 302 to
   https://assets.zennio.com/application_program/<File>.zip|.knxprod. That stable documents URL is the index "url"
   and the product page its "page". Per application the ETS5/ETS6 file is taken, the ETS4 one only when there is no
   ETS5/ETS6 file; ETS3 files, firmware and the "…-older-versions" pages are skipped.
2. The application program repository (www.zennio.com/application-program-repository/): one table of every
   application program and version (Product, Code, Version, File name, Link → assets.zennio.com). Per product and
   application code the newest ETS4+ file (.knxprod, or a .zip not labelled ETS3) is read unless step 1 already
   read the same file. This adds discontinued products and products whose page links no application; their "url"
   is the assets URL and their "page" the repository searched for the product.

Each file is downloaded once, kept in memory only, and forgotten. Resumable: the state file keeps the product pages,
the repository table and the entries of every file processed.

usage: python3 -I zennio.py [--state FILE] [--out FILE] [--list-only]
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
SITEMAP = "https://www.zennio.com/sitemap_index.xml"
REPOSITORY = "https://www.zennio.com/application-program-repository/"
DOCUMENTS = "https://www.zennio.com/documents/"
MANUFACTURER = "Zennio"
INTERVAL = 1.1  # seconds between requests

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


def text(fragment):
    return re.sub(r"\s+", " ", htmllib.unescape(re.sub(r"<[^>]+>", " ", fragment))).strip()


# --- 1. product pages ----------------------------------------------------------------------------------------

def product_pages(state, state_path):
    if "sitemap" not in state:
        index = get(SITEMAP)[0].decode("utf-8", "replace")
        pages = []
        for sitemap in re.findall(r"<loc>([^<]*producto-sitemap[^<]*)</loc>", index):
            xml = get(sitemap)[0].decode("utf-8", "replace")
            pages += [u for u in re.findall(r"<loc>([^<]*)</loc>", xml)
                      if re.match(r"^https://www\.zennio\.com/product/[^/]+/?$", u)]
        state["sitemap"] = sorted(set(pages))
        save(state_path, state)
    return state["sitemap"]


LINK = re.compile(r'href="(?:https://www\.zennio\.com)?/documents/(application_program[^"/?#]*)/?"[^>]*>(.*?)</a>',
                  re.S | re.I)


def read_page(url):
    html = get(url)[0].decode("utf-8", "replace")
    links = []
    for slug, label in LINK.findall(html):
        if [slug, text(label)] not in links:
            links.append([slug, text(label)])
    return links


def ets(slug, label):
    """'5' (ETS5/6 or plain .knxprod), '4', '3', or '' when the link does not say."""
    words = (slug + " " + label).lower()
    if re.search(r"ets\s*-?[56]|knxprod", words):
        return "5"
    if re.search(r"ets\s*-?4", words):
        return "4"
    if re.search(r"ets\s*-?3|vd\d", words):
        return "3"
    return ""


def choose(links):
    """The application-program slugs of one product page worth downloading."""
    groups = {}
    for slug, label in links:
        if re.search(r"firmware", slug + label, re.I):
            continue
        kind = ets(slug, label)
        if kind == "3":
            continue
        base = re.split(r"\s+-\s+ETS", label, flags=re.I)[0].strip().lower() or slug
        groups.setdefault(base, []).append((kind, slug))
    chosen = []
    for items in groups.values():
        chosen += [s for k, s in items if k == "5"] or [s for k, s in items if k == ""] or [s for k, s in items]
    return chosen


# --- 2. application program repository -----------------------------------------------------------------------

def repository(state, state_path):
    """[[product, code, version, file name, url]] of every repository row."""
    if "repository" not in state:
        html = get(REPOSITORY)[0].decode("utf-8", "replace")
        rows, product = [], ""
        for row in re.findall(r"<tr data-download=[^>]*>(.*?)</tr>", html, re.S):
            cells = {k: v for k, v in re.findall(r'data-th="([^"]+)"[^>]*>(.*?)</td>', row, re.S)}
            if "Product" in cells:
                product = text(cells["Product"])
            href = re.search(r'href="([^"]+)"', cells.get("Link", ""))
            if href:
                rows.append([product, text(cells.get("Code", "")), text(cells.get("Version", "")),
                             text(cells.get("File name", "")), htmllib.unescape(href.group(1))])
        if len(rows) < 100:
            raise IOError("the repository table looks empty (%d rows): did the page change?" % len(rows))
        state["repository"] = rows
        save(state_path, state)
    return state["repository"]


def _version(version):
    return tuple(int(n) for n in re.findall(r"\d+", version))


def newest_per_application(rows):
    """{url: (product, file name)}: per (product, code) the newest ETS4+ product file, ETS5/6 first."""
    best = {}
    for product, code, version, label, url in rows:
        name = urllib.parse.unquote(url.rsplit("/", 1)[-1]).lower()
        if not name.endswith((".knxprod", ".zip")) or re.search(r"firmware|update|driver", label + name, re.I):
            continue
        if re.search(r"ets\s*3", label, re.I) and not re.search(r"ets\s*[456]", label, re.I):
            continue
        if re.search(r"\bets\s*[56]\b|ets5|ets6", label, re.I) or name.endswith(".knxprod"):
            kind = 2 if not re.search(r"ets\s*4", label, re.I) else 1
        else:
            kind = 0
        rank = (_version(version), kind)
        key = (product, code)
        if key not in best or rank > best[key][0]:
            best[key] = (rank, url, product, label)
    return {url: (product, label) for _, url, product, label in best.values()}


def repository_page(product):
    return REPOSITORY + "?" + urllib.parse.urlencode({"q": product.split(",")[0].strip()})


# --- run -----------------------------------------------------------------------------------------------------

def process(state, state_path, url, page, note):
    done = state["files"].get(url)
    if done and done.get("status") in ("ok", "empty"):
        return done
    try:
        data, final, _ = get(url)
    except Exception as error:  # noqa: BLE001 - one bad file must not stop the run
        print("  %s: %s" % (url, error), file=sys.stderr, flush=True)
        state["files"][url] = {"status": "error", "error": str(error)}
        save(state_path, state)
        return state["files"][url]
    name = urllib.parse.unquote(final.rsplit("/", 1)[-1])
    entries = knxindex.entries(data, url, MANUFACTURER, page, name)
    record = {"status": "ok" if entries else "empty", "final": final, "size": len(data), "entries": entries}
    del data
    state["files"][url] = record
    save(state_path, state)
    print("  %s %s: %d products" % (note, name, len(entries)), flush=True)
    return record


def main(argv):
    state_path = os.path.join(SCRATCH, "state-zennio.json")
    out_path = os.path.join(SCRATCH, "zennio.json")
    if "--state" in argv:
        state_path = argv[argv.index("--state") + 1]
    if "--out" in argv:
        out_path = argv[argv.index("--out") + 1]
    os.makedirs(os.path.dirname(state_path), exist_ok=True)
    state = load(state_path)
    state.setdefault("pages", {})
    state.setdefault("files", {})

    pages = product_pages(state, state_path)
    for i, page in enumerate(pages):
        if page in state["pages"]:
            continue
        try:
            state["pages"][page] = read_page(page)
        except Exception as error:  # noqa: BLE001
            print("  page %s: %s" % (page, error), file=sys.stderr, flush=True)
            continue
        if i % 25 == 0:
            print("  pages %d/%d" % (i + 1, len(pages)), flush=True)
            save(state_path, state)
    save(state_path, state)
    rows = repository(state, state_path)

    wanted = {}  # documents slug → first product page linking it
    for page in pages:
        if page.rstrip("/").endswith("-older-versions"):
            continue
        for slug in choose(state["pages"].get(page, [])):
            wanted.setdefault(slug, page)
    from_repository = newest_per_application(rows)
    print("%d product pages (%d link application programs): %d files; repository: %d rows, %d newest files" % (
        len(pages), sum(1 for v in state["pages"].values() if v), len(wanted), len(rows), len(from_repository)),
        flush=True)
    if "--list-only" in argv:
        return

    used = []  # (url, record) in the index
    finals = set()
    for n, (slug, page) in enumerate(sorted(wanted.items())):
        url = DOCUMENTS + slug + "/"
        record = process(state, state_path, url, page, "[page %d/%d]" % (n + 1, len(wanted)))
        used.append((url, record))
        if record.get("final"):
            finals.add(urllib.parse.unquote(record["final"]))
    for n, (url, (product, label)) in enumerate(sorted(from_repository.items())):
        if urllib.parse.unquote(url) in finals:
            continue
        record = process(state, state_path, url, repository_page(product),
                         "[repository %d/%d]" % (n + 1, len(from_repository)))
        used.append((url, record))

    entries = [p for _, record in used for p in record.get("entries", [])]
    product_page = {p["orderNumber"]: p["page"] for p in entries if "/product/" in p["page"]}
    # A newer version only in the repository wins dedupe(); keep the product page when there is one.
    products = [{**p, "page": product_page.get(p["orderNumber"], p["page"])} for p in knxindex.dedupe(entries)]
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump({"version": 1, "products": products}, handle, indent=1, ensure_ascii=False)
        handle.write("\n")
    records = [r for _, r in used]
    print("%d products (%d with a product page) from %d files read (%d with products, %d without, %d errors) → %s" % (
        len(products), sum(1 for p in products if "/product/" in p["page"]), len(records),
        sum(1 for r in records if r.get("status") == "ok"), sum(1 for r in records if r.get("status") == "empty"),
        sum(1 for r in records if r.get("status") == "error"), out_path))


if __name__ == "__main__":
    main(sys.argv[1:])
