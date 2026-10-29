# Product index generators

These scripts build `src/catalog/data/product-index.json`, the list of manufacturers' products that Kestrel
can download (specs/07-catalog.md §6). Each brand script lists the brand's published KNX product files, downloads
each file once, reads every product in it and writes index entries. The files are never kept: Kestrel ships the
list, the manufacturer serves the files (ADR-009).

Standard-library Python only. Run them with `python3 -I`.

```sh
cd tools/product-index
export KESTREL_INDEX_OUT=/tmp/kestrel-index   # state and results (the default); downloads stay in memory
python3 -I -B abb.py                           # one script per brand (see below); a rerun continues
python3 -I knxindex.py merge ../../src/catalog/data/product-index.json \
    $(ls $KESTREL_INDEX_OUT/*.json | grep -v /state-)
python3 -I knxindex.py inspect some.knxprod    # what an entry would contain
```

The merge input is every `<brand>.json` (not the `state-*.json` files). Put the previous index last: its entries
only fill in products the scripts no longer find.

| Script | Source of the file list |
|---|---|
| `abb.py`, `busch_jaeger.py` | ABB Library search API |
| `siemens.py` | Siemens KNX technical data site |
| `theben.py` | theben.de product databases |
| `gira.py` | Gira partner download portal |
| `jung.py` | Jung application downloads |
| `mdt.py` | MDT ETS product data page |
| `zennio.py` | zennio.com application programs |
| `schneider.py` | se.com download sitemaps, download.se.com files |
| `hager.py` | hager.com catalogue sitemaps and related-product links (Hager, Berker) |

Hager (about 4 h, 5,500 slow pages) and Jung (about 1 h 40, 1,400 article pages) are the slow ones.

`knxindex.py` holds the shared parts: polite downloads with retries (`fetch`), the products of a `.knxprod`, a zip or
a zip in a zip (`products_in`, `entries`: order number, English product text, newest application, mask, kind,
size, SHA-256), one entry per manufacturer id and order number (`dedupe`), and `merge`.

Manufacturers reorganise their sites: re-run the scripts before a release and check the counts against the last
index. A changed SHA-256 is normal (a new version of the file); Kestrel still downloads it and says so.

Known limits:
- Only ETS4+ product files are indexed; VD1–VD5 files and ETS2/3 bundles are skipped.
- Files behind a click-through licence (some Siemens files) are left out, and so are downloads that need a login.
- Products found only in a complete database (Jung's KNX 2900, Theben's, MDT's) point to that large file.
- Hager's sitemaps miss products; the script follows related-product links, but a product on one country site
  only can still be missing.
- The brand of an entry follows the manufacturer id of its file (an ABB-made Busch-Jaeger device is "ABB").
