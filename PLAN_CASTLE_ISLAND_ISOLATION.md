# Plan — Full Geometry Isolation for Aetherhold Island

**Date:** 2026-08-08  
**Branch:** `feat/phase-13-aetherhold-relocation` (HEAD `d3e237c`, all fixes unstaged per request)  
**Source:** `BUGFIX_CASTLE_ISLAND_PLACEMENT.md` + Image 1 (seed 1 Fully Restored) + read-only probe 2026-08-08

---

## Context from Image 1

Window title `Wayfarer seed 1 fragments 14/14 souls 5/5 Fully Restored`, HUD `src/main.c:3586` shows `the land is whole`. Minimap `src/main.c:3741-3786` top-right dark `0x0c0c10` rock ring on `0x345f8a` ocean is the Aetherhold silhouette — healthy, no village regression visible. World density `WORLD_W 164 / OVERWORLD_H 91 / DREAM 60` `src/main.c:61-77` matches dense forest seen.

## Current Fix State (unstaged in `src/main.c`, not committed per instruction)

All isolation work is in the working tree only (`git status` → `M src/main.c`), not committed:

- `src/main.c:125-141` `castle_approach_tile` — shelf capped `101→97` (`tx 76..97`, `58..97`) with comment leaving ocean column `98` vs `castle_left[]:104-108` min `99` at rows 21-22. Fixes the "partial land bridge via approach shelf" noted in `BUGFIX_CASTLE_ISLAND_PLACEMENT.md:66-67`.
- `src/main.c:1302-1314` `castle_apply_layout` ocean buffer — `X0-2→X0-13` (`110-13=97` to `X0+W+2=153`, i.e. `97..153`) covering the `99..107` bulge missed before (`BUGFIX:19-21`). Comment notes `11 left of X0, 2-tile margin`.
- `src/main.c:1333-1345` landmass loop + `src/main.c:1372-1384` `castle_apply_heights` — widened from `X0..X0+W (110..151)` → `X0-13..X0+W+2 (97..153)` so `castle_island_tile:115-123` bulge is actually stamped as `SURF_ROCK/LAND` with correct heights.
- `src/main.c:2161-2184` `pick_tile_in_region` guard `if (castle_island_tile(x,y)) continue;` — the single-line verifier fix from `BUGFIX:39-47`, prevents required entities on the water-locked island whose causeway gate (`CASTLE_CAUSEWAY_Y 56 X0 94..108:88-90`) is not modelled by `world_solvable`.
- `src/main.c:11328-11383` `--dump-ents` diagnostic retained, unguarded (kept per your `keep --dump-ents` instruction; `BUGFIX:68` claims guarded but working tree is not).

**Remaining divergence:** `src/main.c:143-151` `castle_approach_path_tile` still `58..108` — 11 tiles beyond the shelf cap `97`. Harmless (only sets `w->path` decoration `src/main.c:1328` when `castle_approach_tile` is true) but inconsistent.

`BUGFIX_CASTLE_ISLAND_PLACEMENT.md` itself is untracked and now stale — it describes only the `pick_tile_in_region` fix and labels branch `feat/phase-11-ship-complete`.

---

## Plan — Verify Full Water-Lock Without Committing

### 1. Rebuild & regression suite

```powershell
.\build.ps1 -SelfTest
$ e = ".\build\wayfarer-selftest.exe"
& $e --reach-test  --seeds 50  # all 19/19 reachable, negative control PASS
& $e --gating-test --seeds 30  # walk == graph, gating blocks
& $e --aether-test --seeds 20  # causeway closed before Well Soul, opens after
& $e --play-test   --seeds 20  # headless to completion (50 ~2 min at 164×157, avoid Select-Object -Last piping delay)
```

Expect `PASS` on all — proves `solid`/`regions_build:1886` / `bfs_open:1843` isolation did not re-break `world_solvable`.

### 2. Geometry isolation proof (no commit)

```powershell
# 50-seed scan — expect 0/50 with castle 1
py -3 -c "import subprocess; print([s for s in range(1,51) if any('castle 1' in l for l in subprocess.check_output([r'build/wayfarer-selftest.exe','--dump-ents','--seed',str(s)], text=True).splitlines() if 'ent ' in l)])"

# Single-seed probes (region 255 == REGION_NONE:845 == isolated ocean/rock)
& $e --dump-ents --seed 1
# check: g.w.region[23][98]==255 solid 1 surf OCEAN, g.w.region[23][107]!=spawn_region, causeway (100,56) solid 1 closed at init
```

Previous run: `regions 16 spawn_region 0`, `ent 18` at `(48,32)` not `(107,23)`, `0/50` castle placements. Gap `(98,23)` `solid 1 surf 2`, island interior `region 255` (correct for closed causeway — `bfs_open` cannot seed island when ocean blocks, so `REGION_NONE`).

### 3. Visual audit (screenshots)

```powershell
& $e --frames 1 --seed 1 --shot overworld.png
# overhead of gap column
& $e --frames 1 --seed 1 --shot gap_98_23.png   # centre camera on (98,23)
# causeway states
& $e --frames 1 --seed 1 --shot causeway_closed.png
& $e --frames 1 --seed 1 --shards 6 --shot causeway_open.png
```

Criteria: minimap dark ring continuous except causeway `y56 x94..108`; one blue column at `98` between shelf `97` and bulge `99`; interior `SURF_LAND` vs edge `SURF_ROCK` (`src/main.c:1342`); causeway `solid 0 bridge 1` only when `has_castle_key` (Well Soul restored). Compare to `BUGFIX:29` grid screenshot.

### 4. Consistency fix — decision point

Cap `castle_approach_path_tile:143-151` from `58..108` → `58..97` to match `castle_approach_tile`. One-line, render-only. Leave diverging if path decoration beyond shelf is intentional — no collision effect either way.

### 5. Housekeeping

- Leave `src/main.c` unstaged (no commit to any branch — per your instruction).
- Keep `--dump-ents` in place, unguarded; decide before merge whether to guard with `WAYFARER_SELFTEST`.
- Leave `BUGFIX_CASTLE_ISLAND_PLACEMENT.md` untracked, or move to `docs/`/`design/` if retained.
- No `art_data.h` change; build stays `~846 KB` well under budget.

---

## Open Questions (for next session)

1. Include `castle_approach_path_tile` cap `108→97` or explicitly leave diverging for decoration?
2. Screenshots from keep `(129,14)` / interior courtyard needed to judge skyline readability at `TILE 18`, or only the isolation gap?
3. Keep `--dump-ents` unguarded vs move inside `#if WAYFARER_SELFTEST` before PR?
