#!/usr/bin/env python3
"""Make the Windows and macOS application icons from the Kestrel logo.

    python3 -I tools/icons/make_app_icons.py

Renders src/shell/logo/kestrel.svg with Inkscape and writes apps/kestrel/kestrel.ico (ImageMagick) and
apps/kestrel/kestrel.icns (written here: PNG chunks). Both are committed, so building Kestrel needs neither tool;
run this again after changing the logo.
"""

import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
LOGO = os.path.join(ROOT, "src", "shell", "logo", "kestrel.svg")
APP = os.path.join(ROOT, "apps", "kestrel")

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
# ICNS chunk types holding PNG data, by pixel size (several types share a size: 1x and 2x slots).
ICNS_TYPES = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
              (b"ic10", 1024), (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512)]


def render(size, folder):
    path = os.path.join(folder, "kestrel-%d.png" % size)
    if not os.path.exists(path):
        subprocess.run(["inkscape", LOGO, "-w", str(size), "-h", str(size), "-o", path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path


def write_icns(path, folder):
    chunks = b""
    for kind, size in ICNS_TYPES:
        with open(render(size, folder), "rb") as handle:
            data = handle.read()
        chunks += kind + struct.pack(">I", 8 + len(data)) + data
    with open(path, "wb") as handle:
        handle.write(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)


def main():
    with tempfile.TemporaryDirectory() as folder:
        pngs = [render(size, folder) for size in ICO_SIZES]
        subprocess.run(["magick"] + pngs + [os.path.join(APP, "kestrel.ico")], check=True)
        write_icns(os.path.join(APP, "kestrel.icns"), folder)
    print("wrote apps/kestrel/kestrel.ico and apps/kestrel/kestrel.icns")
    return 0


if __name__ == "__main__":
    sys.exit(main())
