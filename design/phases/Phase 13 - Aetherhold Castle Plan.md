---
tags: [design, phase, wayfarer, plan]
phase: 13
status: ready
updated: 2026-08-06
---

# Phase 13 — Aetherhold Castle Plan (Slice 1–2 Landing)

**Status:** READY for Slice 1–2. Spec is `[[Phase 13 - Aetherhold Castle]]` (PLANNED overall, two reference maps: Castle Island + Connecting Land). This plan slices the first landing — causeway gating + mainland watchtower key + outer courtyard ruins — so the gate is proven before any keep/dungeon lands. Dungeon (Slice 4) is deferred.

## Decisions locked for this landing

- **Unlock = new `castle_key`** at **mainland watchtower `Area 1`** (`~112,18`), `E` via `try_interact` (not `souls`/`frags` reuse). Total tracked IDs `14+5+1=20 ≤32`, `Uint32` mask safe. Major Memory stays future.
- **Placement = NE coast reserve** `x 92..144, y 8..42` (52×34) in overworld `80` tall — north-east corner, matches Connecting Land east coast path, leaves central `90×80` for `gen_sector` village + `dream 85..137` untouched. Bridge chokepoint at `(118,42) → (118,85)` spanning `GAP 5` water.
- **Scope = Slice 1–2 only.** Slice 1: mask+gate+key+test+save v2 (no art). Slice 2: outer courtyard art + storytelling. Keep/dungeon deferred.

## Global constraints (do not re-derive)

- `TILE 18`, `WORLD 144×138` already at `068fea8`. Do not change again in this phase.
- `tile_blocked` reads only `solid` + `regions[].terrain` (+ gate via `solid` reuse, not a third input) — `Handover.md:734` rule.
- `tools/bake.ps1 → src/art_data.h → wayfarer.exe`, no `SDL_image`/`SDL_ttf`/`SDL_mixer` at runtime, `nm` 1 symbol.
- Stack guard `700KB` at `src/main.c:959` holds `144×138` (~497KB World+Scratch). Reserve adds ~1.8KB.

## Slice 1 — Causeway + watchtower key (no art, vertical slice)

**Goal:** the castle is visible, unreachable by default, reachable after one `E` at the watchtower — proven by a test that would fail if the bridge ever touches the overworld.

**Concrete tasks, in order, with file refs:**

1. Define reservation and flag in `src/main.c:60` beside `WORLD_W`:
   - `#define CASTLE_RESERVE_X0 92` `Y0 8` `W 52` `H 34`, `Uint8 castle_mask[WORLD_H][WORLD_W]` as `static` mask (or `Uint8` bitmask), `Uint8 castle_key` + `castle_unlocked` derived `==1` on `Game`.
2. Carve the reserve in `game_init:1785` **before** `gen_sector:944` and `place_buildings:824`: fill reserve with flat meadow + coastline + cliff edge, mark `castle_mask` where handcrafted solid will live; `gen_sector`/`place_buildings` skip masked tiles.
3. Gate the causeway at `tile_blocked:1390`: if `castle_mask[ty][tx] & CASTLE_SOLID` and `!castle_unlocked` → blocked (treat as `solid`). Causeway bridge tile(s) are the only opening; `solid` cleared on unlock.
4. Place `castle_key` entity at `(112,18)` watchtower (hardcoded, not RNG), reachable after `Wade`/`Climb` already gates that forest — keep ability gating unchanged.
5. Wire `try_interact:3650` new branch `try_pick_castle_key` returning `4`, `SFX_CHIME`, toast `"a castle key is found"`, `hud.mm_dirty=1`. Order: `portal(1)→restore(2)→shard(3)→key(4)`.
6. Bump save to `v2` (`wayfarer.sav:29`): `magic WF v2`, `u8 castle_key` after `abilities`, `reserved` shift, validation-first load leaves live `Game` untouched on bad magic/version/OOB; `v1` loads with key `0`.
7. Add `--aether-test` beside `--shard-test:9503`: (a) closed: flood from spawn reaches `0` tiles beyond causeway, (b) open after `E` at watchtower: `>200` tiles beyond, (c) `tile_blocked` purity (`solid`/`terrain` only, mask goes through `solid`), (d) mask determinism (same seed → same mask, decoration via `tile_hash` may differ), (e) save round-trip `v1`+`v2`.
8. HUD: keep `PROMPT_LOCKED` padlock for the closed causeway (already in `src/main.c:966`), no new clutter per `Save and UI.md:22`.
9. Build → `786,944 → +~400B`, run full suite + `--aether-test`.

**Definition of done — Slice 1:**
- [ ] Castle visible from approach, not reachable while `castle_key==0` — `--aether-test` closed case green.
- [ ] `E` at watchtower picks up key, causeway opens, far side reachable — open case green.
- [ ] `tile_blocked` invariant survives (no third input).
- [ ] Save `v2` round-trip bit-identical, `v1` loads with key locked.
- [ ] Full suite green at `144×138`, render `<2ms`.

**Traps for Slice 1:**
- Reserve colliding with `gen_sector` — must be carved before generation, not patched after.
- Bridge touching overworld by one tile makes gate cosmetic — `--aether-test` closed case is the load-bearing check.
- New flag reusing `souls` would hide in `Handover.md:734` logic — new `Uint8` is explicit.

## Slice 2 — Outer Courtyard ruins (first art)

**Goal:** the courtyard reads as ruined and navigable from the causeway, storytelling without interaction.

**Concrete tasks:**

1. Author `assets/castle/wall_*.png` `rubble_*.png` `statue_*.png` 6–8 sprites, add `castle` to `$Categories` in `tools/bake.ps1:41`, bake → `src/art_data.h` `64→~72` records.
2. Populate courtyard from mask + `tile_hash` decoration (reuse `prop_at` thresholds), add 2–3 vignettes (fallen banners, burned carts, dead trees).
3. Verify by screenshot from `(118,42)` — keep dominates skyline, courtyard navigable; no new test.

**Definition of done — Slice 2:**
- [ ] Outer courtyard screenshotted, readable as ruins, keep visible from causeway.
- [ ] `nm` still clean, `build/` no shipped PNG, full suite still green.

**Still deferred after this landing:** Inner courtyard/keep verticality, dungeon modules (`--dungeon-test`), torch lighting, guardian enemies, Aetherhold fragments.

## Verification gate (landing)

- `--aether-test` (5 sub-cases) + full suite (`rng/iso/font/hud/fog/sprite/fade/rebuild/ground/motion/sector/portal/shard/save/land(20)/village(32)/play(50)/gating/reach/bridge/region/move/audio`) at `144×138`, `nm` 1 symbol, `build` only exes + `.last_size` + `wayfarer.sav` (29B), screenshots: approach with castle on horizon, closed vs open causeway, outer courtyard ruins.

## File map for this landing

- `src/main.c:60` tunables + `src/main.c:940` guard comment + `src/main.c:1390` `tile_blocked` + `src/main.c:1785` `game_init` + `src/main.c:3650` `try_interact` + `src/main.c:9503` `hud_selftest` area + save structs.
- `tools/bake.ps1` + `assets/castle/` + `src/art_data.h` (Slice 2).
- `design/phases/Phase 13 - Aetherhold Castle Plan.md` (this file), `Handover.md:1730` §11, `Phase Roadmap.md:24`.

## Evidence

Not yet built — plan ready 2026-08-06, Slice 1 next.
