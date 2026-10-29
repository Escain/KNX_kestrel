#!/usr/bin/env python3
"""Gira product index: every KNX product file (ETS4+ .knxprod, usually zipped) on Gira's download portal.

    python3 -I gira.py <out-dir>

Writes <out-dir>/gira.json ({"version": 1, "products": [...]}) and keeps <out-dir>/state-gira.json so that a rerun
continues where it stopped (delete it to start over).

Enumeration: the portal (https://partner.gira.com/en/service/download/index.html) is a tree of categories
(?id=N) holding download cards; a card links either straight to the file (/data3/<name>) or to a detail page
(?id=N&type=D). The script walks the whole "Software" tree (id 262; it contains "KNX product databases" →
"Individual product databases", id 3456, and product folders such as Gira X1 whose database is only there),
opens the detail pages of cards that look like product databases, and adds the hits of the portal search
(modus=suche_result) for a few database terms. Files are kept in memory only; pre-ETS4 formats (VD1–VD5, ETS1–3
bundles), firmware and documents are not downloaded.
"""

import html
import json
import os
import re
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

BRAND = "Gira"
BASE = "https://partner.gira.com"
PORTAL = BASE + "/en/service/download/index.html"
SOFTWARE = 262
# "KNX product databases" folders (individual, overall): every file there is a product database, even when its
# card only says "from version 5.7.7".
DATABASE_FOLDERS = {1044, 2545, 3456}
# Top-level portal sections outside "Software" (documentation, brochures, …) and the root: never walked.
OUTSIDE = {1, 118, 187, 1421, 2632, 5920}
SEARCH_TERMS = ["product database", "Produktdatenbank", "ETS", "knxprod"]
DELAY = 1.0          # seconds between requests
DATABASE = re.compile(r"ETS|database|datenbank|knxprod", re.I)
OLD_FILE = re.compile(r"ets[ _-]?[123]|\.vd\d$", re.I)   # ETS1–3 bundles and VD1–VD5 files
OLD_TEXT = re.compile(r"ETS ?[123]", re.I)
NEW_TEXT = re.compile(r"ETS ?[4-6]", re.I)


class Client:
    def __init__(self, delay):
        self.delay = delay
        self.last = 0.0
        self.requests = 0

    def get(self, url, data=None):
        wait = self.last + self.delay - time.time()
        if wait > 0:
            time.sleep(wait)
        try:
            return knxindex.fetch(url, data=data, delay=max(self.delay, 1.0))[0]
        finally:
            self.last = time.time()
            self.requests += 1


def text_of(fragment):
    fragment = re.sub(r"<svg.*?</svg>", "", fragment, flags=re.S)
    return " ".join(html.unescape(re.sub(r"<[^>]+>", " ", fragment)).split())


def size_of(meta):
    """'ZIP, 4.91 MB' → bytes (0 when unknown)."""
    match = re.search(r"([\d.,]+)\s*(KB|MB|GB)", meta)
    if not match:
        return 0
    number = float(match.group(1).replace(",", ""))
    return int(number * {"KB": 1 << 10, "MB": 1 << 20, "GB": 1 << 30}[match.group(2)])


def absolute(href):
    return urllib.parse.urljoin(PORTAL, html.unescape(href))


def cards(page_html):
    """(href, title, text) of every download card on a portal page."""
    out = []
    for article in re.findall(r'<article class="dl5-card".*?</article>', page_html, flags=re.S):
        heading = re.search(r'<h3><a href="([^"]+)">(.*?)</a>', article, flags=re.S)
        if not heading:
            continue
        paragraphs = [text_of(p) for p in re.findall(r"<p[^>]*>(.*?)</p>", article, flags=re.S)]
        out.append((absolute(heading.group(1)), text_of(heading.group(2)), " | ".join(paragraphs)))
    return out


def categories(page_html):
    """Category ids linked from a portal page (children, breadcrumbs)."""
    ids = set()
    for href in re.findall(r'href="([^"]*service/download/index\.html\?[^"]*)"', page_html):
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(html.unescape(href)).query)
        if set(query) == {"id"}:
            ids.add(int(query["id"][0]))
    return ids


def detail_files(page_html):
    """(file url, text) of the current files on a detail page (the archive section is not loaded)."""
    body = page_html.split('id="dl5-archiv"', 1)[0]
    title = re.search(r"<h1[^>]*>(.*?)</h1>", body, flags=re.S)
    text = text_of(title.group(1)) if title else ""
    main = re.search(r"<main.*?</main>", body, flags=re.S)
    text += " " + text_of(main.group(0) if main else body)[:600]
    return [(absolute(href), text) for href in dict.fromkeys(re.findall(r'href="(/data3/[^"]+)"', body))]


def wanted(url, text, size, page):
    """Why a candidate file is skipped, or None when it should be downloaded."""
    name = urllib.parse.unquote(url.rsplit("/", 1)[-1])
    lower = name.lower()
    if not lower.endswith((".zip", ".knxprod")):
        return "not a zip/knxprod (%s)" % (lower.rsplit(".", 1)[-1] if "." in lower else "?")
    folder = urllib.parse.parse_qs(urllib.parse.urlsplit(page).query).get("id", ["0"])[0]
    if not DATABASE.search(text) and int(folder) not in DATABASE_FOLDERS:
        return "not a product database"
    if OLD_FILE.search(lower):
        return "ETS1-3 bundle (file name)"
    if OLD_TEXT.search(text) and not NEW_TEXT.search(text) and size > (5 << 20):
        return "ETS2/ETS3 product database (description)"
    return None


def enumerate_files(client, log):
    """{file url: {"page": url, "text": str, "size": int}} from the Software tree and the search."""
    files = {}

    def add(url, page, title, text, size):
        if not url.startswith(BASE + "/data3/"):
            return
        entry = files.setdefault(url, {"page": page, "text": "", "size": size})
        entry["text"] = (entry["text"] + " | " + title + " | " + text).strip(" |")
        entry["size"] = entry["size"] or size

    details = {}

    def handle(page_url, page_html):
        for href, title, text in cards(page_html):
            size = size_of(text)
            if "/data3/" in href:
                add(href, page_url, title, text, size)
            elif "type=D" in href and DATABASE.search(title + " " + text):
                details.setdefault(href, (title, text))

    queue, seen = [SOFTWARE], {SOFTWARE}
    while queue:
        category = queue.pop(0)
        url = "%s?id=%d" % (PORTAL, category)
        page = client.get(url).decode("utf-8", "replace")
        handle(url, page)
        for child in sorted(categories(page) - seen - OUTSIDE):
            seen.add(child)
            queue.append(child)
    log("walked %d categories" % len(seen))

    for term in SEARCH_TERMS:
        data = urllib.parse.urlencode({"modus": "suche_result", "swert": term}).encode()
        page = client.get(PORTAL, data=data).decode("utf-8", "replace")
        handle(PORTAL, page)
    log("searched %d terms" % len(SEARCH_TERMS))

    for href, (title, text) in sorted(details.items()):
        page = client.get(href).decode("utf-8", "replace")
        for url, detail_text in detail_files(page):
            add(url, href, title, text + " | " + detail_text, size_of(text))
    log("opened %d detail pages" % len(details))
    return files


def save(path, data):
    temp = path + ".tmp"
    with open(temp, "w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=0)
    os.replace(temp, path)


def main():
    if len(sys.argv) > 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1] if len(sys.argv) == 2 else knxindex.OUT_DIR
    os.makedirs(out_dir, exist_ok=True)
    state_path = os.path.join(out_dir, "state-gira.json")
    out_path = os.path.join(out_dir, "gira.json")
    state = {"files": {}}
    if os.path.exists(state_path):
        with open(state_path, encoding="utf-8") as handle:
            state = json.load(handle)

    def log(message):
        print(message, flush=True)

    client = Client(DELAY)
    if "listing" not in state:
        state["listing"] = enumerate_files(client, log)
        save(state_path, state)
    listing = state["listing"]
    log("%d candidate files" % len(listing))

    done = state["files"]
    for number, (url, info) in enumerate(sorted(listing.items()), 1):
        if url in done and done[url]["status"] != "skipped":   # skips are re-judged on every run
            continue
        reason = wanted(url, info["text"], info["size"], info["page"])
        if reason:
            done[url] = {"status": "skipped", "reason": reason, "entries": []}
            continue
        name = urllib.parse.unquote(url.rsplit("/", 1)[-1])
        try:
            download = client.get(url)
        except Exception as error:   # keep going; a rerun retries the failures
            log("[%d/%d] %s: ERROR %s" % (number, len(listing), name, error))
            continue
        entries = knxindex.entries(download, url, BRAND, info["page"], name)
        done[url] = {"status": "ok" if entries else "no products", "size": len(download), "entries": entries}
        log("[%d/%d] %s: %d bytes, %d products" % (number, len(listing), name, len(download), len(entries)))
        save(state_path, state)
    save(state_path, state)

    products = knxindex.dedupe([e for item in done.values() for e in item["entries"]])
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    save(out_path, {"version": 1, "products": products})
    files = sum(1 for item in done.values() if item["entries"])
    log("%d products from %d files → %s (%d requests)" % (len(products), files, out_path, client.requests))
    missing = [u for u in listing if u not in done]
    if missing:
        log("%d files failed; run again to retry" % len(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
