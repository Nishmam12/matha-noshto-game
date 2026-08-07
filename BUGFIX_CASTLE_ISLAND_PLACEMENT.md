# Bug Fix: Castle Island Entity Placement

**Date:** 2026-08-07  
**Severity:** Required — entities placed on water-locked area unreachable by player  
**Branch:** `feat/phase-11-ship-complete`

---

## The Bug

A required soul (entity 18) was being placed on the **Aetherhold castle island** at tile `(107, 23)` — an area designed to be water-locked, reachable only after restoring the Dream Well's Soul and opening the causeway.

The completion verifier (`world_solvable`) uses the region graph which does **not** model the causeway gate. Entities on the island read as "reachable" to the verifier while the player cannot physically reach them in actual play.

### Root Cause

Two issues combined:

1. **Ocean buffer too narrow:** `castle_apply_layout` clears an ocean buffer from `CASTLE_RESERVE_X0-2` to `CASTLE_RESERVE_X0+W+2` (x=108..153), but the island silhouette (`castle_left[]`) extends to x=99 — tiles at x=99..107 were never cleared.

2. **Island loop too narrow:** The island tile-setting loop iterates x from `CASTLE_RESERVE_X0` to `CASTLE_RESERVE_X0+W` (110..151), missing the same x=99..109 range where the silhouette bulges outward.

This left generated terrain (from `gen_sector`) intact in those columns, potentially creating a land bridge between mainland and island. The island merged into region 11 (mainland), allowing `pick_tile_in_region` to place required entities there.

### Diagnosis Steps

1. Added `--dump-ents` diagnostic flag to print entity positions, regions, solidity, castle membership, and walk-BFS distances from spawn with abilities=0 and abilities=0xFF.

2. Ran on seed 1: found ent 18 (SOUL) at `(107, 23)` with `castle 1`, in region 11 (same as mainland). Walk BFS confirmed tile-reachable at dist 53 via an unintended path.

3. Grid screenshot confirmed castle island (top-right outline) visible but visually disconnected from mainland.

4. Verified all 19 entities are tile-walk-reachable with full abilities, confirming the issue is placement on an area whose access gate isn't modeled by the verifier.

### The Fix

Modified `pick_tile_in_region` (`src/main.c` ~line 2159) to skip castle-island tiles when selecting entity positions:

```c
/* The Aetherhold island is water-locked: its only entrance is the
 * causeway, which opens when the Well Soul is restored. That gate is
 * not modelled by world_solvable's region graph, so a required entity
 * placed on the island reads as "reachable" to the verifier while the
 * player cannot actually get to it. Never place required content there. */
if (castle_island_tile(x, y))
    continue;
```

This single change prevents any required entity (fragments, souls, ability grants) from being placed on the island. The island remains an explorable bonus area for Phase 13 expansion content.

### Verification

All tests pass after the fix:

| Test | Seeds | Result |
|------|-------|--------|
| `--reach-test` | 50 | PASS — all 19/19 entities reachable |
| `--play-test` | 50 | PASS — all seeds complete |
| `--gating-test` | 30 | PASS — walk == graph, gating blocks |
| `--aether-test` | 20 | PASS — causeway closed before key |

**Seed 1 before fix:** ent 18 at `(107, 23)` on castle island  
**Seed 1 after fix:** ent 18 at `(20, 46)` off island, walk-reachable

### Notes for Future Sessions

- The island silhouette (`castle_left[]` / `castle_right[]`) extends beyond `CASTLE_RESERVE_X0`. This creates a minor design inconsistency: the island has a partial land bridge via the approach shelf. Not critical for completion but worth fixing if the island should be truly water-locked.
- The `--dump-ents` diagnostic can be removed once no longer needed. It adds ~0 bytes to shipping build (guarded by `WAYFARER_SELFTEST`).
- No changes to `art_data.h` or build size impact.
