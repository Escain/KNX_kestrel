#!/usr/bin/env python3
"""Generates the synthetic ZIP fixtures of tests/unit/archive and the M-FFFF .knxprod of tests/unit/catalog.

Deterministic: fixed timestamps and attributes, sorted inputs. The outputs are committed; rerun after editing
this script or tests/data/catalog/M-FFFF/*.xml:

    python3 tests/data/archive/make_fixtures.py

Normal archives are written with Python's `zipfile` (a cross-check of our reader against an independent
writer, ADR-004). Malformed archives come from the small raw writer below, which writes headers field by field.
Everything here is synthetic (ADR-009): fake manufacturer M-FFFF, no vendor data.
"""

import io
import pathlib
import struct
import zipfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
DATA = HERE.parent
DATE_TIME = (1980, 1, 1, 0, 0, 0)

LOREM = b"".join(b"Kestrel line %03d: the quick brown fox jumps over the lazy dog.\n" % i for i in range(200))


def info(name, compress=zipfile.ZIP_DEFLATED):
    zi = zipfile.ZipInfo(name, date_time=DATE_TIME)
    zi.compress_type = compress
    zi.create_system = 3  # Unix, so the bytes do not depend on the host OS
    zi.external_attr = (0o40755 << 16) | 0x10 if name.endswith("/") else (0o100644 << 16)
    return zi


def zipfile_archive(members, comment=b"", streamed=False):
    """members: list of (name, bytes, compress_type)."""

    class Unseekable(io.RawIOBase):
        def __init__(self):
            self.buffer = bytearray()

        def writable(self):
            return True

        def write(self, b):
            self.buffer += b
            return len(b)

    if streamed:
        sink = Unseekable()
        with zipfile.ZipFile(sink, "w") as zf:
            for name, data, compress in members:
                with zf.open(info(name, compress), "w") as f:
                    f.write(data)
            zf.comment = comment
        return bytes(sink.buffer)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as zf:
        for name, data, compress in members:
            zf.writestr(info(name, compress), data)
        zf.comment = comment
    return buffer.getvalue()


def raw_deflate(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def raw_archive(entries, comment=b"", zip64=False):
    """entries: dicts with name (bytes), data (stored payload), method, flags, crc, csize, usize,
    local_extra, central_extra. Writes exactly what it is told, so fields can lie."""
    out = bytearray()
    central = bytearray()
    for e in entries:
        offset = len(out)
        name = e["name"]
        crc = e.get("crc", zlib.crc32(e.get("plain", e["data"])))
        csize = e.get("csize", len(e["data"]))
        usize = e.get("usize", len(e.get("plain", e["data"])))
        method = e.get("method", 0)
        flags = e.get("flags", 0)
        local_extra = e.get("local_extra", b"")
        central_extra = e.get("central_extra", b"")
        hdr_csize, hdr_usize, hdr_offset = csize, usize, offset
        if zip64:
            local_extra = struct.pack("<HHQQ", 1, 16, usize, csize) + local_extra
            central_extra = struct.pack("<HHQQQ", 1, 24, usize, csize, offset) + central_extra
            hdr_csize = hdr_usize = hdr_offset = 0xFFFFFFFF
        out += struct.pack("<IHHHHHIIIHH", 0x04034B50, 45 if zip64 else 20, flags, method, 0, 0x21, crc,
                           hdr_csize, hdr_usize, len(name), len(local_extra))
        out += name + local_extra + e["data"]
        central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 0x031E, 45 if zip64 else 20, flags, method, 0,
                               0x21, crc, hdr_csize, hdr_usize, len(name), len(central_extra), 0, 0, 0,
                               0o100644 << 16, hdr_offset)
        central += name + central_extra
    cd_offset = len(out)
    out += central
    count = len(entries)
    if zip64:
        eocd64 = len(out)
        out += struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, count, count, len(central), cd_offset)
        out += struct.pack("<IIQI", 0x07064B50, 0, eocd64, 1)
        out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, len(comment))
    else:
        out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, count, count, len(central), cd_offset, len(comment))
    return bytes(out + comment)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"wrote {path.relative_to(DATA.parent.parent)} ({len(data)} bytes)")


def archive_fixtures():
    out = HERE
    basic_members = [
        ("hello.txt", b"Hello, Kestrel!\n", zipfile.ZIP_STORED),
        ("dir/", b"", zipfile.ZIP_STORED),
        ("dir/lorem.txt", LOREM, zipfile.ZIP_DEFLATED),
        ("empty.txt", b"", zipfile.ZIP_DEFLATED),
        ("dir/grüße.txt", "Grüße aus M-FFFF\n".encode(), zipfile.ZIP_DEFLATED),
    ]
    basic = zipfile_archive(basic_members, comment=b"Kestrel test archive")
    write(out / "basic.zip", basic)
    write(out / "streamed.zip", zipfile_archive(basic_members[:3], streamed=True))
    write(out / "truncated.zip", basic[: len(basic) * 6 // 10])

    # Corrupt CRC: flip one payload byte of the stored "hello.txt" (headers stay consistent).
    bad = bytearray(basic)
    payload = bad.index(b"Hello, Kestrel!")
    bad[payload] ^= 0x20
    write(out / "bad-crc.zip", bytes(bad))

    hello = b"secret\n"
    write(out / "encrypted.zip", raw_archive([{"name": b"secret.txt", "data": hello, "flags": 0x0001}]))
    aes_extra = struct.pack("<HHHHBH", 0x9901, 7, 2, 0x4541, 3, 8)  # AE-2, "AE", AES-256, real method deflate
    write(out / "aes.zip", raw_archive([{"name": b"secret.txt", "data": hello, "method": 99,
                                         "local_extra": aes_extra, "central_extra": aes_extra}]))
    write(out / "traversal.zip", zipfile_archive([("../evil", b"evil\n", zipfile.ZIP_STORED)]))
    write(out / "absolute.zip", raw_archive([{"name": b"/etc/passwd", "data": b"root\n"}]))
    write(out / "unknown-method.zip", raw_archive([{"name": b"a.lzma", "data": b"\x00" * 8, "method": 14}]))
    # Size lies: deflated LOREM declared smaller / larger than it inflates to (CRC matches the real data).
    deflated = raw_deflate(LOREM)
    write(out / "size-lie-small.zip", raw_archive([{"name": b"lorem.txt", "data": deflated, "plain": LOREM,
                                                    "method": 8, "usize": 1000}]))
    write(out / "size-lie-large.zip", raw_archive([{"name": b"lorem.txt", "data": deflated, "plain": LOREM,
                                                    "method": 8, "usize": len(LOREM) + 1000}]))
    # A small bomb: 4 MiB of zeros deflates to a few KiB.
    zeros = bytes(4 << 20)
    write(out / "bomb.zip", raw_archive([{"name": b"zeros.bin", "data": raw_deflate(zeros), "plain": zeros,
                                          "method": 8}]))
    write(out / "zip64.zip", raw_archive([
        {"name": b"a.txt", "data": b"zip64 stored\n"},
        {"name": b"b.txt", "data": raw_deflate(LOREM), "plain": LOREM, "method": 8},
    ], comment=b"zip64", zip64=True))
    # CP437 name without the UTF-8 flag: 0x82 is 'e acute'.
    write(out / "cp437.zip", raw_archive([{"name": b"caf\x82.txt", "data": b"cp437\n"}]))


def knxprod_fixture():
    source = DATA / "catalog" / "M-FFFF"
    members = [(f"M-FFFF/{p.name}", p.read_bytes(), zipfile.ZIP_DEFLATED) for p in sorted(source.glob("*.xml"))]
    # Stand-in for the master data shipped inside real product files (synthetic: one mask, one procedure).
    master = (DATA / "catalog" / "knx_master.xml").read_bytes()
    members.append(("knx_master.xml", master, zipfile.ZIP_DEFLATED))
    write(DATA / "catalog" / "M-FFFF.knxprod", zipfile_archive(members))


if __name__ == "__main__":
    archive_fixtures()
    knxprod_fixture()
