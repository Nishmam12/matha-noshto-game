---
tags: [design, phase, wayfarer]
phase: 12
status: planned
updated: 2026-08-05
---

# Phase 12 — Dream Realm

**Status:** Planned. Approved 2026-08-05 as a deliberate change of direction, timeboxed.
**Sequenced:** after [[Phase 08 - Save Load]], before [[Phase 10 - Motion]]. Numbered 12 rather than
renumbered into place because every other phase file is linked by name and renumbering would break
the graph. **It does not move the 2026-08-14 hard stop**; [[Phase 11 - Ship Critical]] still begins
on that date regardless of how many slices of this phase are finished.
**Depends on:** [[Phase 01 - Landform]] (the radial height field this phase runs twice),
[[Phase 05 - Verification Debt]] (`--land-test` must exist before the grid it guards changes shape),
[[Phase 06 - Water And Bridges]] (`--bridge-test` is the template `--portal-test` copies),
[[Phase 07 - Asset Seam]] (the bake pipeline and the palette seam this phase leans on hardest).
**Blocks:** Nothing. Every slice is separately shippable — see Timebox.

## Why this phase

The map already has a mystical area and nobody ever goes there. `TERRAIN_DARK` is a Kindle-gated
region type at `0x3b3350` that spawns crystals and otherwise does nothing, and Kindle itself is top
of [[Cut List]] precisely because it gates a corner of the map with no reason to visit it. Meanwhile
`assets/magical/` holds **56 delivered frames** — 16 portal, 16 rift, 16 well, 8 crystal — that
[[Phase 07 - Asset Seam]] deliberately left unbaked because nothing in the renderer called them.

This phase resolves all three at once: the dark region becomes the route to a portal, the portal
gives Kindle a purpose, and the portal is the caller the magical FX were waiting for.

The destination is a second biome in the Lumiara "Dream Realm" style the team supplied as concept
art on 2026-08-05: floating islands over a violet starfield void, purple canopies, cyan crystal
light, ruined stone arches. It is not a different game — it is **the same world dreaming**, which is
both the fiction and the technical strategy (see Art Production, tier 1).

## Definition of done

- [ ] The grid is `108×104`. Overworld occupies rows 0–59, dream archipelago rows 64–103, with rows
      60–63 an unwalkable void band. Both landmasses generate from the same radial height field,
      run twice with different parameters.
- [ ] A portal pair links the two. Travel is an `E` press with a proximity check, **not** a step-on
      trigger, so `tile_blocked` still reads `solid` and `regions[].terrain` and nothing else.
- [ ] `portal_link()` is consulted by the walk BFS, region adjacency, the autopilot and the interact
      — one decision, four readers — so **walk-reachable == graph-reachable stays true by
      construction** and `--gating-test` keeps its teeth.
- [ ] 4 of the 14 fragments and 2 of the 5 Found Souls live in the dream realm. `FRAGMENT_COUNT`,
      `SOUL_COUNT` and the `Uint32` restored-mask are all unchanged.
- [ ] 8 dream shards, in their own array, feed a **Dream Well** that releases the second dream Soul.
- [ ] A pixel-art **prompt indicator** appears above anything interactable, bobbing on the anim clock.
- [ ] `--portal-test` and `--shard-test` exist, each with a negative control, **and `--portal-test`
      was written before the portal rendered a single pixel.**
- [ ] `--land-test` is **re-aimed** at a two-landmass grid, not loosened until it passes.
- [ ] The full suite is re-**run**, not re-argued.
- [ ] The dream realm has been screenshotted and **looked at by the user**.

## Architecture

### Two predicates, and that is the whole of it

**`dream_sector(ty)`** — a one-line row test, the only thing in the codebase that knows where the
dream realm is. Read by `tile_colour`, `world_heights`, `prop_at`, `place_buildings`,
`place_rivers` and `place_entities`. Moving the sector is therefore a one-line change.

**`portal_link(w, tile)`** — returns the paired tile or −1. Consulted by exactly four callers:

1. the walk BFS (`bfs_open` / `flood_open`),
2. region adjacency in `regions_build`,
3. `autopilot_tick`,
4. the `E` interact in `sim_step`.

That (1) and (2) read the *same* function is the entire correctness argument. This is the portal's
version of decision 33 — *a bridge clears `solid`; it is not a collision special case* — and it is
why `--gating-test`'s walk-vs-graph assertion needs no weakening.

### What deliberately does not change

- **Collision.** Entering a portal is an interact, not a movement. `tile_blocked` is untouched, so
  the completability proof stays a **re-run, never a re-argument**. It also removes the
  arrival-ping-pong that a step-on trigger would need a latch to suppress.
- **The entity system.** 14 fragments + 5 souls = 19, still ≤ 32, still one `Uint32` mask.
- **Fog, camera, depth sort, save format.** One grid means none of them learn a second world exists.
- **`SURF_*`.** The void reuses `SURF_OCEAN` with a sector-aware palette — violet starfield instead
  of sea blue — and inherits the existing stepped sea-floor ramp, which is exactly the cliff
  underside the concept art shows. A new `SURF_VOID` **only if that reads wrong on screen**, decided
  by looking, not in advance.

### The shard / completion collision, and its resolution

`game_complete()` is `frags_restored + souls_restored >= ENTITY_COUNT` — **every** entity. So the
moment the Well grants a required Found Soul, the shards feeding it stop being optional, and a
player who cannot find 8 shards cannot finish the game.

This was caught during design, not during implementation, and the resolution is to make it
**provable rather than to back off**:

- Shards live in their own array, **not** in `ents[]`, so the mask and `game_complete` are untouched.
- `world_solvable` gains **one clause**: at least `SHARD_REQUIRED` shards are reachable.
- That clause gets a negative control like everything else here (`--shard-test`).

`SHARD_REQUIRED` is 6 of 8 placed, so two can be awkwardly sited without stranding anyone.

## Content

| | |
|---|---|
| Fragments in the dream realm | **4 of 14** — enough that the biome matters, not so many the overworld reads as a prologue |
| Found Souls | **2 of 5** — one placed normally, one locked in the Well |
| Dream shards | **8 placed, 6 required** |
| Portal sites | one pair: the overworld end sits in a `TERRAIN_DARK` region (Kindle-gated), the dream end on the archipelago's largest island |

## The prompt indicator

The first UI this game has ever shipped, and the first ambient motion in it.

- **`draw_prompt(fb, cx, by, kind, t)`** — render-only, called from `render` for **all four**
  interactable kinds: an unrestored entity (fragment or Soul) via `entity_in_reach()` ≥ 0, an
  uncollected shard, a portal end, and the Well. `entity_in_reach` already takes a `const Game *`
  and already returns the index, so the entity case costs nothing new; shards need the same
  proximity test against their own array.
- **A baked pixel keycap** in the Lumiara panel style: deep indigo fill, 1 px lavender border,
  warm-white glyph, soft outer glow. Three variants — `E` (interact), `E` over a portal ring
  (travel), and a **padlock** for a portal you lack Kindle for, so the gate is legible rather than
  mysterious.
- **Bobs ±2 px** on a sine of the existing anim clock. [[Handover]] §2 lists "nothing sways,
  shimmers, bobs or smokes" under what does not exist; this quietly starts [[Phase 10 - Motion]].
- **Deliberately not the bitmap font.** A ~300-byte baked keycap keeps the font's `+0 bytes` gate
  (decision 25) intact by construction, and a 5×7 glyph would not match the art anyway.

## Art production

Three tiers, ordered by cost.

**Tier 1 — recolour, ~1.4 KB.** `ArtSprite` stores `pal_off` into a shared `ART_PAL[]` *separately*
from `data_off` into the shared pixel stream. A dream tree is therefore **the same pixel stream with
a different palette**: one ~16-byte record plus ~54 bytes of palette, about **70 bytes** against
~1,700 for a re-authored sprite. The 8 trees, bush, grass tuft and rock all get dream palettes
generated by `bake.ps1` — hue toward indigo/violet, highlights lifted to cyan, per the Lumiara
palette strip. **This is the bulk of the biome, and it is nearly free.** It also guarantees the
dream silhouettes match the overworld's, which is what makes it read as the same world dreaming
rather than as a different game.

**Tier 2 — bake what already exists, ~22 KB.** 8 `fx_portal` frames, 8 `fx_well`, 4 `fx_crystal`.
**Which frames is not "the first N"** — take every second frame (`_0, _2, _4 …`) so a halved set is
still a complete loop rather than the first half of one; 8 frames at 8 fps is a full second. The
4 crystal frames are `_0, _2, _4, _6` of 8. **`fx_rift` stays unbaked**: 16 frames with no caller in
this design, and baking a sprite nothing draws is pure byte cost.

**Tier 3 — generate new, short list only.** The keycap prompt set, the shard pickup, one glowing
flora. Everything else on the concept sheet recolours instead. Generation goes through `bake.ps1`
like all art and never at runtime, per decision 34.

## Size and memory budget — measured, not estimated

Extrapolated from the one hard datapoint available: 37 sprites → 60,029 RLE bytes at 1.52×.

| Item | Bytes |
|---|---|
| 8 portal frames (48×48) | ~6 KB |
| 8 well frames (48×48) | ~6 KB |
| 4 crystal frames (**96×96**) | ~10 KB |
| `fx_rift` — not baked | 0 |
| ~20 palette-swapped dream props | ~1.4 KB |
| Prompt keycaps + shard + flora | ~1 KB |
| New code | ~3 KB |
| **Total** | **~27 KB, against 684,800 free — 3.9% of headroom** |

Byte cost here is **a knob, not a risk**: the 96×96 crystals cost 4× per frame what everything else
does and are the first thing to halve if it ever tightens.

**The binding constraint is the stack, not the file.** `Game` and `Scratch` are deliberately locals,
never statics (the `.data` trap). Eight self-test functions declare **both**:

| | now | at `WORLD_H` 104 |
|---|---|---|
| `Game` (11 B/tile) | 71 KB | **124 KB** |
| `Scratch` (14 B/tile) | 91 KB | **157 KB** |
| both, in one frame | 162 KB | **~281 KB** |

That fits MinGW's 2 MB default with room to spare, but it is **the number to check, not assume**,
and it is the thing that will bite if the grid is ever grown again.

## Timebox — five separately shippable slices

Each slice stands alone if the next is cut.

| Slice | Delivers | Risk |
|---|---|---|
| 1 | Grid growth, `dream_sector`, second generator pass, `--land-test` re-aimed | **High** — generator + test bounds |
| 2 | `--portal-test` **first**, then `portal_link`, then the portal renders. Travel works | **High** — the reachability argument |
| 3 | Tier-1 recolours + tier-2 baked FX. It looks like Lumiara | Low, additive |
| 4 | Prompt indicator, fragments moved into the sector. It plays | Low, additive |
| 5 | Shards, the Well, `--shard-test`. It has a reason to explore | Low, additive |

If the box runs out after slice 3, the game still ships a portal to a beautiful place with fragments
in it — just no shards and no Well.

## Verification gate

- **`--portal-test`** — `--bridge-test`'s exact shape (decision 36): generate each seed twice, once
  with `g_suppress_portal` set (self-test only, mirroring `g_suppress_bridges`), and fail if the
  player's reachable component never shrinks. **Written before the portal draws a pixel.**
- **`--shard-test`** — the new `world_solvable` clause, with a negative control that starves a seed
  of reachable shards and confirms rejection.
- **`--land-test` re-aimed** at a grid that is deliberately two landmasses. Per [[Handover]] §7, if
  a bound has to move, the justification belongs here — not in a quietly loosened number.
- **Full suite re-run**: reach 50, gating 30, play 50, bridge 200, sprite, iso, fog, village, rng,
  move, audio, perf.
- **A screenshot of the dream realm, looked at by the user.** Every visual bug of consequence in
  this project was found by looking, never by a test.

## Traps specific to this phase

- **Constants denominated in TILES do not scale with the grid.** `REVEAL_TILES`, `VILLAGE_SITES`,
  `VILLAGE_RADIUS`, `VILLAGE_SPACING`, `BUILDING_TARGET`, `RIVER_SRC_MIN` and `BRIDGE_SPACING` are
  all tile counts, so a 1.73× taller grid silently thins the villages and shortens the rivers
  relative to the world. This is **exactly** the trap the `TILE` 32→24 rescale already paid for
  once ([[Handover]] §7). Re-derive them by hand; nothing warns.
- **`place_rivers` BFS descends to *sea*, and the dream sector has none.** Rivers must be confined
  to the overworld sector, or `dream_sector` must be consulted inside `place_rivers`. Left
  unhandled, a dream-sector river source has no finite distance-to-sea and the descent has no
  defined behaviour.
- **`world_gen`'s radial height field is centred on the whole grid.** It has to become per-sector or
  the dream archipelago will be one lobe of a single giant island.
- **`render_grid` (F2, the 12-seed view) draws whole worlds**, unlike the main band sweep which
  draws only what is on screen. Check its cost at the new grid size.
- **A test can encode the assumption the change is removing.** `--land-test` currently asserts
  properties of *one* island. Re-aim it; do not loosen it.
- **Write `--portal-test` first.** [[Phase 07 - Asset Seam]] named a test to write first, it was
  written second, and a screenshot loop spent a stretch suspecting a decoder bug the test disproved
  in one run. Screenshots are the slowest debugging loop this project has.

## Open, and deliberately not decided here

- **Whether the void reads better as recoloured `SURF_OCEAN` or needs a real `SURF_VOID`.** Decided
  on screen, in slice 3, not in advance.
- **The player-occlusion question** ([[Handover]] §0) is still open and is *worse* in a biome whose
  concept art is dense with tall canopies. It is not in this phase's scope, but slice 3 will make it
  more visible and may force the decision.
- **Whether 4 fragments behind the portal is the right split.** A number chosen for shape, not
  measured against pacing — and pacing has not been re-measured since the last rescale either.
