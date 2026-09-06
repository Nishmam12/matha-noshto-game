#!/usr/bin/env python3
"""
Wayfarer Lumiara/Underworld shrink bake: resample decor to 60% in place,
and soften Lumiara's harsh tone.

  - SHRINKS 45 decoration sprites (15 Lumiara + 30 Underworld) to SCALE 0.6.
  - SOFTENS Lumiara-only palette entries (desat + dim).
  - REMOVES nothing by itself; main.c empties Lumiara reeds separately.

WHY INCREMENTAL: tools/bake.py cannot run in this checkout (the Lumiara
sources it names are not all here, so a full re-bake would DROP shipped
art), and tools/bake_dungeon.py refuses on headers holding DUN_ sprites.
This edits, following tools/bake_dungeon_fix.py.

DISCIPLINE:
  - parser round-trip identity verified before changing anything.
  - NO new palette colours: nearest-neighbour resample IN INDEX SPACE, so the
    result is a strict SUBSET of the indices that shipped. Palette stands at
    253 of 254; nothing to spend.
  - TILES EXCLUDED: 16x16 ground cells are grid-locked (autotile, borders,
    seamless fill). Shrinking them breaks the grid. Portals excluded: gates
    are landmarks. STAG excluded: tile-test porous control.
  - same record order and count, so stored ids (ART_TILE_AT, ART_NPC_BASE)
    cannot move. ART_DATA rebuilt with dedupe; every sprite decoded back and
    compared. Refuses to run twice.

Usage:  python tools/bake_shrink_lum_uw.py
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake_dungeon as bd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_FILE = os.path.join(ROOT, "src", "art_data.h")

SCALE = 0.6

# Soften-only for Lumiara: desaturate toward luminance, then dim.
LUM_TONE_DESAT = 0.65
LUM_TONE_DIM = 0.94

LUM_SHRINK = [
    "LUM_TREE", "LUM_MONOLITH", "LUM_BUSH", "LUM_MUSHROOM",
    "LUM_BENCH", "LUM_ARCHWAY", "LUM_STATUE", "LUM_CHEST",
    "LUM_URN", "LUM_SIGNPOST", "LUM_LANTERN", "LUM_BANNER",
    "LUM_JELLYFISH", "LUM_MANTA", "LUM_FOX",
]

UW_SHRINK = [
    "UW_TREE_1", "UW_TREE_2", "UW_TREE_3",
    "UW_PINE_1", "UW_PINE_2", "UW_PINE_3",
    "UW_BUSH_1", "UW_BUSH_2", "UW_BUSH_3",
    "UW_LOG_1", "UW_LOG_2", "UW_LOG_3", "UW_LOG_4",
    "UW_ROCKPROP_1", "UW_ROCKPROP_2", "UW_ROCKPROP_3",
    "UW_STONE_1", "UW_STONE_2", "UW_STONE_3",
    "UW_CRYSTAL_1", "UW_CRYSTAL_2", "UW_CRYSTAL_3", "UW_CRYSTAL_4",
    "UW_TUFT_1", "UW_TUFT_2", "UW_TUFT_3", "UW_TUFT_4",
    "UW_REED_1", "UW_REED_2", "UW_REED_3",
]


def fail(msg):
    print("bake_shrink_lum_uw: ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def rle_decode(data, off, length, n):
    out = bytearray()
    i = off
    end = off + length
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
    if len(out) != n or i != end:
        fail("RLE decode length %d != %d" % (len(out), n))
    return bytes(out)


def resample_idx(src, ow, oh, nw, nh):
    dst = bytearray(nw * nh)
    for y in range(nh):
        sy = min(oh - 1, int((y + 0.5) * oh / nh))
        for x in range(nw):
            sx = min(ow - 1, int((x + 0.5) * ow / nw))
            dst[y * nw + x] = src[sy * ow + sx]
    return dst


def main():
    with open(OUT_FILE, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    parsed = bd.parse_header(text)

    if bd.emit_enum(parsed["records"]) != parsed["enum_sec"] + "\n":
        fail("enum emitter disagrees - format drift, aborting")
    if bd.emit_sprites(parsed["records"]) != parsed["spr_sec"] + "\n":
        fail("sprite emitter disagrees - format drift, aborting")
    if bd.emit_pal(parsed["pal"]) != parsed["pal_sec"] + "\n":
        fail("palette emitter disagrees - format drift, aborting")
    if bd.emit_data(parsed["data"]) != parsed["data_sec"] + "\n":
        fail("data emitter disagrees - format drift, aborting")
    print("  round-trip identity verified")

    targets = LUM_SHRINK + UW_SHRINK
    by_name = {r["name"]: r for r in parsed["records"]}
    for name in targets:
        if name not in by_name:
            fail("sprite %s not in header" % name)
    # Refuse twice: LUM_TREE 56 wide -> 34 at 0.6x.
    if by_name["LUM_TREE"]["w"] == 34:
        fail("LUM_TREE already 34 wide - refusing to shrink twice")
    if by_name["LUM_TREE"]["w"] != 56:
        fail("LUM_TREE is %d wide, not the 56 this tool shrinks"
             % by_name["LUM_TREE"]["w"])
    for guard in ("LUM_STAG", "LUM_PORTAL", "UW_PORTAL_A",
                  "LUM_GRASS_A", "UW_FLOOR_A"):
        if guard not in by_name:
            fail("guard sprite %s missing" % guard)

    pal = parsed["pal"]
    old_data = parsed["data"]
    shrunk = {}
    for name in targets:
        r = by_name[name]
        ow, oh = r["w"], r["h"]
        nw = max(1, int(round(ow * SCALE)))
        nh = max(1, int(round(oh * SCALE)))
        src = rle_decode(old_data, r["off"], r["len"], ow * oh)
        dst = resample_idx(src, ow, oh, nw, nh)
        if max(dst) > len(pal):
            fail("%s indexes past the palette" % name)
        if not set(dst) <= set(src):
            fail("%s gained an index the full-size sprite lacks" % name)
        if 0 not in set(dst) and 0 in set(src) and nw * nh > 1:
            pass  # tiny resamples may lose all transparency; allowed
        shrunk[name] = (dst, nw, nh)
        print("  %-14s %3dx%-3d -> %3dx%-3d" % (name, ow, oh, nw, nh))

    new_records = []
    pixels = []
    for r in parsed["records"]:
        if r["name"] in shrunk:
            dst, nw, nh = shrunk[r["name"]]
            new_records.append({"name": r["name"], "w": nw, "h": nh,
                                "ax": nw // 2, "ay": nh})
            pixels.append(bytes(dst))
        else:
            new_records.append({"name": r["name"], "w": r["w"], "h": r["h"],
                                "ax": r["ax"], "ay": r["ay"]})
            pixels.append(rle_decode(old_data, r["off"], r["len"],
                                     r["w"] * r["h"]))
    assert len(new_records) == len(parsed["records"]), "record count moved"

    # Soften Lumiara's tone: retone palette entries used by LUM_* sprites and
    # by nothing else, so Forest/Underworld/Dungeon/NPC/character cannot move.
    lum_names = {r["name"] for r in new_records if r["name"].startswith("LUM_")}
    use_lum = set()
    use_other = set()
    for r, pixel in zip(new_records, pixels):
        s = set(pixel)
        s.discard(0)
        if r["name"] in lum_names:
            use_lum |= s
        else:
            use_other |= s
    lum_only = use_lum - use_other
    new_pal = list(pal)
    toned = 0
    for idx in sorted(lum_only):
        r, g, b = pal[idx - 1]
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        nr = lum + (r - lum) * LUM_TONE_DESAT
        ng = lum + (g - lum) * LUM_TONE_DESAT
        nb = lum + (b - lum) * LUM_TONE_DESAT
        nr = max(0, min(255, int(round(nr * LUM_TONE_DIM))))
        ng = max(0, min(255, int(round(ng * LUM_TONE_DIM))))
        nb = max(0, min(255, int(round(nb * LUM_TONE_DIM))))
        if (nr, ng, nb) != (r, g, b):
            new_pal[idx - 1] = (nr, ng, nb)
            toned += 1
    print("  tone: softened %d Lumiara-only palette entries (desat %.2f dim %.2f)"
          % (toned, LUM_TONE_DESAT, LUM_TONE_DIM))
    pal = new_pal

    new_data = bytearray()
    dedupe = {}
    for r, pixel in zip(new_records, pixels):
        rle = bd.get_rle(pixel)
        hit = dedupe.get(rle)
        if hit is None:
            hit = {"off": len(new_data), "len": len(rle)}
            new_data.extend(rle)
            dedupe[rle] = hit
        r["off"], r["len"] = hit["off"], hit["len"]

    for r, pixel in zip(new_records, pixels):
        got = rle_decode(new_data, r["off"], r["len"], r["w"] * r["h"])
        if got != pixel:
            fail("%s does not decode back" % r["name"])
    for r in new_records:
        blob = rle_decode(new_data, r["off"], r["len"], r["w"] * r["h"])
        if blob and max(blob) > len(pal):
            fail("%s indexes past the palette" % r["name"])
    print("  rebuilt ART_DATA: %d -> %d bytes; all %d sprites decode"
          % (len(old_data), len(new_data), len(new_records)))

    def sub_count(pat, rep):
        nonlocal_text = [text]
        new_text, n = re.subn(pat, rep, nonlocal_text[0], count=1, flags=re.S)
        if n != 1:
            fail("pattern %r matched %d times" % (pat[:40], n))
        return new_text

    text = sub_count(r"#define ART_SPRITE_COUNT \d+",
                     "#define ART_SPRITE_COUNT %d" % len(new_records))
    text = sub_count(r"#define ART_DATA_BYTES \d+",
                     "#define ART_DATA_BYTES %d" % len(new_data))
    text = sub_count(r"(enum \{\n).*?(    ART_NONE = -1\n\};\n)",
                     r"\g<1>" + bd.emit_enum(new_records) + r"\g<2>")
    text = sub_count(r"(static const ArtSprite ART_SPRITES\[ART_SPRITE_COUNT\] = \{\n).*?(\};\n)",
                     r"\g<1>" + bd.emit_sprites(new_records) + r"\g<2>")
    text = sub_count(r"(static const unsigned char ART_PAL\[ART_PAL_BYTES\] = \{\n).*?(\};\n)",
                     r"\g<1>" + bd.emit_pal(pal) + r"\g<2>")
    text = sub_count(r"(static const unsigned char ART_DATA\[ART_DATA_BYTES\] = \{\n).*?(\};\n)",
                     r"\g<1>" + bd.emit_data(new_data) + r"\g<2>")

    with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    re2 = bd.parse_header(text)
    assert re2["sprite_count"] == len(new_records)
    assert re2["pal_n"] == len(pal), "palette count must not move"
    assert re2["pal"] == pal, "palette did not round-trip"
    assert len(re2["data"]) == len(new_data)
    print("  wrote %s: %d sprites shrunk, palette %d, data %d -> %d" %
          (OUT_FILE, len(targets), len(pal), len(old_data), len(new_data)))


if __name__ == "__main__":
    main()
