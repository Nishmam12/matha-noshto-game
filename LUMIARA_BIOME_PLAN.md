# Lumiara (The Dream Realm) Biome Integration Plan

This document outlines the architecture, asset pipeline, engine integration, and verification plan for adding the **Lumiara (The Dream Realm)** biome into **Wayfarer (Top-Down)** as **Area 3 / Biome 2** (`BIOME_LUMIARA`).

---

## 1. Architectural Invariants & Constraints

1. **Size Gate (< 1,440,000 bytes)**:
   - Current release binary size: ~741 KB.
   - All baked Lumiara tiles, props, and code must comfortably fit within the remaining headroom.
2. **Single Global Palette (< 254 colors)**:
   - All graphics share one insertion-ordered palette so `fogpal[32][FOGPAL_N]` works with zero per-draw fog calculations.
   - Current palette: ~113 colors.
   - Projected Lumiara additions: ~35–50 colors.
   - Total projected palette: ~150–165 colors (well under the 254 limit).
3. **Collision vs Render Invariant**:
   - `tile_blocked` inspects **only** `solid[][]`, `regions[].terrain`, and the ability mask.
   - Biome ground types, props, and canopy are strictly render-only.
4. **Single Translation Unit**:
   - All runtime game code stays in [`src/main.c`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/main.c).
   - Generated art stays in [`src/art_data.h`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/art_data.h) (built via [`tools/bake.ps1`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/tools/bake.ps1)).
5. **32-bit Restoration State**:
   - Area 1 (Forest): Collectibles 0–9 (Bits 0–9)
   - Area 2 (Underworld): Collectibles 10–19 (Bits 10–19)
   - Area 3 (Lumiara): Collectibles 20–29 (Bits 20–29)
   - Total: 30 of 32 bits used in `Uint32` bitmask.

---

## 2. Phase Breakdown

### Phase 1: Art Pipeline & Baking ([`tools/bake.ps1`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/tools/bake.ps1))

- **Top-Down 16×16 Ground Autotiling Sheets**:
  - [`assets/Lumiara/TopDown/tileset_chasm_to_grass_16x16.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Lumiara/TopDown/tileset_chasm_to_grass_16x16.png) (Void Chasm -> Dream Grass)
  - [`assets/Lumiara/TopDown/tileset_grass_to_cobblestone_16x16.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Lumiara/TopDown/tileset_grass_to_cobblestone_16x16.png) (Dream Grass -> Cobblestone Path)
  - [`assets/Lumiara/TopDown/tileset_water_to_grass_16x16.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Lumiara/TopDown/tileset_water_to_grass_16x16.png) (Bioluminescent Cyan Water -> Dream Grass)
- **Top-Down Landmark Entities & Structures**:
  - `topdown_dreamgate_portal.png` (Dreamgate Portal with bottom-center anchor)
  - `topdown_dream_tree.png` (Canopy Tree)
  - `topdown_mana_monolith.png` (Mana Crystal Pedestal)
- **Curated Props & Decor** ([`assets/Lumiara/`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Lumiara)):
  - `flora_crystal_flower.png` & `flora_purple_mushrooms.png` (Understorey)
  - `lantern_post_purple.png` & `signpost_wayfinding.png` (Wayfinding)
  - `runic_chest.png` & `relic_urn.png` (Collectibles & Relics)
  - `stone_bench_mossy.png` & `statue_guardian_gargoyle.png` (Ruins)
  - `banner_faded_kingdom.png` & `archway_ruined_runic.png` (Landmarks)
- **Ethereal Fauna**:
  - `fauna_spirit_fox.png`, `fauna_star_stag.png`, `fauna_dream_jellyfish.png`, `fauna_sky_manta.png`

---

### Phase 2: Biome System in Engine ([`src/main.c`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/main.c))

- **Biome Definition**:
  ```c
  enum {
      BIOME_FOREST     = 0,
      BIOME_UNDERWORLD = 1,
      BIOME_LUMIARA    = 2,
      BIOME_COUNT      = 3
  };
  ```

- **Area-Specific Tile Tables**:
  - Ground Base Fills:
    - Forest: `tile_grass_base`, `tile_dirt_fill`, `tile_water_fill`, `tile_rock_fill`
    - Underworld: `tile_uw_ground_base`, `tile_uw_dirt_fill`, `tile_uw_acid_fill`, `tile_uw_rock_fill`
    - Lumiara: `tile_lum_ground_base`, `tile_lum_dirt_fill`, `tile_lum_water_fill`, `tile_lum_rock_fill`
  - Autotile Overlay Edges:
    - Lumiara: `tile_lum_grass_edge`, `tile_lum_water_edge`, `tile_lum_cobble_edge`, `tile_lum_rock_ring`

- **Biome-Specific Prop Placement (`prop_art`)**:
  - `prop_art[BIOME_LUMIARA][PROP_COUNT]`:
    - Canopy: Bioluminescent Dream Trees
    - Understorey: Crystal Flowers, Glowing Purple Mushrooms
    - Boulders/Rock: Mana Crystals, Ruined Pedestals
    - Decor: Runic Chests, Urns, Statues, Lanterns, Benches

---

### Phase 3: Area Progression & Dimensional Portals

- **Area State**:
  - `Game` struct tracks current area (`g->area = 1, 2, or 3`).
  - Area 1 (Forest): Collectibles 0–9
  - Area 2 (Underworld): Collectibles 10–19
  - Area 3 (Lumiara): Collectibles 20–29
- **Portal Transitions**:
  - Area 1 Portal -> Area 2 (Underworld seed: `seed ^ 0xDEADBEEF`)
  - Area 2 Portal -> Area 3 (Lumiara seed: `seed ^ 0xCAFEBABE`)
- **Save / Load Multi-Area Support**:
  - Byte 3 stores `SAVE_AREA` (1, 2, or 3).
  - Preserves 32-bit restoration mask across all realms.

---

### Phase 4: Audio / Softsynth Atmosphere

- **Harmonic Progression for Lumiara** (`LAYER_CFG_TABLE[BIOME_LUMIARA]`):
  - Layer 0 (Drone): Ethereal high celestial fifths (E4/B4 airy synth drone).
  - Layers 1–4 (Collectibles unlocked): Sparkling arpeggios, crystalline bell harmonics, and dream melodies.

---

### Phase 5: Verification & Self-Tests

- **`--sprite-test`**: Validate all baked Lumiara tiles and props decode correctly and maintain anchor invariants.
- **`--tile-test`**: Assert opacity of all Lumiara base fill tiles.
- **`--save-test`**: Assert 3-area roundtrip serialization (Area 1, Area 2, Area 3) and delta replay.
- **`--reach-test` & `--play-test`**: Assert 20/20 headless playthroughs solve all three realms.
- **Size Assertion**: Ensure final binary remains < 1,440,000 bytes with zero warnings (`-Werror`).

---

## 3. Implementation Checklist

- [x] Update [`tools/bake.ps1`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/tools/bake.ps1) with Lumiara tile rects and prop definitions. (Also added a median-cut palette quantizer - the source art is painterly, not flat pixel art, and blew the 254-colour budget by 4x before quantization.)
- [x] Run `powershell -File tools\bake.ps1` to generate new [`src/art_data.h`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/art_data.h).
- [x] Add `BIOME_LUMIARA` tile tables, prop selector, and biome rendering dispatch in [`src/main.c`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/main.c). (No new `TileSet` field or `GT_*` value: cobblestone rides the existing `dirt_fill` slot and the Void Chasm rides `rock_ring`/`rock_fill`, the same re-skinning Underworld already established.)
- [x] Update Area 2 portal completion to route to Area 3 (Lumiara). (Generalized `game_transition_to_area2` into `game_transition_to_area(g, sc, next_area)` and fixed `game_save`'s keep_mask, which a naive 2-area-style extension would have corrupted for whichever area is neither being left nor entered.)
- [x] Configure `LAYER_CFG_TABLE[BIOME_LUMIARA]` softsynth audio layers.
- [x] Update tests (`--tile-test`, `--sprite-test`, `--reach-test`, `--play-test`, plus `--save-test` area-3 round trip and new negative controls).
- [x] Run `powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1` to ensure 100% pass rate. (17/17 categories PASS, including 90 reach-test seeds and 60/60 play-test completions across all three areas.)
- [x] Build release executable via `powershell -ExecutionPolicy Bypass -File .\build.ps1`. (797,184 bytes, 642,816 under the 1,440,000 target.)
