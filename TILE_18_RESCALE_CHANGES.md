# TILE 18 Rescale & Minimap Optimization — Changes & Additions Summary

## Overview
This update introduces **TILE 18 Option B Rescaling** and **Minimap 1px/tile Optimization** for **Wayfarer**. The grid resolution was expanded from `108×104` to `144×138` with denser `TILE 18` tile rendering (36×18 px diamonds), increasing active world area by 77% while scaling village clusters, stack guards, and HUD elements proportionally.

---

## What Was Added & Changed

### 1. World Grid Rescaling (`TILE 18`) (`src/main.c`)
- **Expanded Grid Geometry**:
  - `TILE`: Reduced from 24 to 18 (diamonds resized from 48×24 px to 36×18 px).
  - `WORLD_W × WORLD_H`: Expanded from 108×104 to **144×138** (Overworld 80 rows, Void 8 rows, Dream Archipelago 50 rows).
  - Active world tile count increased by **+77%** (19,872 vs 11,232 tiles).
- **Proportional Scaling Updates**:
  - Village placement parameters, building density, and structure footprints scaled to match the expanded grid.
  - Stack guard memory safety limit updated from 400 KB to 700 KB.
  - `soul_bob` vertical displacement re-calibrated for TILE 18 geometry.

### 2. Minimap Optimization (`1px/tile`) (`src/main.c`)
- **Halved Minimap Dimensions**:
  - `MM_TILE`: Halved from 2 px/tile to **1 px/tile**.
  - Minimap screen surface reduced from 288×276 px to **144×138 px**, reducing memory bandwidth and cache footprint by 75%.
  - Entity marker sizes and legend offsets dynamically scaled relative to `MM_TILE`.

### 3. Ship-Critical Foundation (Phase 11 + Phase 10 Recap)
- **5-Layer Softsynth**: 5 procedural audio layers (Base, Strings, Pad, Bells, Voice of Souls) + synthesized SFX (Portal, Chime, Shard).
- **Full HUD**: Top-left counters, top-right minimap, bottom-center toasts, center win banner, bottom-right seed text, 91-glyph font.
- **Unified Interactions**: `try_interact()` E-key precedence (Portal → Shard → Soul/Fragment).
- **Ambient Motion**: Tree sway, water shimmer, waterfall fall-lines, chimney smoke, grass fireflies, soul bobbing.

---

## Build & Headroom Metrics
- **Executable Size**: `786,432 bytes` (**653,568 bytes headroom** under 1.44 MB ship target).
- **Performance**: Render mean **1.017 ms** (max 1.880 ms), 59.6 fps at 1920×1080.
- **Zero External Dependencies**: Verified via `nm` (0 external libraries: standalone Windows `.exe`).
- **Test Suite Status**: All automated test suites **PASS** (100/100 seeds clean).

---

## File Summary
| File | Status | Description |
|---|---|---|
| `src/main.c` | **Modified** | Rescaled grid to TILE 18 (144×138), updated stack guard to 700KB, halved minimap to 1px/tile. |
| `Handover.md` | **Modified** | Documented TILE 18 144×138 world scale, 1px minimap optimization, and updated architecture specs. |
| `PHASE_11_SHIP_CRITICAL_CHANGES.md` | **Maintained** | Detailed Phase 11 softsynth, HUD, font, and interaction fixes. |
| `TILE_18_RESCALE_CHANGES.md` | **New** | Complete overview of TILE 18 rescaling and 1px/tile minimap changes. |
