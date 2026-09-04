#!/usr/bin/env python3
"""
Wayfarer Dungeon art bake: assets/Dungeon/** -> src/art_data.h (incremental).

WHY INCREMENTAL: tools/bake.py is the canonical full baker, but the Lumiara
sources it names (TopDown/topdown_dream_tree.png, runic_chest.png,
lantern_post_purple.png, fauna_*.png) are not in this checkout, so a full
re-bake would DROP shipped Lumiara art. Until those sources are restored, new
art is APPENDED by parsing the committed header, baking against its palette,
and splicing records in BEFORE the character block (sprite_selftest classifies
everything from ART_CH_IDLE_DOWN_0 onward as character frames, so Dungeon
sprites must sort before it to be checked as decorations).

SAFETY: the parser re-emits the enum, sprite table, palette and data sections
from what it parsed and requires byte-identity with the file BEFORE changing
anything. A format drift fails loudly instead of corrupting the header.
Insertion is after every index the header stores elsewhere (ART_TILE_AT,
ART_NPC_BASE), which is asserted - so those sections are untouched textually.

Usage:  python tools/bake_dungeon.py
"""

import os
import re
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT_FILE = os.path.join(ROOT, "src", "art_data.h")

TILE = 16
NPC_SNAP_D2 = 100          # same snap radius as bake.py's NPC block
DUN_PALETTE_BUDGET = 14    # 239 used of 254; leave one spare
DUN_TORCH_BG = (64, 54, 69)
DUN_TORCH_FRAMES = 4


def fail(msg):
    print(f"bake_dungeon: ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


# ---- header parsing -------------------------------------------------------
def parse_header(text):
    m = re.search(r"#define ART_SPRITE_COUNT (\d+)", text)
    sprite_count = int(m.group(1))
    m = re.search(r"#define ART_PAL_N\s+(\d+)", text)
    pal_n = int(m.group(1))

    enum_sec = re.search(r"enum \{\n(.*?)\n\};\n", text, re.S).group(1)
    if not enum_sec.endswith("\n    ART_NONE = -1"):
        fail("enum section does not end with ART_NONE as expected")
    enum_sec = enum_sec[:len(enum_sec) - len("\n    ART_NONE = -1")]
    names = re.findall(r"    ART_(\w+) = (\d+),", enum_sec)

    spr_sec = re.search(
        r"static const ArtSprite ART_SPRITES\[ART_SPRITE_COUNT\] = \{\n(.*?)\n\};\n",
        text, re.S).group(1)
    recs = re.findall(
        r"    \{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}, /\* (\w+) \*/",
        spr_sec)

    pal_sec = re.search(
        r"static const unsigned char ART_PAL\[ART_PAL_BYTES\] = \{\n(.*?)\n\};\n",
        text, re.S).group(1)
    pal_nums = [int(x) for x in re.findall(r"\d+", pal_sec)]

    data_sec = re.search(
        r"static const unsigned char ART_DATA\[ART_DATA_BYTES\] = \{\n(.*?)\n\};\n",
        text, re.S).group(1)
    data_nums = [int(x) for x in re.findall(r"\d+", data_sec)]

    if len(names) != sprite_count or len(recs) != sprite_count:
        fail(f"enum ({len(names)}) / table ({len(recs)}) != count ({sprite_count})")
    for i, ((en, ei), r) in enumerate(zip(names, recs)):
        w, h, ax, ay, off, ln, rn = r
        if en != rn or int(ei) != i:
            fail(f"enum/table mismatch at {i}: {en}={ei} vs {rn}")
    if len(pal_nums) != pal_n * 3:
        fail("palette body length disagrees with ART_PAL_N")
    pal = [(pal_nums[i], pal_nums[i + 1], pal_nums[i + 2])
           for i in range(0, len(pal_nums), 3)]
    records = [{"name": r[6], "w": int(r[0]), "h": int(r[1]),
                "ax": int(r[2]), "ay": int(r[3]),
                "off": int(r[4]), "len": int(r[5])} for r in recs]
    return {
        "sprite_count": sprite_count, "pal_n": pal_n,
        "records": records, "pal": pal, "data": bytearray(data_nums),
        "enum_sec": enum_sec, "spr_sec": spr_sec,
        "pal_sec": pal_sec, "data_sec": data_sec,
    }


# ---- section emitters (must match tools/bake.py byte-for-byte) ------------
def emit_enum(records):
    out = ""
    for i, r in enumerate(records):
        out += f"    ART_{r['name']} = {i},\n"
    return out


def emit_sprites(records):
    out = ""
    for r in records:
        out += (f"    {{ {r['w']:3d}, {r['h']:3d}, {r['ax']:3d}, {r['ay']:3d}, "
                f"{r['off']:6d}, {r['len']:5d} }}, /* {r['name']} */\n")
    return out


def emit_pal(pal):
    out = ""
    for i, c in enumerate(pal):
        if i % 8 == 0:
            out += "    "
        out += f"{c[0]:3d}, {c[1]:3d}, {c[2]:3d}, "
        if (i + 1) % 8 == 0:
            out += "\n"
    if len(pal) % 8 != 0:
        out += "\n"
    return out


def emit_data(data):
    out = ""
    for i, b in enumerate(data):
        if i % 16 == 0:
            out += "    "
        out += f"{b:3d},"
        if (i + 1) % 16 == 0:
            out += "\n"
    if len(data) % 16 != 0:
        out += "\n"
    return out


# ---- bake state (mirrors bake.py's globals) -------------------------------
pal_list = []
pal_map = {}
records = []
data_bytes = bytearray()
dedupe = {}


def get_palette_index(r, g, b):
    key = (r, g, b)
    if key in pal_map:
        return pal_map[key]
    idx = len(pal_list) + 1
    if idx > 254:
        raise ValueError(f"Global palette exceeded 254 colours at {key} as #{idx}")
    pal_list.append(key)
    pal_map[key] = idx
    return idx


def get_opaque_box(im, x0, y0, w, h):
    min_x, min_y = w, h
    max_x, max_y = -1, -1
    px = im.load()
    for y in range(h):
        for x in range(w):
            if px[x0 + x, y0 + y][3] >= 128:
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)
    if max_x < 0:
        return None
    return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)


def get_index_array(im, x0, y0, w, h, tint=None):
    px = im.load()
    idx = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            p = px[x0 + x, y0 + y]
            if p[3] < 128:
                idx[y * w + x] = 0
            else:
                r, g, b = p[0], p[1], p[2]
                if tint:
                    r, g, b = tint(r, g, b)
                idx[y * w + x] = get_palette_index(r, g, b)
    return idx


def get_rle(idx):
    out = bytearray()
    lit = bytearray()
    n = len(idx)
    i = 0

    def flush():
        nonlocal lit
        while len(lit) > 0:
            take = min(128, len(lit))
            out.append(0x80 | (take - 1))
            out.extend(lit[:take])
            lit = lit[take:]

    while i < n:
        v = idx[i]
        run = 1
        while (i + run) < n and idx[i + run] == v and run < 128:
            run += 1
        if run >= 2:
            flush()
            out.append(run - 1)
            out.append(v)
            i += run
        else:
            lit.append(v)
            i += 1
    flush()
    return bytes(out)


def add_sprite(name, idx, w, h, ax, ay):
    if ax < 0 or ay < 0 or ax > 65535 or ay > 65535:
        raise ValueError(f"Anchor out of range for {name}")
    rle = get_rle(idx)
    if rle in dedupe:
        hit = dedupe[rle]
        off, length = hit["off"], hit["len"]
    else:
        off = len(data_bytes)
        data_bytes.extend(rle)
        length = len(rle)
        dedupe[rle] = {"off": off, "len": length}
    records.append({"name": name, "w": w, "h": h, "ax": ax, "ay": ay,
                    "off": off, "len": length})


# ---- the Dungeon bake -----------------------------------------------------
def bake_dungeon():
    dun_png = os.path.join(ASSETS, "Dungeon", "Dungeon tileset.png")
    img_dun = Image.open(dun_png).convert("RGBA")
    dun_tile_cells = [
        {"n": "DUN_FLOOR_A", "c": 9, "r": 10},
        {"n": "DUN_FLOOR_B", "c": 10, "r": 10},
        {"n": "DUN_FLOOR_C", "c": 9, "r": 11},
        {"n": "DUN_FLOOR_D", "c": 10, "r": 11},
        {"n": "DUN_CRACK_A", "c": 0, "r": 10},
        {"n": "DUN_CRACK_B", "c": 1, "r": 10},
        {"n": "DUN_WALL_A", "c": 6, "r": 1},
        {"n": "DUN_WALL_B", "c": 7, "r": 1},
        {"n": "DUN_WALL_C", "c": 2, "r": 3},
        {"n": "DUN_GRATE", "c": 9, "r": 2},
    ]
    dun_prop_rects = [
        {"n": "DUN_BANNER_B", "x": 5 * TILE, "y": 3 * TILE, "w": 16, "h": 32},
        {"n": "DUN_BANNER_R", "x": 6 * TILE, "y": 3 * TILE, "w": 16, "h": 32},
        {"n": "DUN_FIREPLACE", "x": 8 * TILE, "y": 3 * TILE, "w": 16, "h": 32},
        {"n": "DUN_DOOR", "x": 0, "y": 5 * TILE, "w": 32, "h": 32},
        {"n": "DUN_BARREL", "x": 7 * TILE, "y": 5 * TILE, "w": 32, "h": 32},
        {"n": "DUN_POT", "x": 9 * TILE, "y": 5 * TILE, "w": 16, "h": 32},
        {"n": "DUN_POTSML", "x": 10 * TILE, "y": 5 * TILE, "w": 16, "h": 32},
        {"n": "DUN_PILLAR_A", "x": 20 * TILE, "y": 0, "w": 16, "h": 48},
        {"n": "DUN_PILLAR_B", "x": 21 * TILE, "y": 0, "w": 16, "h": 48},
        {"n": "DUN_PORTC", "x": 23 * TILE, "y": TILE, "w": 16, "h": 32},
        {"n": "DUN_SKULL", "x": 208, "y": 69, "w": 48, "h": 27},
        {"n": "DUN_STAIRS", "x": 17 * TILE, "y": 0, "w": 32, "h": 16},
    ]
    for t in dun_tile_cells:
        box = get_opaque_box(img_dun, t["c"] * TILE, t["r"] * TILE, TILE, TILE)
        if box != (0, 0, TILE, TILE):
            raise RuntimeError(
                f"Dungeon tile {t['n']} at cell {t['c']},{t['r']} is not fully "
                f"opaque - tiles draw with no transparency term")

    dun_torch_gif = Image.open(os.path.join(ASSETS, "Dungeon", "01.gif"))
    if getattr(dun_torch_gif, "n_frames", 1) != DUN_TORCH_FRAMES:
        raise RuntimeError("Dungeon 01.gif must hold 4 torch frames")
    dun_torch_frames = []
    for f in range(DUN_TORCH_FRAMES):
        dun_torch_gif.seek(f)
        dun_torch_frames.append(dun_torch_gif.convert("RGB"))
    for f in dun_torch_frames:
        fp = f.load()
        for cx, cy in ((0, 0), (63, 0), (0, 127), (63, 127)):
            if fp[cx, cy] != DUN_TORCH_BG:
                raise RuntimeError("Dungeon torch background changed - re-key it")
    tbox = None
    for f in dun_torch_frames:
        fp = f.load()
        xs = [x for y in range(128) for x in range(64) if fp[x, y] != DUN_TORCH_BG]
        ys = [y for y in range(128) for x in range(64) if fp[x, y] != DUN_TORCH_BG]
        if not xs:
            raise RuntimeError("Dungeon torch frame is all background")
        if tbox is None:
            tbox = [min(xs), min(ys), max(xs), max(ys)]
        else:
            tbox[0] = min(tbox[0], min(xs))
            tbox[1] = min(tbox[1], min(ys))
            tbox[2] = max(tbox[2], max(xs))
            tbox[3] = max(tbox[3], max(ys))

    dun_king_im = Image.open(
        os.path.join(ASSETS, "Dungeon", "King", "south.png")).convert("RGBA")
    dun_king_box = get_opaque_box(dun_king_im, 0, 0,
                                  dun_king_im.width, dun_king_im.height)
    if dun_king_box is None:
        raise RuntimeError("Dungeon King south.png is fully transparent")

    dun_hist = {}

    def hist_add(col):
        dun_hist[col] = dun_hist.get(col, 0) + 1

    px = img_dun.load()
    for t in dun_tile_cells:
        for y in range(t["r"] * TILE, (t["r"] + 1) * TILE):
            for x in range(t["c"] * TILE, (t["c"] + 1) * TILE):
                hist_add((px[x, y][0], px[x, y][1], px[x, y][2]))
    for d in dun_prop_rects:
        for y in range(d["y"], d["y"] + d["h"]):
            for x in range(d["x"], d["x"] + d["w"]):
                p = px[x, y]
                if p[3] >= 128:
                    hist_add((p[0], p[1], p[2]))
    for f in dun_torch_frames:
        fp = f.load()
        for y in range(128):
            for x in range(64):
                p = fp[x, y]
                if p != DUN_TORCH_BG:
                    hist_add(p)
    dun_kpx = dun_king_im.load()
    for y in range(dun_king_box[1], dun_king_box[1] + dun_king_box[3]):
        for x in range(dun_king_box[0], dun_king_box[0] + dun_king_box[2]):
            p = dun_kpx[x, y]
            if p[3] >= 128:
                hist_add((p[0], p[1], p[2]))

    dun_snap_base = list(pal_list)
    dun_color_map = {}
    dun_left = []
    for col in dun_hist:
        best_d = 1 << 30
        best = None
        for c in dun_snap_base:
            dr, dg, db = col[0] - c[0], col[1] - c[1], col[2] - c[2]
            d = dr * dr + dg * dg + db * db
            if d < best_d:
                best_d, best = d, c
        if best is not None and best_d <= NPC_SNAP_D2:
            dun_color_map[col] = best
        else:
            dun_left.append((col, dun_hist[col]))

    dun_buckets = [dun_left]
    while len(dun_buckets) < DUN_PALETTE_BUDGET:
        best_idx, best_range, best_chan = -1, 0, 0
        for i, bk in enumerate(dun_buckets):
            if len(bk) <= 1:
                continue
            rs = [e[0][0] for e in bk]
            gs = [e[0][1] for e in bk]
            bs = [e[0][2] for e in bk]
            rr, gg, bb = max(rs) - min(rs), max(gs) - min(gs), max(bs) - min(bs)
            mx = max(rr, gg, bb)
            if mx > best_range:
                best_range, best_idx = mx, i
                best_chan = 0 if mx == rr else (1 if mx == gg else 2)
        if best_idx < 0:
            break
        bk = dun_buckets.pop(best_idx)
        bk.sort(key=lambda e: e[0][best_chan])
        mid = len(bk) // 2
        dun_buckets.append(bk[:mid])
        dun_buckets.append(bk[mid:])
    for bk in dun_buckets:
        sr = sum(e[0][0] * e[1] for e in bk)
        sg = sum(e[0][1] * e[1] for e in bk)
        sb = sum(e[0][2] * e[1] for e in bk)
        sn = sum(e[1] for e in bk)
        if sn > 0:
            rep = (round(sr / sn), round(sg / sn), round(sb / sn))
            for e in bk:
                dun_color_map[e[0]] = rep

    def dun_tint(r, g, b):
        return dun_color_map[(r, g, b)]

    for t in dun_tile_cells:
        idx = get_index_array(img_dun, t["c"] * TILE, t["r"] * TILE,
                              TILE, TILE, dun_tint)
        add_sprite(t["n"], idx, TILE, TILE, 0, 0)
    for d in dun_prop_rects:
        box = get_opaque_box(img_dun, d["x"], d["y"], d["w"], d["h"])
        if box is None:
            raise RuntimeError(f"Dungeon prop {d['n']} is fully transparent")
        bx, by, bw, bh = box
        idx = get_index_array(img_dun, d["x"] + bx, d["y"] + by, bw, bh, dun_tint)
        add_sprite(d["n"], idx, bw, bh, bw // 2, bh)
    tbx0, tby0, tbx1, tby1 = tbox
    tw, th = tbx1 - tbx0 + 1, tby1 - tby0 + 1
    for f in range(DUN_TORCH_FRAMES):
        im = dun_torch_frames[f]
        fp = im.load()
        idx = bytearray(tw * th)
        for y in range(tby0, tby1 + 1):
            for x in range(tbx0, tbx1 + 1):
                p = fp[x, y]
                if p == DUN_TORCH_BG:
                    idx[(y - tby0) * tw + (x - tbx0)] = 0
                else:
                    qr, qg, qb = dun_tint(p[0], p[1], p[2])
                    idx[(y - tby0) * tw + (x - tbx0)] = get_palette_index(qr, qg, qb)
        add_sprite(f"DUN_TORCH_{f}", idx, tw, th, tw // 2, th)
    dun_king_idx = get_index_array(dun_king_im, dun_king_box[0], dun_king_box[1],
                                   dun_king_box[2], dun_king_box[3], dun_tint)
    add_sprite("DUN_KING", dun_king_idx,
               dun_king_box[2], dun_king_box[3],
               dun_king_box[2] // 2, dun_king_box[3])
    return len(dun_tile_cells), len(dun_prop_rects)


# ---- main -----------------------------------------------------------------
def main():
    print("bake_dungeon: reading src/art_data.h...")
    with open(OUT_FILE, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    parsed = parse_header(text)

    # Round-trip identity: prove the emitters before trusting them.
    if emit_enum(parsed["records"]) != parsed["enum_sec"] + "\n":
        fail("enum emitter disagrees with file - format drift, aborting")
    if emit_sprites(parsed["records"]) != parsed["spr_sec"] + "\n":
        fail("sprite emitter disagrees with file - format drift, aborting")
    if emit_pal(parsed["pal"]) != parsed["pal_sec"] + "\n":
        fail("palette emitter disagrees with file - format drift, aborting")
    if emit_data(parsed["data"]) != parsed["data_sec"] + "\n":
        fail("data emitter disagrees with file - format drift, aborting")
    print("  round-trip identity verified (enum, sprites, palette, data)")

    if any(r["name"].startswith("DUN_") for r in parsed["records"]):
        fail("header already holds DUN_ sprites - refusing to bake twice")

    # Restore bake state from the header.
    global pal_list, pal_map, records, data_bytes, dedupe
    pal_list = list(parsed["pal"])
    pal_map = {c: i + 1 for i, c in enumerate(pal_list)}
    records = list(parsed["records"])
    data_bytes = bytearray(parsed["data"])
    for r in records:
        sl = bytes(data_bytes[r["off"]:r["off"] + r["len"]])
        if sl not in dedupe:
            dedupe[sl] = {"off": r["off"], "len": r["len"]}
    pal_before = len(pal_list)
    rec_before = len(records)

    n_tiles, n_props = bake_dungeon()
    dun_new_recs = records[rec_before:]
    dun_new_pal = len(pal_list) - pal_before

    # Insertion point: before the first character frame, after every stored id.
    ch_start = next(i for i, r in enumerate(parsed["records"])
                    if r["name"] == "CH_IDLE_DOWN_0")
    tile_ids = [int(x) for x in
                re.findall(r"\{([-\d,\s]+)\}, /\* row \d+ \*/", text)
                for x in x.split(",")]
    npc_ids = [int(x) for x in re.findall(
        r"ART_NPC_BASE\[ART_NPC_KINDS\] = \{([^}]*)\}", text)[0].split(",")]
    npc_last = max(npc_ids) + 3  # ART_NPC_FRAMES - 1: the last frame index
    if max(tile_ids + [npc_last]) >= ch_start:
        fail("stored sprite ids reach into the character block - cannot splice")
    if dun_new_pal > DUN_PALETTE_BUDGET:
        fail(f"dungeon added {dun_new_pal} colours over budget {DUN_PALETTE_BUDGET}")

    new_records = (parsed["records"][:ch_start] + dun_new_recs
                   + parsed["records"][ch_start:])
    new_pal = pal_list
    new_data = data_bytes

    # Splice the four regenerated sections plus their defines.
    def sub_count(pat, rep):
        nonlocal text
        text, n = re.subn(pat, rep, text, count=1, flags=re.S)
        if n != 1:
            fail(f"pattern {pat[:40]!r} matched {n} times")

    sub_count(r"#define ART_SPRITE_COUNT \d+",
              f"#define ART_SPRITE_COUNT {len(new_records)}")
    sub_count(r"#define ART_PAL_N\s+\d+",
              f"#define ART_PAL_N     {len(new_pal)}")
    sub_count(r"#define ART_PAL_BYTES \d+",
              f"#define ART_PAL_BYTES {len(new_pal) * 3}")
    sub_count(r"#define ART_DATA_BYTES \d+",
              f"#define ART_DATA_BYTES {len(new_data)}")
    sub_count(r"(enum \{\n).*?(    ART_NONE = -1\n\};\n)",
              r"\g<1>" + emit_enum(new_records) + r"\g<2>")
    sub_count(r"(static const ArtSprite ART_SPRITES\[ART_SPRITE_COUNT\] = \{\n).*?(\};\n)",
              r"\g<1>" + emit_sprites(new_records) + r"\g<2>")
    sub_count(r"(static const unsigned char ART_PAL\[ART_PAL_BYTES\] = \{\n).*?(\};\n)",
              r"\g<1>" + emit_pal(new_pal) + r"\g<2>")
    sub_count(r"(static const unsigned char ART_DATA\[ART_DATA_BYTES\] = \{\n).*?(\};\n)",
              r"\g<1>" + emit_data(new_data) + r"\g<2>")

    with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    # Re-parse the output: counts, order, ranges.
    reparsed = parse_header(text)
    assert reparsed["sprite_count"] == len(new_records)
    assert reparsed["pal_n"] == len(new_pal)
    assert len(reparsed["data"]) == len(new_data)
    got = [r["name"] for r in reparsed["records"]]
    assert got.index("DUN_FLOOR_A") < got.index("CH_IDLE_DOWN_0")
    assert got.index("DUN_KING") < got.index("CH_IDLE_DOWN_0")
    print(f"  dungeon: {n_tiles} tiles, {n_props} props, "
          f"{DUN_TORCH_FRAMES} torch frames, king; "
          f"{dun_new_pal} new colours ({pal_before} -> {len(new_pal)})")
    print(f"  sprites: {rec_before} -> {len(new_records)}; "
          f"wrote {OUT_FILE}")


if __name__ == "__main__":
    main()
