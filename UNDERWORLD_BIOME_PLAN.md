# Underworld Biome Integration Plan

This plan details the end-to-end design and implementation for integrating the **Underworld** biome into Wayfarer (Top-Down), fulfilling the transition from Area 1 (Fantasy Forest) to Area 2 (Underworld).

---

## 1. Architectural Invariants & Constraints

1. **Size Gate (< 1,440,000 bytes)**:
   - Current release binary size: ~741 KB.
   - Headroom: ~698 KB.
   - All baked Underworld tiles, props, and code must comfortably fit within 50–70 KB of `.rodata`.
2. **Single Global Palette (< 254 colors)**:
   - All graphics share one insertion-ordered palette so `fogpal[32][FOGPAL_N]` works without runtime per-draw fog calculations.
   - Existing Forest + Character: 65 colors.
   - Underworld core tiles (`Ground_rocks`, `Water_coasts`, `Details`): ~45 new colors.
   - Curated Underworld props: ~30–50 new colors.
   - Total projected palette size: ~140–160 colors (strictly below the 254-color limit).
3. **Collision vs Render Invariant**:
   - `tile_blocked` inspects **only** `solid[][]`, `regions[].terrain`, and the ability mask.
   - Biome ground types, props, and canopy are strictly render-only.
4. **Single Translation Unit**:
   - All runtime game code stays in [`src/main.c`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/main.c).
   - Generated art stays in [`src/art_data.h`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/art_data.h) (built via [`tools/bake.ps1`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/tools/bake.ps1)).

---

## 2. Phase Breakdown

### Phase 1: Art Pipeline & Baking ([`tools/bake.ps1`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/tools/bake.ps1))
- **Tileset Extraction**:
  - Extract 16×16 Underworld ground tiles from [`assets/Underworld/PNG/Ground_rocks.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Underworld/PNG/Ground_rocks.png) (corrupted dirt, dark rock, stone rubble, cliff edges).
  - Extract 16×16 Underworld water/acid tiles and coastlines from [`assets/Underworld/PNG/Water_coasts.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Underworld/PNG/Water_coasts.png).
  - Extract 16×16 ground details from [`assets/Underworld/PNG/Details.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Underworld/PNG/Details.png).
- **Curated Decorations & Props**:
  - Curate a cohesive set of Underworld decorations from [`assets/Underworld/PNG/Objects_separately/`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Underworld/PNG/Objects_separately):
    - Dead & broken trees (canopy & stands).
    - Tombstones & graves (scattered ruins).
    - Bone piles & skeleton debris.
    - Crystal formations (glowing focal points).
    - Thorn plants & dark shrubs (understorey).
    - Ruins & skull arches.
  - Apply ground-contact anchor convention (bottom-center of trimmed opaque box).
- **Portal Sprite**:
  - Bake portal sprite(s) from [`assets/Portal/Dimensional_Portal.png`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/assets/Portal/Dimensional_Portal.png) with bottom-center anchor.

---

### Phase 2: Biome System in Engine ([`src/main.c`](file:///c:/Users/afnan/Desktop/Projects/Wayfarer/src/main.c))
- **Biome Definition**:
  ```c
  enum {
      BIOME_FOREST = 0,
      BIOME_UNDERWORLD = 1,
      BIOME_COUNT
  };
  ```
- **Area-Specific Tile Tables**:
  - Ground Base Fills:
    - Forest: `tile_grass_base`, `tile_dirt_fill`, `tile_water_fill`, `tile_rock_fill`.
    - Underworld: `tile_uw_ground_base`, `tile_uw_dirt_fill`, `tile_uw_acid_fill`, `tile_uw_rock_fill`.
  - Autotile Overlay Edges:
    - Forest: grass-over-dirt, olive-over-grass, water-rim, rock-ring.
    - Underworld: corrupted-ground-over-dirt, dark-rock-ring, acid-coast-rim.
- **Biome-Specific Prop Placement (`prop_at`)**:
  - Forest: Oak trees, pines, bushes, logs, forest rocks, mushrooms.
  - Underworld: Dead trees, broken trunks, thorn plants, crystal clusters, graves, bone piles.

---

### Phase 3: Area Progression & Portal Activation
- **Area State**:
  - `Game` struct tracks current area (`g->area = 1` or `g->area = 2`).
  - Area 1 (Forest) holds collectibles 0–9 (7 fragments, 3 souls).
  - Area 2 (Underworld) holds collectibles 10–19 (7 fragments, 3 souls).
- **Dimensional Portal Entity**:
  - Placed at the edge of Area 1 upon generation.
  - When `area_complete(g)` (all 10 Area 1 items restored), the portal lights up.
  - Stepping onto the portal triggers transition to Area 2 (Underworld seed: `seed ^ 0xDEADBEEF`).
- **Save / Load Multi-Area Support**:
  - Save format byte 3 stores `SAVE_AREA` (1 or 2).
  - 32-bit entity restoration mask preserves both Forest and Underworld restoration state seamlessly.

---

### Phase 4: Audio / Softsynth Atmosphere
- In Area 2 (Underworld), modulate the real-time softsynth:
  - Base drone pitch shifted to lower subterranean register (e.g. A1 / D2).
  - Darker harmonic progression for the 5 unlockable layers as Underworld memory fragments are restored.

---

### Phase 5: Verification & Self-Tests
- **`--sprite-test`**: Validate all baked Underworld tiles and props decode correctly and maintain anchor invariants.
- **`--tile-test`**: Assert opacity of all Underworld base fill tiles.
- **`--save-test`**: Assert multi-area roundtrip serialization (Area 1 vs Area 2) and delta replay.
- **`--reach-test` & `--play-test`**: Assert 20/20 headless playthroughs solve both Forest and Underworld maps.
- **Size Assertion**: Ensure final binary remains < 1,440,000 bytes with zero warnings (`-Werror`).

---

## 3. Implementation Order

1. Run `bake.ps1` expansion script to inspect, curate, and extract Underworld tile IDs and rects.
2. Update `tools/bake.ps1` and generate new `src/art_data.h`.
3. Update `src/main.c` with biome tile tables, prop selector, and biome rendering dispatch.
4. Add portal entity and area transition logic.
5. Update save/load and tests.
6. Verify via `./tools/run-tests.ps1` and build size check.
