---
tags: [design, phase, wayfarer, plan]
phase: 13
status: in_progress
updated: 2026-08-07
---

# Phase 13 — Aetherhold Castle Plan (Slice 1–2 Landing)

**Status:** Slice 1–2 IMPLEMENTED, then **relocated 2026-08-07** (see note). Spec is `[[Phase 13 - Aetherhold Castle]]` (IN PROGRESS overall, two reference maps: Castle Island + Connecting Land). The landing is causeway gating + outer courtyard composition using the supplied dark-fantasy pack. Dungeon (Slice 4) is deferred.

> **Everything under "Decisions locked" below is the 2026-08-06 design as originally built.**
> Same-day, 2026-08-07, it was superseded: the mainland-watchtower `castle_key` **item was removed**
> and the island **moved from the SE coast to a fixed top-right footprint**. Current facts, verified
> directly in `src/main.c`:
> - **Unlock** = restoring the Dream Well's Soul (`WELL_SOUL_IDX`) sets `has_castle_key` as a side
>   effect — there is no separate pickup, no watchtower, no `(88,59)` key tile any more.
> - **Placement** = top-right water-locked island, `CASTLE_RESERVE_X0 110`, `Y0 2`, `42×42`. The
>   causeway span is unchanged — `x 94..107, y 56` — it now reaches the top-right footprint instead
>   of the old SE one.
> - **World** is `164×157` (was `144×138`), same `TILE 18`.
>
> The rest of this file (task ordering, traps, verification approach) is still accurate in shape —
> only the coordinates and the unlock mechanism it references are stale. Read it for *how the
> landing was built*, not for the current key/position facts.

## Decisions locked for the 2026-08-06 landing (superseded — see note above)

- **Unlock = new `castle_key`** at **mainland watchtower `Area 1`** (`88,59`), `E` via `try_interact` (not `souls`/`frags` reuse). Total tracked IDs `14+5+1=20 ≤32`, `Uint32` mask safe. Major Memory stays future.
- **Placement = SE overworld coast** — fixed jagged island rows `35..76` (same `144×138` grid), above the Dream gap `80..84`. Mainland approach/watchtower sits west of it at `castle_key (88,59)`; the horizontal causeway is `x 94..107, y 56`. The Dream sector remains untouched.
- **Scope = Slice 1–2 only.** Slice 1: mask+gate+key+test+save v2 (no art). Slice 2: outer courtyard art + storytelling. Keep/dungeon deferred.

## Global constraints (do not re-derive)

- `TILE 18`, `WORLD 164×157` as of `eac01fd`/`95f108d` (2026-08-07; was `144×138` at `068fea8`). Do not change again in this phase.
- `tile_blocked` reads only `solid` + `regions[].terrain` (+ gate via `solid` reuse, not a third input) — `Handover.md:734` rule.
- `tools/bake.ps1 → src/art_data.h → wayfarer.exe`, no `SDL_image`/`SDL_ttf`/`SDL_mixer` at runtime, `nm` 1 symbol.
- Stack guard `700KB` at `src/main.c:959` holds `144×138` (~497KB World+Scratch). The fixed mask is computed from row spans; no large per-tile castle array is added.

## Slice 1 — Causeway + watchtower key (no art, vertical slice)

**Goal:** the castle is visible, unreachable by default, reachable after one `E` at the watchtower — proven by a test that would fail if the bridge ever touches the overworld.

**Concrete tasks, in order, with file refs:**

1. Define the southeast reservation and flag in `src/main.c:60` beside `WORLD_W`:
   - fixed jagged row spans `CASTLE_RESERVE_Y0 35` through `76`, `Uint8 has_castle_key` on `Game`, `CASTLE_CAUSEWAY_X0 94`, `X1 108`, `Y 56`.
2. Apply the fixed southeast layout after `world_gen` and before rivers/buildings: clear the island buffer to ocean, fill the jagged island silhouette with rock coast + land interior, carve the mainland approach, then let rivers/buildings skip the reserved footprint.
3. Gate the causeway at `tile_blocked:1390`: if `castle_mask[ty][tx] & CASTLE_SOLID` and `!castle_unlocked` → blocked (treat as `solid`). Causeway bridge tile(s) are the only opening; `solid` cleared on unlock.
4. Place `castle_key` at `(88,59)` on the mainland approach watchtower (hardcoded, not RNG); keep ability gating unchanged.
5. Wire `try_interact:3650` new branch `try_pick_castle_key` returning `4`, `SFX_CHIME`, toast `"a castle key is found"`, `hud.mm_dirty=1`. Order: `portal(1)→restore(2)→shard(3)→key(4)`.
6. Bump save to `v2` (`wayfarer.sav:29`): `magic WF v2`, `u8 castle_key` after `abilities`, `reserved` shift, validation-first load leaves live `Game` untouched on bad magic/version/OOB; `v1` loads with key `0`.
7. Add `--aether-test` beside `--shard-test:9503`: (a) closed: flood from spawn reaches `0` tiles beyond causeway, (b) open after `E` at watchtower: `>200` tiles beyond, (c) `tile_blocked` purity (`solid`/`terrain` only, mask goes through `solid`), (d) mask determinism (same seed → same mask, decoration via `tile_hash` may differ), (e) save round-trip `v1`+`v2`.
8. HUD: keep `PROMPT_LOCKED` padlock for the closed causeway (already in `src/main.c:966`), no new clutter per `Save and UI.md:22`.
9. Build → current release is `899,584` after the supplied dark-fantasy subset (93 records / 82 streams / 11 dream variants); run full suite + `--aether-test`.

**Definition of done — Slice 1:**
- [x] Castle island mask exists in the southeast overworld, causeway is closed before the key, and `--aether-test` is green.
- [x] `E` at watchtower picks up key, causeway opens, and the real interaction path is tested.
- [x] `tile_blocked` invariant survives (gate is represented through `solid`).
- [x] Save `v2` round-trip is green; legacy `v1` loads with key locked.
- [x] Full suite green at `144×138`; current release is `899,584` bytes.

**Traps for Slice 1:**
- Reserve colliding with `gen_sector` — must be carved before generation, not patched after.
- Bridge touching overworld by one tile makes gate cosmetic — `--aether-test` closed case is the load-bearing check.
- New flag reusing `souls` would hide in `Handover.md:734` logic — new `Uint8` is explicit.

## Slice 2 — Outer Courtyard ruins (first art)

**Goal:** the courtyard reads as ruined and navigable from the causeway, storytelling without interaction.

**Concrete tasks:**

1. Use the supplied `assets/dark_fantasy/walls/`, `buildings/`, and `props/` assets. `tools/bake.ps1` explicitly emits the relevant subset as `AETHER_*`; current bake is 93 records / 82 streams / 11 dream variants. Dungeon/interior/environment assets remain unbaked until their slices have callers.
2. Populate courtyard from mask + `tile_hash` decoration (reuse `prop_at` thresholds), add 2–3 vignettes (fallen banners, burned carts, dead trees).
3. Verify by screenshot from `(118,42)` — keep dominates skyline, courtyard navigable; no new test.

**Definition of done — Slice 2:**
- [x] Outer courtyard composition is wired with supplied dark-fantasy walls/buildings/props and screenshot-tested in selftest overlay mode.
- [x] `--sprite-test`, `nm`/build checks, and full suite remain green; no PNG is shipped beside the executable.

**Still deferred after this landing:** Inner courtyard/keep verticality, dungeon modules (`--dungeon-test`), torch lighting, guardian enemies, Aetherhold fragments.

## Verification gate (landing)

- `--aether-test` (5 sub-cases) + full suite (`rng/iso/font/hud/fog/sprite/fade/rebuild/ground/motion/sector/portal/shard/save/land(20)/village(32)/play(50)/gating/reach/bridge/region/move/audio`) at `144×138`, `nm` 1 symbol, `build` only exes + `.last_size` + `wayfarer.sav` (29B), screenshots: approach with castle on horizon, closed vs open causeway, outer courtyard ruins.

## File map for this landing

- `src/main.c:60` tunables + `src/main.c:940` guard comment + `src/main.c:1390` `tile_blocked` + `src/main.c:1785` `game_init` + `src/main.c:3650` `try_interact` + `src/main.c:9503` `hud_selftest` area + save structs.
- `tools/bake.ps1` + `assets/dark_fantasy/{walls,buildings,props}/` + `src/art_data.h` (Slice 2).
- `design/phases/Phase 13 - Aetherhold Castle Plan.md` (this file), `Handover.md:1730` §11, `Phase Roadmap.md:24`.

## Evidence

Slice 1–2 implemented 2026-08-06 as described above (SE island, mainland key). **Relocated
2026-08-07**: top-right island (`110,2`, `42×42`), key item removed for a Well-Soul-restore gate,
world `164×157`, bake trimmed to 78 records / 67 streams (a 108-PNG `assets/castle/` pack was
baked once for the relocation and then reverted — committed, currently unbaked). Current release:
`846,848` bytes, `593,152` headroom. Full suite + `--aether-test` + 50-seed `--play-test`
re-verified green live on 2026-08-07. Remaining slices are still explicitly deferred: inner
keep/verticality polish, dungeon modules, lighting/audio, guardians, and Aetherhold rewards.
