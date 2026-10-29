#!/usr/bin/env python3
"""Jung product index: every KNX product file (ETS4+ .knxprod, possibly zipped) Jung publishes.

    python3 -I jung.py <out-dir>

Writes <out-dir>/jung.json ({"version": 1, "products": [...]}) and keeps <out-dir>/state-jung.json so that a rerun
continues where it stopped (delete it to start over).

Enumeration: the download centre lists every KNX article in the "knx" product-download category
(https://www.jung-group.com/en-DE/downloads/product-downloads/knx?p=N, 10 articles a page, plus "knx-qsg"). Each
article has a download page (/en-DE/downloads/product/<article>) whose table names every file; the KNX product
files are the rows titled "Database ETS…" of type KNXPROD or ZIP (VD2–VD5 rows are the pre-ETS4 formats; rows
without a type, whose file exists only in another language, are tried unless their title says ETS2/ETS3). Their
links go through the stable redirector https://application.jung.de/downloads/download/<id>/<language>, which
301-redirects to the current dated file on downloads.jung.de; the index keeps the redirector. Every file is
downloaded once (many articles, e.g. colour variants, share one). Last, the old complete database (article
KNX 2900, "Database ETS5") is read for products that no per-article file covers.

Jung answers HTTP 429 when asked too fast: the script waits DELAY seconds between requests and slows down for
good after a 429.
"""

import html
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knxindex  # noqa: E402

BRAND = "Jung"
SITE = "https://www.jung-group.com/en-DE"
CATEGORIES = ["knx", "knx-qsg"]
LISTING = SITE + "/downloads/product-downloads/%s?p=%d"
PRODUCT = SITE + "/downloads/product/%s"
COMPLETE = "KNX2900"            # article of the complete product database (fallback only)
REDIRECTOR = re.compile(r"https://application\.jung\.de/downloads/download/(\d+)/([\w-]+)")
DELAY = 3.0


class Client:
    """Polite GET: DELAY between requests; every 429 makes the delay 1.5× longer for the rest of the run."""

    def __init__(self, delay):
        self.delay = delay
        self.last = 0.0
        self.requests = 0

    def get(self, url):
        for attempt in range(6):
            wait = self.last + self.delay - time.time()
            if wait > 0:
                time.sleep(wait)
            try:
                return knxindex.fetch(url, retries=0)
            except urllib.error.HTTPError as error:
                if error.code not in (429, 500, 502, 503, 504) or attempt == 5:
                    raise
                if error.code == 429:
                    self.delay = min(self.delay * 1.5, 30.0)
                    retry_after = error.headers.get("Retry-After", "")
                    pause = int(retry_after) if retry_after.isdigit() else 60 * (attempt + 1)
                    print("  429: waiting %d s, delay now %.1f s" % (pause, self.delay), flush=True)
                    time.sleep(pause)
                else:
                    time.sleep(10 * (attempt + 1))
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                if attempt == 5:
                    raise
                time.sleep(10 * (attempt + 1))
            finally:
                self.last = time.time()
                self.requests += 1
        raise IOError("unreachable")


def clean(page_html):
    return re.sub(r"<svg.*?</svg>|<\?xml[^>]*>|<script.*?</script>|<style.*?</style>", "", page_html, flags=re.S)


def text_of(fragment):
    return " ".join(html.unescape(re.sub(r"<[^>]+>", " ", fragment)).split())


def listing_page(page_html):
    """(article slugs, last page number) of one category listing page."""
    page_html = clean(page_html)
    slugs = re.findall(r'href="https://www\.jung-group\.com/en-DE/downloads/product/([^"#?]+)', page_html)
    pages = [int(p) for p in re.findall(r'data-page="(\d+)"', page_html)]
    return list(dict.fromkeys(urllib.parse.unquote(s) for s in slugs)), max(pages or [1])


def databases(page_html):
    """[(download id, language, title, file type)] of the "Database ETS…" rows of an article's download page."""
    page_html = clean(page_html)
    out = {}
    blocks = re.split(r'(?=<div class="download-item" data-order-index=)', page_html)[1:]
    for block in blocks:
        title = re.search(r'download-item__title">(.*?)</div>', block, flags=re.S)
        kind = re.search(r'download-item__file-type">(.*?)</div>', block, flags=re.S)
        title = text_of(title.group(1)) if title else ""
        kind = text_of(kind.group(1)).upper() if kind else ""
        if not title.lower().startswith("database"):
            continue
        links = REDIRECTOR.findall(block)
        if not links:
            continue
        ids = list(dict.fromkeys(i for i, _ in links))
        for download_id in ids:
            languages = [lang for i, lang in links if i == download_id]
            language = "en" if "en" in languages else languages[0]
            if download_id not in out or title:
                out[download_id] = (download_id, language, title, kind)
    return list(out.values())


def fold(text):
    return "".join(c for c in text.lower() if c.isalnum())


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
    state_path = os.path.join(out_dir, "state-jung.json")
    out_path = os.path.join(out_dir, "jung.json")
    state = {}
    if os.path.exists(state_path):
        with open(state_path, encoding="utf-8") as handle:
            state = json.load(handle)
    listings = state.setdefault("listings", {})     # "category p" → [slugs]
    articles = state.setdefault("articles", {})     # slug → [[id, language, title, type]]
    files = state.setdefault("files", {})           # redirector url → {status, entries, …}

    def log(message):
        print(message, flush=True)

    client = Client(DELAY)

    # 1. Article lists.
    # The last page number comes from page 1's pagination (pages past the end still show "next" links); a page
    # without articles also ends the category.
    for category in CATEGORIES:
        page, last = 1, 1
        while page <= last:
            key = "%s %d" % (category, page)
            if key not in listings or page == 1:
                body = client.get(LISTING % (category, page))[0].decode("utf-8", "replace")
                listings[key], last_seen = listing_page(body)
                if page == 1:
                    state.setdefault("last_page", {})[category] = last_seen
                save(state_path, state)
            if not listings[key]:
                break
            last = state["last_page"][category]
            page += 1
        log("%s: %d pages" % (category, last))
    slugs = list(dict.fromkeys(s for key in sorted(listings, key=lambda k: (k.split()[0], int(k.split()[1])))
                               for s in listings[key]))
    log("%d articles" % len(slugs))

    # 2. Article download pages.
    for number, slug in enumerate(slugs + [COMPLETE], 1):
        if slug in articles:
            continue
        try:
            body = client.get(PRODUCT % urllib.parse.quote(slug))[0].decode("utf-8", "replace")
        except urllib.error.HTTPError as error:
            log("[%d] %s: HTTP %d" % (number, slug, error.code))
            if error.code == 404:
                articles[slug] = []
            continue
        articles[slug] = [list(row) for row in databases(body)]
        if articles[slug] or number % 50 == 0:
            log("[%d/%d] %s: %s" % (number, len(slugs) + 1, slug,
                                   ", ".join("%s %s (%s)" % (r[0], r[3], r[2]) for r in articles[slug]) or "-"))
        save(state_path, state)

    # 3. Product files, each once.
    pages = {}          # redirector url → [article slugs]
    meta = {}
    for slug, rows in articles.items():
        for download_id, language, title, kind in rows:
            url = "https://application.jung.de/downloads/download/%s/%s" % (download_id, language)
            pages.setdefault(url, []).append(slug)
            meta[url] = (title, kind)
    for number, url in enumerate(sorted(pages), 1):
        if url in files:
            continue
        title, kind = meta[url]
        # No type is shown when the file exists only in another language: try it, unless the title names an ETS2/3
        # database (those rows are always VD2–VD5 where the type is shown).
        if kind not in ("KNXPROD", "ZIP", "") or not kind and re.search(r"ETS ?[23]", title):
            files[url] = {"status": "skipped", "reason": "%s (%s)" % (kind or "no type", title), "entries": []}
            save(state_path, state)
            continue
        if pages[url] == [COMPLETE] and re.search(r"ETS ?[34]", title):
            files[url] = {"status": "skipped", "reason": "older complete database (%s)" % title, "entries": []}
            save(state_path, state)
            continue
        try:
            download, final_url, _ = client.get(url)
        except Exception as error:   # keep going; a rerun retries the failures
            log("[%d/%d] %s: ERROR %s" % (number, len(pages), url, error))
            continue
        name = urllib.parse.unquote(final_url.rsplit("/", 1)[-1])
        entries = knxindex.entries(download, url, BRAND, "", name)
        for entry in entries:
            order = fold(entry["orderNumber"])
            slugs_here = pages[url]
            match = [s for s in slugs_here if fold(s) == order] or \
                    [s for s in slugs_here if fold(s).endswith(order) or order.endswith(fold(s))] or slugs_here
            entry["page"] = PRODUCT % urllib.parse.quote(match[0])
        files[url] = {"status": "ok" if entries else "no products", "size": len(download), "file": name,
                      "articles": pages[url], "entries": entries}
        log("[%d/%d] %s → %s: %d bytes, %d products" % (number, len(pages), url, name, len(download), len(entries)))
        save(state_path, state)

    # 4. Index: per-article files first; the complete database only for products none of them covers.
    complete = {url for url, slugs_here in pages.items() if slugs_here == [COMPLETE]}
    products = knxindex.dedupe([e for url, item in files.items() if url not in complete for e in item["entries"]])
    covered = {(p["manufacturerId"], fold(p["orderNumber"])) for p in products}
    extra = [e for url in complete for e in files.get(url, {}).get("entries", [])
             if (e["manufacturerId"], fold(e["orderNumber"])) not in covered]
    products = knxindex.dedupe(products + extra)
    products.sort(key=lambda p: (p["category"], p["product"].lower(), p["orderNumber"]))
    save(out_path, {"version": 1, "products": products})
    used = len({p["url"] for p in products})
    log("%d products (%d only in the complete database) from %d files → %s (%d requests)"
        % (len(products), len(knxindex.dedupe(extra)), used, out_path, client.requests))
    missing = [u for u in pages if u not in files] + [s for s in slugs if s not in articles]
    if missing:
        log("%d downloads or pages failed; run again to retry" % len(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
