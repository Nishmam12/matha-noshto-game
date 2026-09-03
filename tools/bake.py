#!/usr/bin/env python3
"""
Wayfarer art bake: assets/**.png -> src/art_data.h

Python port of tools/bake.ps1, supporting Linux, macOS, and Windows.
Bakes Forest, Underworld (biome 2), and Lumiara (biome 3) assets into C header.
"""

import os
import sys
import glob
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT_FILE = os.path.join(ROOT, "src", "art_data.h")

TILE = 16
TILES_COLS = 8
TILES_ROWS = 15

CHAR_CELL_W = 48
CHAR_CELL_H = 64
CHAR_FRAMES = 8
CHAR_CX = 24
CHAR_FOOT = 44

LUM_PALETTE_BUDGET = 48
LUM_TILE_SMOOTH = 0.70
LUM_TILE_FLATTEN = 0.62
LUM_TILE_DIM = 0.93
LUM_TILE_DESAT = 0.76

# Global state
pal_list = []
pal_map = {}
records = []
data_bytes = bytearray()
dedupe = {}

def get_palette_index(r, g, b):
    key = (r, g, b)
    if key in pal_map:
        return pal_map[key]
    idx = len(pal_list) + 1  # 1-indexed (0 is transparent)
    if idx > 254:
        raise ValueError(f"Global palette exceeded 254 colours: attempted to add {key} as #{idx}")
    pal_list.append(key)
    pal_map[key] = idx
    return idx

def get_opaque_box(im, x0, y0, w, h):
    min_x, min_y = w, h
    max_x, max_y = -1, -1
    px = im.load()
    for y in range(h):
        for x in range(w):
            a = px[x0 + x, y0 + y][3]
            if a >= 128:
                if x < min_x: min_x = x
                if x > max_x: max_x = x
                if y < min_y: min_y = y
                if y > max_y: max_y = y
    if max_x < 0:
        return None
    return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)

def get_index_array(im, x0, y0, w, h, tint=None):
    px = im.load()
    idx = bytearray(w * h)
    for y in range(h):
        row_dst = y * w
        for x in range(w):
            p = px[x0 + x, y0 + y]
            a = p[3]
            if a < 128:
                idx[row_dst + x] = 0
            else:
                r, g, b = p[0], p[1], p[2]
                if tint:
                    r, g, b = tint(r, g, b)
                idx[row_dst + x] = get_palette_index(r, g, b)
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
        raise ValueError(f"Anchor out of range for {name}: ({ax}, {ay})")
    rle = get_rle(idx)
    if rle in dedupe:
        hit = dedupe[rle]
        off, length = hit["off"], hit["len"]
    else:
        off = len(data_bytes)
        data_bytes.extend(rle)
        length = len(rle)
        dedupe[rle] = {"off": off, "len": length}
    records.append({
        "name": name, "w": w, "h": h, "ax": ax, "ay": ay, "off": off, "len": length
    })

def main():
    print("bake.py: reading sources...")

    # --- Fantasy Forest tiles ---
    tileset_png = os.path.join(ASSETS, "Fantasy Forest", "Tiles", "Tileset.png")
    tileset_padded_png = os.path.join(ASSETS, "Fantasy Forest", "Tiles", "Tileset1xPadding.png")
    img_tiles = Image.open(tileset_png).convert("RGBA")
    img_tiles_pad = Image.open(tileset_padded_png).convert("RGBA")

    # Provenance check
    pad_step = TILE + 1
    pad_mismatch = 0
    px_t = img_tiles.load()
    px_tp = img_tiles_pad.load()
    for r in range(TILES_ROWS):
        for c in range(TILES_COLS):
            for y in range(TILE):
                for x in range(TILE):
                    p1 = px_t[c * TILE + x, r * TILE + y]
                    p2 = px_tp[1 + c * pad_step + x, 1 + r * pad_step + y]
                    if p1 != p2:
                        pad_mismatch += 1
    if pad_mismatch > 0:
        raise RuntimeError(f"Provenance mismatch: {pad_mismatch} pixels differ between Tileset and Tileset1xPadding")
    print(f"  provenance  {TILES_COLS * TILES_ROWS} cells verified identical")

    tile_at = [[-1 for _ in range(TILES_COLS)] for _ in range(TILES_ROWS)]
    for r in range(TILES_ROWS):
        for c in range(TILES_COLS):
            box = get_opaque_box(img_tiles, c * TILE, r * TILE, TILE, TILE)
            if box is not None:
                tile_at[r][c] = len(records)
                idx = get_index_array(img_tiles, c * TILE, r * TILE, TILE, TILE)
                add_sprite(f"TILE_C{c}_R{r}", idx, TILE, TILE, 0, 0)
    print(f"  tileset     cells baked")

    # --- Fantasy Forest decorations ---
    decor_png = os.path.join(ASSETS, "Fantasy Forest", "Decorations", "Decorations.png")
    img_decor = Image.open(decor_png).convert("RGBA")
    decor_list = [
        {"n": 'LOG_A',            "x":  75, "y":   0, "w": 40, "h": 32},
        {"n": 'BUSH_LARGE_A',     "x":  13, "y":   4, "w": 35, "h": 27},
        {"n": 'MUSHROOM_BIG_A',   "x": 203, "y":   8, "w": 13, "h": 14},
        {"n": 'BUSH_SMALL_A',     "x": 136, "y":   9, "w": 16, "h": 14},
        {"n": 'MUSHROOM_TINY_A',  "x": 235, "y":   9, "w": 11, "h": 14},
        {"n": 'MUSHROOM_MED_A',   "x": 170, "y":  10, "w": 13, "h": 13},
        {"n": 'LOG_B',            "x":  75, "y":  32, "w": 39, "h": 30},
        {"n": 'BUSH_LARGE_B',     "x":  15, "y":  35, "w": 31, "h": 25},
        {"n": 'MUSHROOM_BIG_B',   "x": 204, "y":  40, "w": 12, "h": 12},
        {"n": 'BUSH_SMALL_B',     "x": 136, "y":  41, "w": 16, "h": 13},
        {"n": 'MUSHROOM_TINY_B',  "x": 235, "y":  41, "w": 11, "h": 13},
        {"n": 'MUSHROOM_MED_B',   "x": 170, "y":  42, "w": 12, "h": 12},
        {"n": 'REED_A',           "x":  68, "y":  67, "w": 24, "h": 27},
        {"n": 'REED_B',           "x": 100, "y":  67, "w": 24, "h": 27},
        {"n": 'TUFT_A',           "x":   8, "y":  70, "w": 14, "h": 22},
        {"n": 'TUFT_B',           "x":  40, "y":  70, "w": 14, "h": 22},
        {"n": 'MUSHROOM_BIG_C',   "x": 203, "y":  71, "w": 13, "h": 14},
        {"n": 'MUSHROOM_TINY_C',  "x": 235, "y":  72, "w": 11, "h": 14},
        {"n": 'MUSHROOM_MED_C',   "x": 170, "y":  73, "w": 13, "h": 13},
        {"n": 'REED_C',           "x":  68, "y":  99, "w": 24, "h": 25},
        {"n": 'REED_D',           "x": 100, "y":  99, "w": 24, "h": 25},
        {"n": 'TUFT_C',           "x":   8, "y": 102, "w": 14, "h": 20},
        {"n": 'TUFT_D',           "x":  40, "y": 102, "w": 14, "h": 20},
        {"n": 'STONE_A',          "x": 169, "y": 109, "w": 15, "h": 11},
        {"n": 'STONE_B',          "x": 200, "y": 109, "w": 16, "h": 12},
        {"n": 'STONE_C',          "x": 234, "y": 109, "w": 12, "h": 10},
        {"n": 'PEBBLES',          "x": 138, "y": 110, "w": 12, "h": 10},
        {"n": 'ROCK_A',           "x": 163, "y": 133, "w": 26, "h": 22},
        {"n": 'ROCK_B',           "x": 194, "y": 133, "w": 27, "h": 23},
        {"n": 'ROCK_C',           "x": 228, "y": 133, "w": 22, "h": 21},
        {"n": 'TREE_A',           "x":  11, "y": 143, "w": 70, "h": 98},
        {"n": 'TREE_B',           "x":  88, "y": 143, "w": 70, "h": 94},
        {"n": 'PINE_A',           "x": 163, "y": 163, "w": 38, "h": 74},
        {"n": 'PINE_B',           "x": 211, "y": 163, "w": 38, "h": 70},
    ]
    for d in decor_list:
        box = get_opaque_box(img_decor, d["x"], d["y"], d["w"], d["h"])
        if box is None:
            raise RuntimeError(f"Decoration {d['n']} is fully transparent")
        bx, by, bw, bh = box
        idx = get_index_array(img_decor, d["x"] + bx, d["y"] + by, bw, bh)
        add_sprite(d["n"], idx, bw, bh, bw // 2, bh)
    print(f"  decorations {len(decor_list)} objects baked")

    # --- Underworld (biome 2) ---
    uw_ground_png = os.path.join(ASSETS, "Underworld2", "Tiled_files", "Ground_rocks.png")
    uw_water_png = os.path.join(ASSETS, "Underworld2", "Tiled_files", "water_coasts.png")
    img_uw_gr = Image.open(uw_ground_png).convert("RGBA")
    img_uw_wc = Image.open(uw_water_png).convert("RGBA")

    # Curated tiles from Underworld2/Tiled_files/
    uw_tile_cells = [
        # Acid water coastlines / hole edges (3x3 blob_slice mapping)
        {"n": "UW_ACID_NW", "src": "wc", "c": 5, "r": 1},
        {"n": "UW_ACID_N",  "src": "wc", "c": 6, "r": 1},
        {"n": "UW_ACID_NE", "src": "wc", "c": 8, "r": 1},
        {"n": "UW_ACID_W",  "src": "wc", "c": 5, "r": 2},
        {"n": "UW_ACID_E",  "src": "wc", "c": 8, "r": 2},
        {"n": "UW_ACID_SW", "src": "wc", "c": 5, "r": 4},
        {"n": "UW_ACID_S",  "src": "wc", "c": 6, "r": 4},
        {"n": "UW_ACID_SE", "src": "wc", "c": 8, "r": 4},

        # Base ground (Floor A..D) from Ground_rocks (col 2, row 2 is primary undead land)
        {"n": "UW_FLOOR_A", "src": "gr", "c": 2, "r": 2},
        {"n": "UW_FLOOR_B", "src": "gr", "c": 3, "r": 2},
        {"n": "UW_FLOOR_C", "src": "gr", "c": 1, "r": 2},
        {"n": "UW_FLOOR_D", "src": "gr", "c": 2, "r": 1},

        # Darker surface / rubble (Rubble A..D) from Ground_rocks (all 256/256 opaque)
        {"n": "UW_RUBBLE_A", "src": "gr", "c": 13, "r": 57}, # darker stone surface
        {"n": "UW_RUBBLE_B", "src": "gr", "c":  7, "r": 20}, # stone pavers
        {"n": "UW_RUBBLE_C", "src": "gr", "c":  9, "r": 20}, # stone pavers
        {"n": "UW_RUBBLE_D", "src": "gr", "c":  7, "r": 15}, # cracked stone ground

        # Dark spiked rock cliffs (Cliff & Rockwall): Ground_rocks
        {"n": "UW_ROCKWALL_A", "src": "gr", "c":  2, "r": 3},  # cliff face (256/256 opaque)
        {"n": "UW_ROCKWALL_B", "src": "gr", "c": 10, "r": 9},  # solid dark rock body (256/256 opaque)
        {"n": "UW_ROCK_NW",    "src": "gr", "c": 1, "r": 1},
        {"n": "UW_ROCK_N",     "src": "gr", "c": 2, "r": 1},
        {"n": "UW_ROCK_NE",    "src": "gr", "c": 3, "r": 1},
        {"n": "UW_ROCK_W",     "src": "gr", "c": 1, "r": 2},
        {"n": "UW_ROCK_E",     "src": "gr", "c": 3, "r": 2},
        {"n": "UW_ROCK_SW",    "src": "gr", "c": 1, "r": 3},
        {"n": "UW_ROCK_S",     "src": "gr", "c": 2, "r": 3},
        {"n": "UW_ROCK_SE",    "src": "gr", "c": 3, "r": 3},

        # Clean toxic acid water fill (col 22, row 0 in water_coasts is 100% opaque toxic green)
        {"n": "UW_ACIDFILL_A", "src": "wc", "c": 22, "r": 0},
        {"n": "UW_ACIDFILL_B", "src": "wc", "c": 22, "r": 0},
        {"n": "UW_ACIDFILL_C", "src": "wc", "c": 22, "r": 0},
        {"n": "UW_ACIDFILL_D", "src": "wc", "c": 22, "r": 0},

        # Stone stairs for climbable high-area passes (Ground_rocks cols 18-19, rows 47-49)
        {"n": "UW_STAIRS_TL", "src": "gr", "c": 18, "r": 47},
        {"n": "UW_STAIRS_TR", "src": "gr", "c": 19, "r": 47},
        {"n": "UW_STAIRS_ML", "src": "gr", "c": 18, "r": 48},
        {"n": "UW_STAIRS_MR", "src": "gr", "c": 19, "r": 48},
        {"n": "UW_STAIRS_BL", "src": "gr", "c": 18, "r": 49},
        {"n": "UW_STAIRS_BR", "src": "gr", "c": 19, "r": 49},
    ]

    for t in uw_tile_cells:
        im = img_uw_wc if t["src"] == "wc" else img_uw_gr
        idx = get_index_array(im, t["c"] * TILE, t["r"] * TILE, TILE, TILE)
        add_sprite(t["n"], idx, TILE, TILE, 0, 0)
    print(f"  uw tiles    {len(uw_tile_cells)} curated cells baked")

    # Underworld curated props from Objects_separately/
    uw_objects_dir = os.path.join(ASSETS, "Underworld2", "PNG", "Objects_separately")
    uw_objects = [
        {"n": 'UW_TREE_1',     "f": 'Dead_tree_shadow1_1.png'},
        {"n": 'UW_TREE_2',     "f": 'Dead_tree_shadow1_2.png'},
        {"n": 'UW_TREE_3',     "f": 'Tree_shadow1_1.png'},
        {"n": 'UW_PINE_1',     "f": 'Broken_tree_shadow1_4.png'},
        {"n": 'UW_PINE_2',     "f": 'Broken_tree_shadow1_6.png'},
        {"n": 'UW_PINE_3',     "f": 'Broken_tree_shadow1_7.png'},
        {"n": 'UW_BUSH_1',     "f": 'Thorn_plant_shadow1_3.png'},
        {"n": 'UW_BUSH_2',     "f": 'Thorn_plant_shadow1_2.png'},
        {"n": 'UW_BUSH_3',     "f": 'Thorn_plant_shadow1_1.png'},
        {"n": 'UW_LOG_1',      "f": 'Broken_tree_shadow1_4.png'},
        {"n": 'UW_LOG_2',      "f": 'Broken_tree_shadow1_5.png'},
        {"n": 'UW_LOG_3',      "f": 'Broken_tree_shadow1_6.png'},
        {"n": 'UW_LOG_4',      "f": 'Broken_tree_shadow1_7.png'},
        {"n": 'UW_ROCKPROP_1', "f": 'Rock_shadow1_1.png'},
        {"n": 'UW_ROCKPROP_2', "f": 'Rock_shadow1_2.png'},
        {"n": 'UW_ROCKPROP_3', "f": 'Rock_shadow1_3.png'},
        {"n": 'UW_STONE_1',    "f": 'Grave_shadow1_1.png'},
        {"n": 'UW_STONE_2',    "f": 'Grave_shadow1_2.png'},
        {"n": 'UW_STONE_3',    "f": 'Grave_shadow1_3.png'},
        {"n": 'UW_CRYSTAL_1',  "f": 'Crystal_shadow1_1.png'},
        {"n": 'UW_CRYSTAL_2',  "f": 'Crystal_shadow1_2.png'},
        {"n": 'UW_CRYSTAL_3',  "f": 'Crystal_shadow1_3.png'},
        {"n": 'UW_CRYSTAL_4',  "f": 'Crystal_shadow1_4.png'},
        {"n": 'UW_TUFT_1',     "f": 'Bones_shadow1_2.png'},
        {"n": 'UW_TUFT_2',     "f": 'Bones_shadow1_18.png'},
        {"n": 'UW_TUFT_3',     "f": 'Bones_shadow1_16.png'},
        {"n": 'UW_TUFT_4',     "f": 'Bones_shadow1_5.png'},
        {"n": 'UW_REED_1',     "f": 'Bones_shadow1_1.png'},
        {"n": 'UW_REED_2',     "f": 'Bones_shadow1_3.png'},
        {"n": 'UW_REED_3',     "f": 'Rock_shadow1_4.png'},
    ]
    for o in uw_objects:
        p = os.path.join(uw_objects_dir, o["f"])
        im = Image.open(p).convert("RGBA")
        box = get_opaque_box(im, 0, 0, im.width, im.height)
        if box is None:
            raise RuntimeError(f"Underworld object {o['f']} is fully transparent")
        bx, by, bw, bh = box
        idx = get_index_array(im, bx, by, bw, bh)
        add_sprite(o["n"], idx, bw, bh, bw // 2, bh)
    print(f"  uw objects  {len(uw_objects)} decorations baked")

    # Dimensional Portal (6 frames, 32x32)
    portal_png = os.path.join(ASSETS, "Portal", "Dimensional_Portal.png")
    img_portal = Image.open(portal_png).convert("RGBA")
    portal_cell = 32
    portal_names = ['UW_PORTAL_A', 'UW_PORTAL_B', 'UW_PORTAL_C', 'UW_PORTAL_D', 'UW_PORTAL_E', 'UW_PORTAL_F']
    for f in range(6):
        pc = f % 3
        pr = f // 3
        box = get_opaque_box(img_portal, pc * portal_cell, pr * portal_cell, portal_cell, portal_cell)
        if box is None:
            raise RuntimeError(f"Portal frame {f} is empty")
        bx, by, bw, bh = box
        idx = get_index_array(img_portal, pc * portal_cell + bx, pr * portal_cell + by, bw, bh)
        add_sprite(portal_names[f], idx, bw, bh, bw // 2, bh)
    print("  portal      6 frames baked")

    # --- Lumiara (biome 3) ---
    lum_dir = os.path.join(ASSETS, "Lumiara")
    lum_top_down_dir = os.path.join(lum_dir, "TopDown")
    img_lum_cobble = Image.open(os.path.join(lum_top_down_dir, "tileset_grass_to_cobblestone_16x16.png")).convert("RGBA")
    img_lum_water  = Image.open(os.path.join(lum_top_down_dir, "tileset_water_to_grass_16x16.png")).convert("RGBA")
    img_lum_chasm  = Image.open(os.path.join(lum_top_down_dir, "tileset_chasm_to_grass_16x16.png")).convert("RGBA")

    def get_lum_img(src):
        if src == 'cobble': return img_lum_cobble
        if src == 'water':  return img_lum_water
        return img_lum_chasm

    lum_tile_cells = [
        {"n": 'LUM_GRASS_A',       "src": 'cobble', "c": 0, "r": 0},
        {"n": 'LUM_GRASS_B',       "src": 'cobble', "c": 3, "r": 0},
        {"n": 'LUM_GRASS_C',       "src": 'cobble', "c": 0, "r": 3},
        {"n": 'LUM_GRASS_D',       "src": 'cobble', "c": 3, "r": 3},
        {"n": 'LUM_COBBLE_A',      "src": 'cobble', "c": 1, "r": 2},
        {"n": 'LUM_COBBLE_B',      "src": 'cobble', "c": 2, "r": 2},
        {"n": 'LUM_GRASSEDGE',     "src": 'cobble', "c": 2, "r": 0},
        {"n": 'LUM_OLIVEEDGE',     "src": 'cobble', "c": 1, "r": 3},
        {"n": 'LUM_WATER_A',       "src": 'water',  "c": 2, "r": 1},
        {"n": 'LUM_WATER_B',       "src": 'water',  "c": 1, "r": 1},
        {"n": 'LUM_WATER_C',       "src": 'water',  "c": 2, "r": 2},
        {"n": 'LUM_WATER_D',       "src": 'water',  "c": 3, "r": 1},
        {"n": 'LUM_WBORDER_NW',    "src": 'water',  "c": 0, "r": 0},
        {"n": 'LUM_WBORDER_N',     "src": 'water',  "c": 0, "r": 2},
        {"n": 'LUM_WBORDER_NE',    "src": 'water',  "c": 3, "r": 0},
        {"n": 'LUM_VOID_A',        "src": 'chasm',  "c": 2, "r": 1},
        {"n": 'LUM_VOID_B',        "src": 'chasm',  "c": 1, "r": 1},
        {"n": 'LUM_VOIDBORDER_NW', "src": 'chasm',  "c": 0, "r": 0},
        {"n": 'LUM_VOIDBORDER_N',  "src": 'chasm',  "c": 3, "r": 0},
        {"n": 'LUM_VOIDBORDER_NE', "src": 'chasm',  "c": 3, "r": 3},
    ]

    lum_objects = [
        {"n": 'LUM_TREE',      "f": os.path.join('TopDown', 'topdown_dream_tree.png')},
        {"n": 'LUM_MONOLITH',  "f": os.path.join('TopDown', 'topdown_mana_monolith.png')},
        {"n": 'LUM_BUSH',      "f": 'flora_purple_mushrooms.png'},
        {"n": 'LUM_MUSHROOM',  "f": 'flora_crystal_flower.png'},
        {"n": 'LUM_BENCH',     "f": 'stone_bench_mossy.png'},
        {"n": 'LUM_ARCHWAY',   "f": 'archway_ruined_runic.png'},
        {"n": 'LUM_STATUE',    "f": 'statue_guardian_gargoyle.png'},
        {"n": 'LUM_CHEST',     "f": 'runic_chest.png'},
        {"n": 'LUM_URN',       "f": 'relic_urn.png'},
        {"n": 'LUM_SIGNPOST',  "f": 'signpost_wayfinding.png'},
        {"n": 'LUM_LANTERN',   "f": 'lantern_post_purple.png'},
        {"n": 'LUM_BANNER',    "f": 'banner_faded_kingdom.png'},
        {"n": 'LUM_JELLYFISH', "f": 'fauna_dream_jellyfish.png'},
        {"n": 'LUM_MANTA',     "f": 'fauna_sky_manta.png'},
        {"n": 'LUM_FOX',       "f": 'fauna_spirit_fox.png'},
        {"n": 'LUM_STAG',      "f": 'fauna_star_stag.png'},
        {"n": 'LUM_PORTAL',    "f": os.path.join('TopDown', 'topdown_dreamgate_portal.png')},
    ]

    # Ground calming and histogram collection
    lum_hist = {}
    lum_tile_toned = {}

    def tone_lum_cell(im, x0, y0, w, h):
        px = im.load()
        r_arr = [float(px[x0 + x, y0 + y][0]) for y in range(h) for x in range(w)]
        g_arr = [float(px[x0 + x, y0 + y][1]) for y in range(h) for x in range(w)]
        b_arr = [float(px[x0 + x, y0 + y][2]) for y in range(h) for x in range(w)]
        a_arr = [px[x0 + x, y0 + y][3] for y in range(h) for x in range(w)]

        # 3x3 smooth
        def smooth_chan(src):
            dst = [0.0] * (w * h)
            for y in range(h):
                for x in range(w):
                    s = 0.0
                    cnt = 0
                    for dy in (-1, 0, 1):
                        ny = y + dy
                        if 0 <= ny < h:
                            for dx in (-1, 0, 1):
                                nx = x + dx
                                if 0 <= nx < w:
                                    s += src[ny * w + nx]
                                    cnt += 1
                    dst[y * w + x] = src[y * w + x] * (1.0 - LUM_TILE_SMOOTH) + (s / cnt) * LUM_TILE_SMOOTH
            return dst

        r_arr = smooth_chan(r_arr)
        g_arr = smooth_chan(g_arr)
        b_arr = smooth_chan(b_arr)

        # Flatten toward mean of opaque pixels
        mr = sum(r for i, r in enumerate(r_arr) if a_arr[i] >= 128)
        mg = sum(g for i, g in enumerate(g_arr) if a_arr[i] >= 128)
        mb = sum(b for i, b in enumerate(b_arr) if a_arr[i] >= 128)
        mn = sum(1 for a in a_arr if a >= 128)
        if mn > 0:
            mr /= mn; mg /= mn; mb /= mn
            f = LUM_TILE_FLATTEN
            r_arr = [r * (1.0 - f) + mr * f for r in r_arr]
            g_arr = [g * (1.0 - f) + mg * f for g in g_arr]
            b_arr = [b * (1.0 - f) + mb * f for b in b_arr]

        # Dim and Desat
        res = []
        for i in range(w * h):
            r, g, b = r_arr[i], g_arr[i], b_arr[i]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            vr = (lum + (r - lum) * LUM_TILE_DESAT) * LUM_TILE_DIM
            vg = (lum + (g - lum) * LUM_TILE_DESAT) * LUM_TILE_DIM
            vb = (lum + (b - lum) * LUM_TILE_DESAT) * LUM_TILE_DIM
            ir = max(0, min(255, round(vr)))
            ig = max(0, min(255, round(vg)))
            ib = max(0, min(255, round(vb)))
            res.append((ir, ig, ib, a_arr[i]))
        return res

    for t in lum_tile_cells:
        im = get_lum_img(t["src"])
        cell_rgba = tone_lum_cell(im, t["c"] * TILE, t["r"] * TILE, TILE, TILE)
        lum_tile_toned[t["n"]] = cell_rgba
        for r, g, b, a in cell_rgba:
            if a >= 128:
                k = (r, g, b)
                lum_hist[k] = lum_hist.get(k, 0) + 1

    lum_obj_imgs = {}
    for o in lum_objects:
        p = os.path.join(lum_dir, o["f"])
        im = Image.open(p).convert("RGBA")
        lum_obj_imgs[o["n"]] = im
        box = get_opaque_box(im, 0, 0, im.width, im.height)
        if box is None:
            raise RuntimeError(f"Lumiara object {o['f']} is fully transparent")
        bx, by, bw, bh = box
        px = im.load()
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                p_col = px[x, y]
                if p_col[3] >= 128:
                    k = (p_col[0], p_col[1], p_col[2])
                    lum_hist[k] = lum_hist.get(k, 0) + 1

    # Median-cut quantization
    buckets = [list(lum_hist.items())]  # list of [( (r,g,b), count ), ...]
    while len(buckets) < LUM_PALETTE_BUDGET:
        best_idx = -1
        best_range = 0
        best_chan = 0  # 0=R, 1=G, 2=B
        for i, bk in enumerate(buckets):
            if len(bk) <= 1: continue
            r_min = min(e[0][0] for e in bk); r_max = max(e[0][0] for e in bk)
            g_min = min(e[0][1] for e in bk); g_max = max(e[0][1] for e in bk)
            b_min = min(e[0][2] for e in bk); b_max = max(e[0][2] for e in bk)
            rr, gr, br = r_max - r_min, g_max - g_min, b_max - b_min
            mx = max(rr, gr, br)
            if mx > best_range:
                best_range = mx
                best_idx = i
                best_chan = 0 if mx == rr else (1 if mx == gr else 2)
        if best_idx < 0:
            break
        bk = buckets.pop(best_idx)
        bk.sort(key=lambda e: e[0][best_chan])
        mid = len(bk) // 2
        buckets.append(bk[:mid])
        buckets.append(bk[mid:])

    lum_color_map = {}
    for bk in buckets:
        sr = sum(e[0][0] * e[1] for e in bk)
        sg = sum(e[0][1] * e[1] for e in bk)
        sb = sum(e[0][2] * e[1] for e in bk)
        sn = sum(e[1] for e in bk)
        if sn > 0:
            rep = (round(sr / sn), round(sg / sn), round(sb / sn))
            for e in bk:
                lum_color_map[e[0]] = rep

    for t in lum_tile_cells:
        cell_rgba = lum_tile_toned[t["n"]]
        idx = bytearray(TILE * TILE)
        for i, (r, g, b, a) in enumerate(cell_rgba):
            if a < 128:
                idx[i] = 0
            else:
                qr, qg, qb = lum_color_map[(r, g, b)]
                idx[i] = get_palette_index(qr, qg, qb)
        add_sprite(t["n"], idx, TILE, TILE, 0, 0)

    for o in lum_objects:
        im = lum_obj_imgs[o["n"]]
        box = get_opaque_box(im, 0, 0, im.width, im.height)
        bx, by, bw, bh = box
        idx = get_index_array(im, bx, by, bw, bh, tint=lambda r, g, b: lum_color_map[(r, g, b)])
        add_sprite(o["n"], idx, bw, bh, bw // 2, bh)
    print("  lum tiles & objects baked")

    # --- Character sheets (MUST STAY LAST) ---
    char_dir = os.path.join(ASSETS, "Character")
    char_sheets = [
        {"n": "CH_IDLE_DOWN",  "f": "idle_down.png"},
        {"n": "CH_IDLE_RIGHT", "f": "idle_right_down.png"},
        {"n": "CH_IDLE_UP",    "f": "idle_up.png"},
        {"n": "CH_IDLE_LEFT",  "f": "idle_left_down.png"},
        {"n": "CH_WALK_DOWN",  "f": "walk_down.png"},
        {"n": "CH_WALK_RIGHT", "f": "walk_right_down.png"},
        {"n": "CH_WALK_UP",    "f": "walk_up.png"},
        {"n": "CH_WALK_LEFT",  "f": "walk_left_down.png"},
    ]
    char_lowest_foot = -1
    for s in char_sheets:
        p = os.path.join(char_dir, s["f"])
        im = Image.open(p).convert("RGBA")
        for f in range(CHAR_FRAMES):
            box = get_opaque_box(im, f * CHAR_CELL_W, 0, CHAR_CELL_W, CHAR_CELL_H)
            if box is None:
                raise RuntimeError(f"Character frame {s['f']} {f} is empty")
            bx, by, bw, bh = box
            bottom = by + bh - 1
            if bottom > char_lowest_foot:
                char_lowest_foot = bottom
            if bottom >= CHAR_FOOT:
                raise RuntimeError(f"Character frame {s['f']} {f} foot on row {bottom} >= CHAR_FOOT {CHAR_FOOT}")
            idx = get_index_array(im, f * CHAR_CELL_W + bx, by, bw, bh)
            add_sprite(f"{s['n']}_{f}", idx, bw, bh, CHAR_CX - bx, CHAR_FOOT - by)
    print(f"  character   64 frames baked (lowest foot {char_lowest_foot})")

    # --- Write src/art_data.h ---
    print(f"bake.py: total sprites: {len(records)}, palette colors: {len(pal_list)}, total RLE bytes: {len(data_bytes)}")
    if len(pal_list) > 254:
        raise ValueError(f"Palette count {len(pal_list)} > 254 limit!")

    with open(OUT_FILE, "w", encoding="utf-8") as out:
        out.write("/* GENERATED by tools/bake.py from assets/. Do not edit by hand.\n")
        out.write(" *\n")
        out.write(" * One global palette; index 0 is transparent in every sprite.\n")
        out.write(" * RLE control byte C:\n")
        out.write(" *     C <  0x80  -> RUN     of (C + 1) px of the following index\n")
        out.write(" *     C >= 0x80  -> LITERAL of ((C & 0x7F) + 1) px, then that many indices\n")
        out.write(" * Row-major over the sprite box; runs may cross row boundaries.\n")
        out.write(" *\n")
        out.write(" * Anchors: tiles (0,0) top-left; decorations bottom-centre of the opaque\n")
        out.write(" * box; character frames cell-relative to (24, 44) of their 48x64 cell.\n")
        out.write(" */\n")
        out.write("#ifndef WAYFARER_ART_DATA_H\n")
        out.write("#define WAYFARER_ART_DATA_H\n\n")
        out.write(f"#define ART_SPRITE_COUNT {len(records)}\n")
        out.write(f"#define ART_CHAR_CX      {CHAR_CX}\n")
        out.write(f"#define ART_CHAR_FOOT    {CHAR_FOOT}\n")
        out.write(f"#define ART_TILE_PX      {TILE}\n\n")
        out.write("enum {\n")
        for i, r in enumerate(records):
            out.write(f"    ART_{r['name']} = {i},\n")
        out.write("    ART_NONE = -1\n")
        out.write("};\n\n")
        out.write("typedef struct {\n")
        out.write("    unsigned short w, h;               /* sprite box */\n")
        out.write("    unsigned short anchor_x, anchor_y;  /* see header for the two conventions */\n")
        out.write("    unsigned int   data_off, data_len;  /* bytes into ART_DATA */\n")
        out.write("} ArtSprite;\n\n")
        out.write(f"static const ArtSprite ART_SPRITES[ART_SPRITE_COUNT] = {{\n")
        for r in records:
            out.write(f"    {{ {r['w']:3d}, {r['h']:3d}, {r['ax']:3d}, {r['ay']:3d}, {r['off']:6d}, {r['len']:5d} }}, /* {r['name']} */\n")
        out.write("};\n\n")

        # Tileset grid -> sprite id
        out.write("/* Tileset grid -> sprite id, ART_NONE where the source cell is empty. The\n")
        out.write(" * enum order skips empty cells, so index != row*8+col; this is the only\n")
        out.write(" * thing that knows where a tile sits in the authored sheet. */\n")
        out.write(f"#define ART_TILE_COLS {TILES_COLS}\n")
        out.write(f"#define ART_TILE_ROWS {TILES_ROWS}\n")
        out.write(f"static const short ART_TILE_AT[ART_TILE_ROWS][ART_TILE_COLS] = {{\n")
        for r in range(TILES_ROWS):
            row_strs = [f"{tile_at[r][c]:4d}" for c in range(TILES_COLS)]
            out.write(f"    {{{', '.join(row_strs)} }}, /* row {r} */\n")
        out.write("};\n\n")

        # Palette
        out.write(f"#define ART_PAL_N     {len(pal_list)}\n")
        out.write(f"#define ART_PAL_BYTES {len(pal_list) * 3}\n")
        out.write(f"static const unsigned char ART_PAL[ART_PAL_BYTES] = {{\n")
        for i, c in enumerate(pal_list):
            if i % 8 == 0:
                out.write("    ")
            out.write(f"{c[0]:3d}, {c[1]:3d}, {c[2]:3d}, ")
            if (i + 1) % 8 == 0:
                out.write("\n")
        if len(pal_list) % 8 != 0:
            out.write("\n")
        out.write("};\n\n")

        # Data
        out.write(f"#define ART_DATA_BYTES {len(data_bytes)}\n")
        out.write(f"static const unsigned char ART_DATA[ART_DATA_BYTES] = {{\n")
        for i, b in enumerate(data_bytes):
            if i % 16 == 0:
                out.write("    ")
            out.write(f"{b:3d},")
            if (i + 1) % 16 == 0:
                out.write("\n")
        if len(data_bytes) % 16 != 0:
            out.write("\n")
        out.write("};\n\n")
        out.write("#endif /* WAYFARER_ART_DATA_H */\n")

    print(f"bake.py: successfully wrote {OUT_FILE}")

if __name__ == "__main__":
    main()
