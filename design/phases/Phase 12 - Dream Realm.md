---
tags: [design, phase, wayfarer]
phase: 12
status: planned
updated: 2026-08-05
---

# Phase 12 — Dream Realm

**Status:** Planned. Approved 2026-08-05 as a deliberate change of direction, timeboxed.
**Implementation plan:** [[Phase 12 - Dream Realm Plan]] — 11 tasks across the five slices, each
with its test-first cycle and its negative control.
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
      **The dream sector is ragged but CONNECTED, not a true archipelago** — corrected 2026-08-05
      while writing the plan. Genuinely separate islets would strand entities the verifier requires
      to be reachable, and it would reject seeds forever. The islet *look* comes from a rougher
      coast (`DREAM_ROUGH 0.78` against `LAND_ROUGH 0.55`) at the same sea threshold, not from
      fragmenting the landmass.
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

**`portal_link(w, tile)`** — returns the paired tile or −1. Consulted by **five** callers, reached
through a single `tile_neighbours()` helper:

1. `bfs_open` — the region partition,
2. `flood_open` — the spawn component and `--land-test`,
3. **`walk_regions`** (`src/main.c:4194`) — the *walk* side of `--gating-test`,
4. region adjacency in `regions_build`,
5. `autopilot_tick`,

plus the `E` interact in `sim_step`, which is not a traversal.

> **Corrected 2026-08-05 while writing the plan.** This section originally said *four* callers and
> omitted `walk_regions` — which is exactly the omission that would have made walk-reachable and
> graph-reachable disagree while every other traversal looked right. Rather than document five
> places to remember, the plan introduces **`tile_neighbours()`**: one function returning the four
> orthogonal tiles plus the portal's pair, with each caller applying its own blocked test. The
> split is then impossible rather than merely discouraged.

That every traversal reads the *same* adjacency is the entire correctness argument. This is the
portal's version of decision 33 — *a bridge clears `solid`; it is not a collision special case* —
and it is why `--gating-test`'s walk-vs-graph assertion needs no weakening.

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

## Evidence — slice 1 (grid growth and the dream sector)

**Landed 2026-08-05. `WORLD_H` 60 → 104, +512 bytes** (755,200 → 755,712; 684,288 still free).

- **`--sector-test` PASS across 30 seeds**, with its negative control disagreeing on 40 of 104
  rows. Before the fix it failed on exactly the two things being built: 240–308 walkable tiles in
  the void band, and an overworld flood leaking into ~1,200 dream tiles.
- **`--land-test` PASS at 30 and 100 seeds**, re-aimed rather than loosened, and both negative
  controls still fire — the drowned-map control now trips **4** assertions instead of 3, because
  the new far-sector check catches it too.
- **The whole suite re-run:** iso, fog, fade, sprite, rng, move(20), village(30 + both controls),
  region(30), reach(50 + control), gating(30), bridge(**198/200**), **play 50/50**.
- **Render 1.194 ms mean / 3.329 ms max**, frame 16.943 ms = 59.0 fps. Up from 1.065 ms because
  the band sweep now covers 210 bands instead of 166. **5.6% of the 21.333 ms budget.**
- **Looked at**, via the new `--grid` flag: twelve worlds, each with two separated landmasses, the
  dream one ragged-but-connected and unpopulated.

### What slice 1 changed that the plan did not anticipate

- **`--play-test` never dropped below 50/50.** The plan warned it might, on the theory that
  `place_entities` could strand a fragment in the unreachable dream sector. It cannot:
  placement only ever draws from `regions_reachable`, so a disconnected sector simply never
  receives an entity. The completability proof was never at risk. Good news, but it also means
  **slice 1's play-test result proves less than it appears to** — the real test of the portal is
  slice 2's.
- **`--bridge-test` moved from 200/200 to 198/200** bridge-bearing seeds shrinking under
  suppression. Expected: river sources are now confined to the overworld, so two seeds have a
  bridge whose removal no longer changes the spawn component. The test's pass condition is
  "at least one seed shrank", and 198 is still overwhelming evidence.
- **`place_rivers` and `place_buildings` had to be clamped to the overworld.** Both sampled `cy`
  over the whole grid, so half their attempts would have landed in the dream sector — thinning
  the overworld's villages and rivers without anything failing. `VILLAGE_SITES`, `VILLAGE_RADIUS`,
  `VILLAGE_SPACING` and `RIVER_SRC_MIN` are all denominated in **tiles** and so do not follow a
  grid change, which is exactly why the sampling range had to move rather than the constants.
  This is the "constants denominated in tiles do not scale" trap, hit for the third time.
- **A `--grid` CLI flag was added** (self-test only, beside `--overlay` and `--tune`). The camera
  follows the player, so no ordinary capture can ever show both landmasses; without this there is
  no way to look at the shape of a two-sector world without a human holding F2.

## Evidence — slice 2 (the portal)

**Landed 2026-08-06. Grid + portal + travel come to +1,536 bytes total** (755,200 → 756,736;
683,264 still free). Render 1.016 ms mean, 59.5 fps.

- **`--gating-test` PASS 30/30 with the portal edge live.** This is the slice's real proof:
  walk-reachable == graph-reachable across all four ability tiers, through an edge that is not a
  tile adjacency. It is what `tile_neighbours` exists for.
- **`--portal-test` PASS, 100/100 seeds shrank** when the portal was suppressed, plus travel
  assertions both ways and a control proving `E` does nothing away from a portal.
- **`--sector-test` PASS at 100 seeds**, now including a spawn-sector assertion.
- **`--land-test` PASS at 30 and 100 seeds**, re-aimed twice (see below), both controls firing.
- **Full suite:** village(30), region(30), reach(50 + control), move(20), rng, iso, fog, fade,
  sprite, bridge(**200/200**, back up from 198), **play 50/50**.

### Four faults this slice found, three of them in code the plan called finished

1. **The spec undercounted the traversals — twice.** It said four readers of `portal_link`; the
   plan found `walk_regions` and made it five; the refactor found **`bfs_gated`** and made it six;
   and `--land-test`'s own **`land_flood`** is a seventh. Six now route through `tile_neighbours`.
   `land_flood` deliberately does **not**: it asks whether each sector is a real place on its own,
   which is a question about landmass shape and must stay independent of the graph.
2. **The player could spawn in the dream realm.** `game_init` picked the largest open component
   across the whole grid — correct with one island, a way to start the game past the portal with
   two. Fixing pass 1 was not enough: once the portal exists `flood_open` crosses it, so the spawn
   component spans both sectors and its **centroid lands in the void band**, putting the nearest
   component tile in the dream sector anyway. Measured at row 69 on seed 15 of 30. Both the
   centroid and the spawn search are now confined to the overworld. **Nothing else catches this** —
   `--land-test` measures the spawn component from wherever the spawn is, so a dream spawn looks
   perfectly healthy.
3. **`--portal-test` initially reported 97/100, not 100/100.** The overworld end was placed on any
   open tile, which on ~3% of seeds is a detached lobe the player cannot reach — exactly the rate
   decision 30 records for detached lobes. Both ends now sample inside their sector's **largest**
   component. The 3 anomalous seeds were a real defect, not test noise.
4. **`autopilot_tick` returned 1 for a portal step**, so `--play-test` printed `restored 20/19` —
   more restorations than there are entities. Its contract is "1 if it RESTORED something, 0 if it
   MOVED", and a portal step is locomotion. Harmless to the run, and precisely the sort of quietly
   wrong number this project has been bitten by before. Caught only because the total exceeded a
   bound that cannot legitimately be exceeded.

### `--land-test` was re-aimed twice, and never loosened

Both times the *threshold* was untouched and only the *denominator* changed, because `total` now
spans two landmasses:

- **50% reachability bound** → measured against the player's own sector. Fired on seed 16 at
  1,557 of 3,583 purely because that seed's dream sector is larger than its overworld.
- **12.5% landmass bound** → measured against the player's sector area. Fired on seed 16 at
  1,137 of 11,232, while 1,137 is a healthy 17.5% of the 6,480-tile overworld.
- A **new** far-sector assertion requires the dream sector's largest component to be ≥6.25% of its
  own area, since the portal needs somewhere worth landing.

### NOT verified by slice 2

- **Nothing draws the portal**, so nobody has seen it. Travel is proven by assertion, not by eye.
  The visual gate moves to slice 3, which is when the FX frames get a caller.
- **`--play-test` 50/50 does not exercise travel.** Every entity still lives in the overworld, so
  the autopilot has no reason to cross. The portal's effect on *reachability* is measured
  (100/100), but its effect on *completability* is not tested until Task 9 puts fragments in the
  dream sector. Slice 1's note applies again, one level up.
- **Where the portal lands is arbitrary within the largest component.** It is not yet in a
  `TERRAIN_DARK` region — regions do not exist when `place_portal` runs — so the Kindle framing
  is still fiction rather than mechanism.

## Evidence — slice 3 (the look)

Tasks 6 and 7. **+9,728 shipping bytes**, 756,736 → 766,464, against 673,536 still free under the
ship target. Render *improved*, 1.016 → 0.974 ms mean.

- **The recolour is one formula with two implementations, and the test proves they agree.**
  `dream_shift()` in `src/main.c` is the definition; `ConvertTo-DreamColour` in `tools/bake.ps1` is
  the copy that has to run at bake time, because a sprite's palette is baked and cannot be shifted
  at runtime. `--sprite-test` section (e) checks **every** baked `_DREAM` palette entry against the
  C function and passes **within 1** — the tolerance is PowerShell's `[int]` rounding half to even
  against C's half away from zero, and nothing else. Same "keep it in sync BY TEST, not by
  discipline" shape as the key-magenta list.
- **The 11 dream sprites share their twins' pixel streams — asserted, not assumed.** This is the
  claim the whole "the biome is nearly free" argument rests on, and it was stated in three design
  documents and checked nowhere. `--sprite-test` now compares `data_off`/`data_len`. Measured cost
  of the 11 variants: **~600 bytes of palette plus 176 bytes of records**, against ~19 KB to
  re-author them.
- **Negative control for both:** an unshifted palette must be rejected. 30 of 30 entries of the
  first tree would fail the comparison, so the check is known to have teeth.
- **Decision 42 now holds per sector, and by construction.** `dream_shift` is monotone in
  luminance, so it cannot reshuffle a value hierarchy — which is what lets it pass `--fog-test`'s
  ordering sweep unchanged. Measured: dream ground 52, dream stone **49**, void **38**.
- **The void is authored, not derived, and `--fog-test`'s new control says why.** A mechanical
  `dream_shift` of the sea ramp — which is what the plan's recolour would have produced — lands at
  luminance **62** against dream ground at 52: a gulf brighter than the land floating in it, which
  is decision 42's pale-floating-cube fault wearing a new costume. The control rejects exactly that
  ramp, so the rejected value is one that was really on the table rather than an invented bad
  number.
- **`--fog-test` sweeps 56 colours now** (was 45), including every dream terrain, the shifted stone
  ramp and the void ramp: **0 inversions**, 5 collapses (was 3 — two more pairs tie under rounding
  at some reveal, reported rather than failed, on the same policy as before).
- **The whole suite was re-RUN, not re-argued**, even though every change in this slice is
  render-only: sector(30), portal(30), land(30 + both controls), village(30), region(30),
  move(30), reach(50 + control), gating(30), bridge(**200/200**), **play 50/50**, iso, fog, fade,
  sprite, rng. Nothing moved.

### What slice 3 changed that the plan did not anticipate

- **The void band is not `dream_sector`.** Task 1's comment assumed rows 60–63 would take the dream
  palette "by the same path", but `dream_sector(ty)` is `ty >= 64`, so the band between the two
  landmasses would have rendered in the overworld's sea blue — a strip of ordinary sea along the
  horizon of a violet void. Resolved with a second, adjacent, **render-only** macro
  `dream_palette(ty)` = `ty >= OVERWORLD_H`, rather than widening `dream_sector`, which would have
  quietly handed four rows to the dream side in `--sector-test`'s counts and `--land-test`'s
  per-sector bounds.
- **The plan's animation clock would have frozen the portal.** It specified
  `(int)(p.anim * PORTAL_FPS)`, but `p.anim` is reset to zero the moment the keys are released so a
  standing player shows frame 0 — correct for a walk cycle, wrong for anything in the world. Added
  `Game.clock`, advanced by `sim_step` and read only by `render`, render-only in the same sense as
  `height` and `surf`.
- **`fx_well` and `fx_crystal` were NOT baked**, against the plan's task 7. Nothing draws the Well
  until slice 5 and **nothing in this phase draws `fx_crystal` at all**, so baking them now is
  ~16 KB with no caller — the exact rule that kept the bitmap font at +0 shipping bytes. They go in
  with the tasks that call them.
- **`ART_BLD_PORTAL_ARCH` was already in the bake and drawn by nothing.** The team delivered a
  portal arch with the buildings back in Phase 07; no table referenced it, so it had been dead
  weight since. It is now the portal's structure, with the FX vortex turning in its opening — so
  task 7 cost eight new frames rather than a new authored sprite.
- **The contact shadow under a baked prop is grass-coloured**, and left overworld-green it drew a
  ring of lawn under every violet tree — at the one place the eye is already looking, because a
  contact shadow is what says where the trunk meets the ground. Found by screenshot; no test has an
  opinion on it.
- **The starfield is free.** `tile_detail` already scatters marks from the tile hash, so the void's
  stars are the sea's speckle with a pale colour and a 1 px width. It is the one thing in the dream
  realm deliberately allowed to out-value the ground: a star is a point light, and decision 42 is
  about surfaces.
- **Flowers and stumps are replaced by crystals in the dream sector, at identical density.** Same
  rolls, same thresholds, the same tiles carrying a prop — only which prop changes, and only for
  the two kinds with no baked art. A sawn stump and a yellow meadow bloom read as the overworld's
  countryside whatever colour the trees behind them are. Keeping the *rate* identical is
  deliberate: prop density is what decides how much terrain the reveal mechanic can still show.
- **`--dream N` was added** (self-test only, beside `--overlay`, `--tune` and `--grid`). `--dream 0`
  stands at the overworld end, `--dream 1` crosses. Without it there is no way to photograph the
  biome: the player provably spawns in the overworld on every seed, and the autopilot has no reason
  to cross while every entity is still overworld-side. It positions her and calls the **real**
  `try_portal`, so a capture cannot show a place the game itself could not put you.

### The bug the user found on the first seed anyone played

**The portal teleported the player onto ground she could not stand on, on 11 of 100 seeds.**
Reported from the screen, on seed 1, within minutes of the slice being called done.

`place_portal` runs **before** `regions_build` — it has to, or the region partition never sees the
dream sector at all — so it picks both ends out of `solid` alone and cannot know what terrain they
will be given. The dream end then sits deep in the region graph *by construction*, which is exactly
where `regions_assign_terrain`'s depth bias gates hardest. Arriving in a `TERRAIN_WATER` region with
no Wade, `tile_blocked` refused her tile, `move_axis` correctly rejected every direction, and the
only input that did anything was `E` to go back.

**Nothing in the suite could have caught it**, and that is the part worth keeping:

- `--gating-test` asserts walk-reachable == graph-reachable. Both agree perfectly that a gated
  arrival tile is unenterable — they are *supposed* to.
- `--portal-test`'s shrink measure was 100/100 before and after. A component you cannot **stand
  in** is still a component you can **reach**.
- `world_solvable` only ever asks about entities, and there are none in the dream sector until
  Task 9.

The invariant is about the arrival point itself, so it needed its own assertion. **Fixed at the
source:** `regions_assign_terrain` now exempts the portal's arrival region exactly as it already
exempts the spawn — a gate you arrive *inside* is not a gate, it is a wall behind you. Refusing to
travel, or nudging her to a nearby open tile on arrival, were both rejected: they are collision
logic papering over a generation fault.

**Measured after: 0 of 100 seeds land on gated ground** (was 11), and the mean walkable area from
the arrival point rose 739 → 818 tiles. The bound is deliberately **zero-versus-nonzero** — "can
she stand up" needs no threshold and cannot be quietly loosened. The negative control gates the
arrival region by hand and requires rejection, so the fault that shipped is the one the checker is
proven to catch.

Cost: **+512 bytes**, 766,464 → 766,976. Whole suite re-**run** because terrain assignment changed:
sector 30, land 30, village 30, region 30, gating 30, reach 50 + control, bridge 200/200,
**play 50/50**.

**Still open, and deliberately not fixed with a number:** 4 of 100 seeds land in a 6-to-30 tile
pocket — standable, but small. That is legitimate gated design (the overworld's spawn region works
the same way) and the right enforcement is **Task 9**: putting 4 fragments and 2 Souls in the dream
sector makes the existing generate-then-verify loop reject a landing that opens onto nothing, with
no invented threshold anywhere. `--portal-test` reports the count so it stays visible.

### NOT verified by slice 3

- **Whether it reads as Lumiara is the user's call and has not been made.** No test has an opinion.
  Seen on seeds 1, 3, 4 and 5, both ends of the portal, fogged and with the overlay.
- **Nothing has been seen in MOTION.** The vortex is eight frames at 8 fps driven by `Game.clock`;
  no scripted run diffs two frames to prove it advances, and nobody has watched it turn.
- **`--play-test` 50/50 still does not exercise travel** — unchanged from slice 2, and still true
  until Task 9.
- **The dream realm's fogged periphery is the overworld's cool grey haze.** `fog_lerp` is one
  global blend and knows nothing about sectors, so unrevealed dream ground reads grey rather than
  violet. That is the game's core mechanic working as designed — the overworld looks the same way —
  but a violet haze on the dream side is a real option nobody has considered on screen.
- **Rocks stand in the void.** `prop_at` puts boulders on solid tiles, ocean included, so the
  starfield carries scattered rocks. In the overworld those are rocks in the shallows and read
  fine; over a void they read as floating debris, which may be on-concept for a floating-island
  biome or may be a fault. Unjudged.
- **The player-occlusion question is not improved by this slice** and the dream canopies are the
  same size as the overworld's, so decision 40's fade carries the same load it did.

## Evidence — slice 4 (the prompt, and the dream realm stops being scenery)

Tasks 8 and 9. **+1,024 bytes total** (766,976 → 768,000) — task 9 cost **+0**. Render 1.089 ms.

### The gap that closed

**`--play-test` crosses the portal on all 50 seeds, 2–4 times each, and completes 19/19.** Four
handovers carried "the portal's effect on *completability* is untested" as a known gap, because
every entity lived in the overworld and the autopilot had no reason to travel. Task 9 put 4
fragments and 2 Souls past the portal and the gap is closed.

**Crossings are COUNTED, not inferred.** The argument is sound — the sectors share no tile edge
(`--sector-test`), so a dream-side entity cannot be restored without travel — but a future change
that accidentally joined the landmasses would keep every seed completing and quietly retire the
only end-to-end exercise travel has. A crossing count of 0 now fails the seed.

### The gate hole task 8 exposed

`PORTAL_REACH` is 25.5 px; an orthogonally adjacent tile centre is 24 px away. **The interact fired
from the tile next to the portal**, so a portal standing in a Kindle-gated region could be taken
from the ungated ground beside it and the ability gated nothing.

`--gating-test` could not see it, and the reason is the interesting part: every traversal reaches
the portal edge through `tile_neighbours` and expands from an end only after standing **on** it, so
walk-vs-graph parity was true of a model **stricter than the real interact**. *Two things that
agree with each other can both disagree with the game.*

`try_portal` now requires the end to be standable, which makes the interact agree with the graph
exactly. **No new input to collision** — collision still reads `solid` and `regions[].terrain` and
nothing else; this is the interact consulting collision, which was always the allowed direction.
The autopilot is unaffected: `bfs_gated` already only crossed from a standable end. It also makes
the **padlock prompt reachable rather than decorative** — you see the portal, you cannot use it
yet, and it is somewhere to come back to instead of a dead end.

### What slice 4 changed that the plan did not anticipate

- **The prompt is procedural, not three baked keycaps.** Nobody is authoring that art, and a keycap
  generated by a script and then baked is the same machine drawing with a build step and ~900 bytes
  of blob in front of it. Drawn in code it costs no art data, and a real authored keycap later
  replaces the body of **one function** — the same seam the team's sprites came in through. Not the
  bitmap font either, for the plan's own reason: `draw_text` is behind `WAYFARER_SELFTEST` and
  calling it would spend decision 25's +0-byte gate on a single letter.
- **The cap was sized against the font, not by eye.** The first version was a 10 px cap with a 4×5
  px `E` and read on screen as a dark speck above the arch. `FONT_5X7` at `FONT_SCALE` 2 is legible
  at 10×14, so the glyph matches that and the cap is drawn around it.
- **The quota is counted on TILE ROWS, not on regions.** `regions_build` partitions through
  `tile_neighbours`, which includes the portal edge, so the region holding the overworld end can
  bleed across into the dream side — "which sector is this region in" has no answer for those.
  Where an entity *stands* always does.
- **`entities_split_ok` is kept out of `world_solvable`.** That function means exactly one thing —
  every entity reachable in ability order — and should keep meaning it. The split is a separate,
  weaker question, asked only by the primary generate-then-verify loop. **The ungating fallback
  deliberately does not ask it:** that path exists to guarantee a completable world at any cost, and
  [[Cut List]] is explicit that the reachability guarantee is never traded. A thin dream realm still
  ships; an unwinnable world does not.
- **The three ability grants stay in the overworld.** Putting one past the portal would make the
  route to the portal depend on an ability behind it — not unsolvable, the verifier would catch
  that, but a needless knot in the one placement that has to stay legible.
- **The prompt checker first used `fake_surface`**, which zeroes the struct — so `format` was NULL,
  `SDL_MapRGB` returned 0, and every prompt pixel was written as **black**, indistinguishable from
  "nothing was drawn" against a cleared buffer. It reported the prompt as blank. A real surface now.

### Verified

- **Written test-first, and it failed on 2 of 10 seeds before the implementation.** The quota is
  often met by luck, which is exactly why it needed asserting rather than assuming. After: 50/50,
  and `attempts 1` on every seed — the retry path never actually fires.
- **The prompt's bob is clamped AND asserted to move.** A bob that never moves satisfies a clamp
  perfectly; asserting only the bound would be a checker a constant 0 passes. Control: an unclamped
  sine is rejected on 264 of 400 samples.
- **`PROMPT_NONE` writes exactly 0 px**, and the three visible kinds render distinct pixel counts
  (400 / 475 / 438) — so a prompt that told the player nothing would fail rather than pass quietly.
- **The gate is asserted from both positions.** With the overworld end gated by hand, `E` is refused
  standing **on** the tile and standing **beside** it — the second is the half that was broken — and
  works again with Kindle.
- **Seen on screen in all three states:** the `E` keycap over the arch on seeds 5 and 11, the
  padlock on seed 1 (whose overworld end sits in a Wade-gated region).
- **Whole suite re-run:** reach 50 + control, **play 50/50 with crossings**, gating 30, sector 30,
  portal 30, land 30 + both controls, village 30, region 30, move 30, bridge **200/200**, iso, fog,
  fade, sprite, rng.

### NOT verified by slice 4

- **The prompt has not been seen in motion**, so whether a ±2 px bob at 1.6 Hz reads as inviting or
  as jitter is unjudged — the same gap the portal vortex still has.
- **Nobody has played a seed through the portal by hand.** The autopilot crosses 50 times; a human
  has crossed once, in the session that found the arrival bug.
- **Whether 4 fragments and 2 Souls is the right split** is a pacing question, and pacing has not
  been measured since before the grid doubled.
- **The interact prompt has not been photographed over a dream-side entity** — only over overworld
  ones. The draw path is identical (one call, one function), so this is a gap in the picture rather
  than in the code.

## Open, and deliberately not decided here

- **Whether the void reads better as recoloured `SURF_OCEAN` or needs a real `SURF_VOID`.** Decided
  on screen, in slice 3, not in advance.
- **The player-occlusion question** ([[Handover]] §0) is still open and is *worse* in a biome whose
  concept art is dense with tall canopies. It is not in this phase's scope, but slice 3 will make it
  more visible and may force the decision.
- **Whether 4 fragments behind the portal is the right split.** A number chosen for shape, not
  measured against pacing — and pacing has not been re-measured since the last rescale either.
