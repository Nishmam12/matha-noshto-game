#!/usr/bin/env python3
"""
Wayfarer Dungeon art FIX bake: three edits to src/art_data.h, in place.

  1. DROP  DUN_DOOR      - main.c no longer draws it (a door nothing can lock
                           on a tile nothing makes solid), and art nothing
                           draws is shipped bytes.
  2. SHRINK DUN_TORCH_*  - the wall sconces baked at 28x68, taller than the
                           king and over four tiles high on a 16px grid. Down
                           to 65%, which is the height of the banners and the
                           fireplace they hang beside.
  3. ADD   DUN_MONSTER_* - what the king turns into at the end of the audience
                           (see story_end_say / SF_BEAST in main.c).

WHY A SECOND INCREMENTAL TOOL: tools/bake.py cannot run in this checkout (the
Lumiara sources it names are not here, so a full re-bake would DROP shipped
art), and tools/bake_dungeon.py refuses to run on a header that already holds
DUN_ sprites - correctly, since it appends rather than edits. This one edits.
It imports bake_dungeon for its parser and its four section emitters, so the
two tools cannot drift in how they read or write the file.

DISCIPLINE, same as bake_dungeon.py:
  - the parser re-emits every section and requires byte-identity with the file
    BEFORE changing anything; format drift fails loudly.
  - NO new palette colours. The torch is resampled NEAREST (a subset of the
    pixels it already had, so a subset of the colours), and the monster's 17
    colours are snapped onto entries that already exist. The palette stands at
    253 of the 254 the format allows, so there is no room to spend and the
    snap is not a preference.
  - ART_DATA is rebuilt from the retained records, which is what actually
    reclaims the dropped door's bytes, and every retained sprite is then
    DECODED from the new blob and compared pixel-for-pixel against its decode
    from the old one. A wrong offset is caught here, not by a screenshot.
  - refuses to run twice (DUN_MONSTER_0 present, or the torch already small).

Usage:  python tools/bake_dungeon_fix.py [--preview-only]
"""

import os
import re
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake_dungeon as bd          # parser + emitters, reused verbatim

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT_FILE = os.path.join(ROOT, "src", "art_data.h")
PREVIEW_DIR = os.path.join(ROOT, "build")

TORCH_SCALE = 0.65             # 28x68 -> 18x44, the banners' height
TORCH_OLD_H = 68               # what the header must say before this runs
TORCH_FRAMES = 4
TORCH_BG = bd.DUN_TORCH_BG

MONSTER_PNG = os.path.join(ASSETS, "Dungeon", "King", "monster.png")
MONSTER_FRAMES = 8
MONSTER_CELL = 80
MONSTER_KEEP_ROWS = 71         # rows 71-79 are the artist's watermark strip
MONSTER_MAX_SNAP_D2 = 800      # every colour must land at least this close


def fail(msg):
    print("bake_dungeon_fix: ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


# ---- RLE decode, so the rebuild can be checked rather than trusted --------
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
        fail("RLE decode length %d != %d (or ran past its record)" % (len(out), n))
    return bytes(out)


def snap(pal, col):
    """Nearest existing palette entry, 1-based index, plus the squared error."""
    best_d, best_i = 1 << 30, 0
    for i, q in enumerate(pal):
        d = ((col[0] - q[0]) ** 2 + (col[1] - q[1]) ** 2 + (col[2] - q[2]) ** 2)
        if d < best_d:
            best_d, best_i = d, i + 1
    return best_i, best_d


def preview(name, idx, w, h, pal, scale=4):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = idx[y * w + x]
            px[x, y] = (0, 0, 0, 0) if v == 0 else pal[v - 1] + (255,)
    im.resize((w * scale, h * scale), Image.NEAREST).save(
        os.path.join(PREVIEW_DIR, "preview_%s.png" % name))


# ---- (2) the torch, resampled ---------------------------------------------
def bake_torches(parsed, pal):
    """The four frames, nearest-neighbour resampled IN INDEX SPACE.

    Resampled from the baked sprite rather than from assets/Dungeon/01.gif,
    which is the only way to be sure this adds no colour: the GIF's colours are
    not the palette's - bake_dungeon.py put every one of them through a
    histogram snap and a median cut first, and that mapping is not reproducible
    without re-running the whole Dungeon bake. Sampling the indices the header
    already holds makes the result a strict SUBSET of the pixels that shipped,
    which is a property this tool can assert rather than hope for.

    Nearest neighbour, not an average: averaging indices is meaningless, and
    averaging colours would invent them. All four frames share one box and one
    grid, so the flame cannot skate as it animates."""
    old_data = parsed["data"]
    by_name = {r["name"]: r for r in parsed["records"]}
    ow = by_name["DUN_TORCH_0"]["w"]
    oh = by_name["DUN_TORCH_0"]["h"]
    nw = max(1, int(round(ow * TORCH_SCALE)))
    nh = max(1, int(round(oh * TORCH_SCALE)))

    out = []
    for f in range(TORCH_FRAMES):
        r = by_name["DUN_TORCH_%d" % f]
        if r["w"] != ow or r["h"] != oh:
            fail("torch frame %d is %dx%d, not %dx%d like frame 0"
                 % (f, r["w"], r["h"], ow, oh))
        src = rle_decode(old_data, r["off"], r["len"], ow * oh)
        idx = bytearray(nw * nh)
        for y in range(nh):
            sy = min(oh - 1, int((y + 0.5) * oh / nh))
            for x in range(nw):
                sx = min(ow - 1, int((x + 0.5) * ow / nw))
                idx[y * nw + x] = src[sy * ow + sx]
        if max(idx) > len(pal):
            fail("torch frame %d indexes past the palette" % f)
        if not set(idx) <= set(src):
            fail("torch frame %d gained an index the full-size frame lacks" % f)
        out.append(idx)
        preview("torch_%d" % f, idx, nw, nh, pal)
    return out, nw, nh


# ---- (3) the monster -------------------------------------------------------
def bake_monster(pal):
    """Eight frames on ONE trim box, watermark cropped, snapped to the palette.

    One box for all eight for the same reason the character sheet has one: a
    box that breathed frame to frame would make the thing skate sideways as it
    animates, and the anchor is what holds it on the king's tile."""
    im = Image.open(MONSTER_PNG).convert("RGBA")
    if im.size != (MONSTER_CELL * MONSTER_FRAMES, MONSTER_CELL):
        fail("monster.png is %dx%d, not %d frames of %d"
             % (im.width, im.height, MONSTER_FRAMES, MONSTER_CELL))
    px = im.load()

    box = None
    for f in range(MONSTER_FRAMES):
        x0 = f * MONSTER_CELL
        xs, ys = [], []
        for y in range(MONSTER_KEEP_ROWS):
            for x in range(MONSTER_CELL):
                if px[x0 + x, y][3] >= 128:
                    xs.append(x)
                    ys.append(y)
        if not xs:
            fail("monster frame %d is empty above the watermark" % f)
        b = [min(xs), min(ys), max(xs), max(ys)]
        box = b if box is None else [min(box[0], b[0]), min(box[1], b[1]),
                                     max(box[2], b[2]), max(box[3], b[3])]
    bw, bh = box[2] - box[0] + 1, box[3] - box[1] + 1

    # The watermark must really be below the box, or it ships inside the art.
    for f in range(MONSTER_FRAMES):
        x0 = f * MONSTER_CELL
        for y in range(MONSTER_KEEP_ROWS, MONSTER_CELL):
            for x in range(MONSTER_CELL):
                if px[x0 + x, y][3] >= 128 and y <= box[1] + bh - 1:
                    fail("watermark row %d falls inside the trim box" % y)

    worst = 0
    out = []
    for f in range(MONSTER_FRAMES):
        x0 = f * MONSTER_CELL + box[0]
        y0 = box[1]
        idx = bytearray(bw * bh)
        for y in range(bh):
            for x in range(bw):
                p = px[x0 + x, y0 + y]
                if p[3] < 128:
                    continue
                i, d = snap(pal, (p[0], p[1], p[2]))
                worst = max(worst, d)
                idx[y * bw + x] = i
        out.append(idx)
        preview("monster_%d" % f, idx, bw, bh, pal)
    if worst > MONSTER_MAX_SNAP_D2:
        fail("a monster colour snapped %d away (squared RGB), over the %d this"
             " tool accepts - the palette is full, so it cannot be added"
             % (worst, MONSTER_MAX_SNAP_D2))
    print("  monster: %d frames, %dx%d, worst colour snap d2=%d"
          % (MONSTER_FRAMES, bw, bh, worst))
    return out, bw, bh


# ---- main ------------------------------------------------------------------
def main():
    preview_only = "--preview-only" in sys.argv

    print("bake_dungeon_fix: reading src/art_data.h...")
    with open(OUT_FILE, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    parsed = bd.parse_header(text)

    if bd.emit_enum(parsed["records"]) != parsed["enum_sec"] + "\n":
        fail("enum emitter disagrees with file - format drift, aborting")
    if bd.emit_sprites(parsed["records"]) != parsed["spr_sec"] + "\n":
        fail("sprite emitter disagrees with file - format drift, aborting")
    if bd.emit_pal(parsed["pal"]) != parsed["pal_sec"] + "\n":
        fail("palette emitter disagrees with file - format drift, aborting")
    if bd.emit_data(parsed["data"]) != parsed["data_sec"] + "\n":
        fail("data emitter disagrees with file - format drift, aborting")
    print("  round-trip identity verified (enum, sprites, palette, data)")

    old = parsed["records"]
    by_name = {r["name"]: r for r in old}
    if "DUN_MONSTER_0" in by_name:
        fail("header already holds DUN_MONSTER_0 - refusing to fix twice")
    if "DUN_DOOR" not in by_name:
        fail("header holds no DUN_DOOR - this tool has already run")
    if by_name["DUN_TORCH_0"]["h"] != TORCH_OLD_H:
        fail("DUN_TORCH_0 is %d tall, not the %d this tool shrinks"
             % (by_name["DUN_TORCH_0"]["h"], TORCH_OLD_H))

    pal = parsed["pal"]
    torch_idx, tw, th = bake_torches(parsed, pal)
    mon_idx, mw, mh = bake_monster(pal)
    print("  torch:   %d frames, %dx%d (was %dx%d)"
          % (TORCH_FRAMES, tw, th, by_name["DUN_TORCH_0"]["w"], TORCH_OLD_H))
    if preview_only:
        print("  --preview-only: wrote build/preview_*.png, header untouched")
        return

    # ---- the new record list ------------------------------------------
    # Pixels first, records second: every retained sprite carries its decoded
    # pixels along so the rebuilt blob can be checked against them.
    old_data = parsed["data"]
    new_records = []
    pixels = []
    for r in old:
        if r["name"] == "DUN_DOOR":
            continue
        if r["name"].startswith("DUN_TORCH_"):
            f = int(r["name"].rsplit("_", 1)[1])
            new_records.append({"name": r["name"], "w": tw, "h": th,
                                "ax": tw // 2, "ay": th})
            pixels.append(bytes(torch_idx[f]))
            continue
        new_records.append({"name": r["name"], "w": r["w"], "h": r["h"],
                            "ax": r["ax"], "ay": r["ay"]})
        pixels.append(rle_decode(old_data, r["off"], r["len"], r["w"] * r["h"]))
        if r["name"] == "DUN_KING":
            for f in range(MONSTER_FRAMES):
                new_records.append({"name": "DUN_MONSTER_%d" % f,
                                    "w": mw, "h": mh,
                                    "ax": mw // 2, "ay": mh})
                pixels.append(bytes(mon_idx[f]))

    # The whole Dungeon block, monster included, must still sort before the
    # character block - sprite_selftest reads everything from CH_IDLE_DOWN_0
    # on as a character frame, and a decoration checked as one fails.
    names = [r["name"] for r in new_records]
    if names.index("DUN_MONSTER_%d" % (MONSTER_FRAMES - 1)) >= names.index("CH_IDLE_DOWN_0"):
        fail("the monster frames landed inside the character block")
    # Stored ids (ART_TILE_AT, ART_NPC_BASE) index sprites by number, so
    # nothing they name may move. Everything dropped or inserted is a DUN_
    # sprite, all of which sort after them - asserted, not assumed.
    tile_ids = [int(x) for grp in re.findall(r"\{([-\d,\s]+)\}, /\* row \d+ \*/", text)
                for x in grp.split(",")]
    npc_ids = [int(x) for x in re.findall(
        r"ART_NPC_BASE\[ART_NPC_KINDS\] = \{([^}]*)\}", text)[0].split(",")]
    first_moved = min(i for i, r in enumerate(old)
                      if r["name"] == "DUN_DOOR")
    if max(tile_ids + [max(npc_ids) + 3]) >= first_moved:
        fail("a stored sprite id sits at or past the first index this edit moves")

    # ---- rebuild ART_DATA ---------------------------------------------
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

    # ---- and check it, sprite by sprite -------------------------------
    for r, pixel in zip(new_records, pixels):
        got = rle_decode(new_data, r["off"], r["len"], r["w"] * r["h"])
        if got != pixel:
            fail("sprite %s does not decode to its own pixels after the rebuild"
                 % r["name"])
    for r in new_records:
        if max(r["w"], r["h"]) and max(bytearray(rle_decode(
                new_data, r["off"], r["len"], r["w"] * r["h"]))) > len(pal):
            fail("sprite %s indexes past the palette" % r["name"])
    print("  rebuilt ART_DATA: %d -> %d bytes; all %d sprites decode to the"
          " same pixels" % (len(old_data), len(new_data), len(new_records)))

    # ---- splice --------------------------------------------------------
    def sub_count(pat, rep):
        nonlocal text
        text, n = re.subn(pat, rep, text, count=1, flags=re.S)
        if n != 1:
            fail("pattern %r matched %d times" % (pat[:40], n))

    sub_count(r"#define ART_SPRITE_COUNT \d+",
              "#define ART_SPRITE_COUNT %d" % len(new_records))
    sub_count(r"#define ART_DATA_BYTES \d+",
              "#define ART_DATA_BYTES %d" % len(new_data))
    sub_count(r"(enum \{\n).*?(    ART_NONE = -1\n\};\n)",
              r"\g<1>" + bd.emit_enum(new_records) + r"\g<2>")
    sub_count(r"(static const ArtSprite ART_SPRITES\[ART_SPRITE_COUNT\] = \{\n).*?(\};\n)",
              r"\g<1>" + bd.emit_sprites(new_records) + r"\g<2>")
    sub_count(r"(static const unsigned char ART_DATA\[ART_DATA_BYTES\] = \{\n).*?(\};\n)",
              r"\g<1>" + bd.emit_data(new_data) + r"\g<2>")

    with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    re2 = bd.parse_header(text)
    assert re2["sprite_count"] == len(new_records)
    assert re2["pal_n"] == len(pal), "the palette must not have moved"
    assert len(re2["data"]) == len(new_data)
    got = [r["name"] for r in re2["records"]]
    assert "DUN_DOOR" not in got
    assert got.index("DUN_MONSTER_0") < got.index("CH_IDLE_DOWN_0")
    for r in re2["records"]:
        rle_decode(re2["data"], r["off"], r["len"], r["w"] * r["h"])
    print("  sprites: %d -> %d; palette unchanged at %d; wrote %s"
          % (len(old), len(new_records), len(pal), OUT_FILE))


if __name__ == "__main__":
    main()
