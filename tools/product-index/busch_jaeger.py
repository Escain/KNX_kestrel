#!/usr/bin/env python3
"""Busch-Jaeger product index: every ETS4+ product file of manufacturer M-0007 in the ABB Library.

Busch-Jaeger's product files are in the ABB Library, next to ABB's, and the library does not tell the brands
apart, so this script lists the same files as abb.py (see there: API searches plus ABB's Software Repository
workbook) and keeps the products whose manufacturer id is M-0007 (Busch-Jaeger Elektro). ABB-branded variants of
Busch-Jaeger devices (order numbers ending in -500) carry ABB's id M-0002 and are in abb.py's output.

Files already read by abb.py are taken from its state file (state-abb.json), so running abb.py first means this
script downloads nothing new; either script can run alone.

usage: python3 -I busch_jaeger.py [--out OUT.json] [--state STATE.json] [--limit N]
"""

import os
import sys

sys.dont_write_bytecode = True  # keep the tools folder clean
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abb  # noqa: E402

if __name__ == "__main__":
    sys.exit(abb.run("Busch-Jaeger"))
