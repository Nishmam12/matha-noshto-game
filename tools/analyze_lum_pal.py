#!/usr/bin/env python3
"""Analyse which palette indices Lumiara ground tiles use exclusively."""

import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake_dungeon as bd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_FILE = os.path.join(ROOT, "src", "art_data.h")


def rle_decode(data, off, length, n):
    out = bytearray()
    i, end = off, off + length
    while i < end:
        b = data[i]
        i += 1
        if b & 0x80:
            take = (b & 0x7F) + 1
            out.extend(data[i:i + take])
            i += take
        else:
            out.extend(bytes([data[i]]) * (b + 1))
            i += 1
    assert len(out) == n and i == end
    return bytes(out)


GROUND_TILES = [
    "LUM_GRASS_A", "LUM_GRASS_B", "LUM_GRASS_C", "LUM_GRASS_D",
    "LUM_COBBLE_A", "LUM_COBBLE_B", "LUM_GRASSEDGE", "LUM_OLIVEEDGE",
    "LUM_WATER_A", "LUM_WATER_B", "LUM_WATER_C", "LUM_WATER_D",
    "LUM_VOID_A", "LUM_VOID_B",
]

with open(OUT_FILE, "r", encoding="utf-8", newline="") as f:
    text = f.read()
parsed = bd.parse_header(text)
by_name = {r["name"]: r for r in parsed["records"]}
pal = parsed["pal"]
data = parsed["data"]

use_ground = Counter()
use_other_lum = Counter()
use_nonlum = Counter()
for r in parsed["records"]:
    px = rle_decode(data, r["off"], r["len"], r["w"] * r["h"])
    c = Counter(px)
    del c[0]
    if r["name"] in GROUND_TILES:
        use_ground.update(c)
    elif r["name"].startswith("LUM_"):
        use_other_lum.update(c)
    else:
        use_nonlum.update(c)

print("ground-tile px total:", sum(use_ground.values()))
print("other-lum px total:", sum(use_other_lum.values()))
print()
print("idx  rgb            ground%%  lumdecor%%  nonlum  tone")
for idx in sorted(set(use_ground) | set(use_other_lum)):
    g = use_ground.get(idx, 0)
    o = use_other_lum.get(idx, 0)
    n = use_nonlum.get(idx, 0)
    tot = g + o + n
    print("%3d  %s  %6.1f%%  %6.1f%%  %6d  %s" % (
        idx, pal[idx - 1],
        100.0 * g / tot, 100.0 * o / tot, n,
        "GROUND-EXCL" if (o == 0 and n == 0) else
        ("lum-shared" if n == 0 else "GLOBAL-SHARED")))
