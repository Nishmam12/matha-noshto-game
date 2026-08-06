---
tags: [design, phase, wayfarer]
phase: 13
status: planned
updated: 2026-08-06
---

# Phase 13 — Aetherhold Castle

**Status:** PLANNED — spec approved 2026-08-06. First major handcrafted region outside the starting village. Builds on Phase 12's portal/dream-realm pattern but is **not** a second biome — it is a handcrafted island connected by a causeway, with a modular dungeon beneath it.
**Depends on:** [[Phase 12 - Dream Realm]] (island generation pattern, portal gating, `tile_blocked` invariant), [[Phase 07 - Asset Seam]] (bake pipeline for castle arch art), [[Isometric Rendering]] (band-sweep depth, elevation), [[Save and UI]] (ability gating, HUD). World must already be `TILE 18` / `144×138` — this phase does not re-derive those.
**Blocks:** Nothing — first chapter after the village; later regions reuse its hybrid-generation and dungeon-module patterns.
**Approach reference images:** `AETHERHOLD CASTLE (CASTLE ISLAND)` + `AETHERHOLD PATH (CONNECTING LAND)` — the pair supplied with this spec. Left = Castle Island (keep / courtyards / walls / causeway / dock), right = Connecting Land (forest, S-path, watchtower, campsite, coastline). Both sections connect seamlessly at the causeway at runtime.

## Why this phase

The starting village reads as *home* — warm, restored, inhabited. The game has no counterpoint: no place that reads as forgotten, vertical, or mysterious, and no handcrafted level that rewards intentional environmental storytelling. The mysterious ruined castle visible across the water in the early game is explicitly designed to create long-term curiosity; without a region behind it, that promise is empty. Aetherhold is the first chapter that converts "cozy exploration" into "adventure" — vertical climbing, larger authored space, first dungeon, deeper puzzles and lore.

This is **not** a magical floating castle. The reference pile is Dark Souls (without horror), Zelda: Twilight Princess, The Last Guardian, Shadow of the Colossus, old European coastal fortresses — grounded, believable, subtle magic only where the story demands it.

The village is life / warmth / hope / restoration / home. The castle is isolation / forgotten history / fallen civilization / mystery / ancient knowledge / slow exploration.

## Design philosophy — do not re-litigate without flagging

- The castle must feel **different on first sight** from the village — cold, ruined, overgrown, vertical — not a recolor of the same tileset.
- Ground every element. No floating islands, no bright magical lighting. Remnants of ancient magic are subtle and story-critical only.
- Environmental storytelling over dialogue. A broken throne, a sword in stone, an abandoned feast, children's toys, a skeleton by a locked door — the player should understand the downfall without long text.
- Restoration is **partial**. Unlike the village (ruin→whole), the castle's ruins remain. The player restores only mechanisms: bridges, doors, elevators, shrines. History stays visible.
- Audio and lighting carry mood with minimal music. Outside: wind/ocean/birds. Castle: echoes/stone. Dungeon: drips/chains/low drones. Outside cold daylight + soft fog; inside warm torches + deep shadows + dust.

## Region overview — the climb

```
            Castle Keep  (throne / library / observatory / boss key)
                 ▲
        Upper Courtyard
                 ▲
         Inner Castle Wall
                 ▲
       Outer Courtyard Ruins  (statues / fountain / barracks / secrets)
                 ▲
           Broken Gatehouse  (tutorial: doors / switches / locks)
                 ▲
          Long Stone Causeway  (moss / cracks / ocean / storm waves)
                 ▲
            Mainland Forest  (path / campfire / watchtower / cliff)
```

The player climbs slowly. Every elevation reveals more of the castle. The keep dominates the skyline from the approach and the causeway.

## Gameplay purpose

- Vertical exploration (stairs, terraces, walls as elevation, not just decoration)
- Environmental storytelling at authored scale
- First dungeon (modular, replayable but pacing-fixed)
- More advanced puzzles (switches, pressure plates, locked doors)
- Lore discovery (journals, relics, portraits)
- Stronger guardian-type enemies (Ancient Knight, Rusted Guard, Stone Sentinel — not excessive fantasy)
- Exploration rewards: Memory Fragments, Relics, Journal Pages, Blueprints, Cosmetics, Keys, Secret Rooms

## World generation — hybrid, not purely procedural

Unlike the starting island (radial height field + `surf` + `gen_sector`), **Aetherhold's layout is handcrafted**. Small details remain procedural to preserve replayability:

**Fixed (authored):** coastline shape, cliff walls, castle walls/towers/gatehouse, courtyards, paths, staircases, causeway, dock, dungeon entrance, building footprints.

**Procedural (decorated):** tree/bush/grass placement, rubble/moss/cracked stones, fallen logs, ivy/ferns, crates/barrels/lanterns, dungeon furnishing (enemy placement, loot, secret rooms). This is the standard hybrid pattern: fixed layout for storytelling + procedural decoration for variation — layout supports navigability, decoration supports replayability.

Cutting the procedural decoration is not a valid descope — it is 3–5 lines of `tile_hash` gating, not authoring.

## The seven areas + dungeon — what each must deliver

### Area 1 — Mainland Approach
Forest path, campfire, abandoned watchtower, broken wagons, stone sign, cliff viewpoints. Castle clearly visible. No combat. Atmosphere: wind, ocean, birds. Serves as preparation and framing.

### Area 2 — The Causeway
Long stone bridge mainland→island. Broken railings, moss, cracks, small watch towers, collapsed sections, ocean below. **Initially inaccessible**, unlocked through story/ability progression. Storm presentation: waves crash against the bridge (visual, not simulation).

### Area 3 — Gatehouse
First contact with the castle. Broken wooden gates, fallen banners, rubble, burned carts, dead trees, guard towers. Teaches dungeon vocabulary: locked doors, environmental switches.

### Area 4 — Outer Courtyard
Large open exploration space. Overgrown grass, fallen statues, old fountain, destroyed barracks, storage, broken walls. Holds secrets: hidden journals, collectibles, shortcuts.

### Area 5 — Inner Courtyard
Emotional heart. Giant dead tree, royal garden, stone pathways, ancient memorial, chapel ruins. NPC Memory: the player learns the kingdom's downfall.

### Area 6 — Castle Keep
Tallest point, visible from almost anywhere. Throne room, library, observatory, royal chambers. Boss Key located here.

### Area 7 — Hidden Dungeon Entrance
Beneath chapel / statue / library / well — hidden, uncovered after restoring enough memories. Stone door opens, torchlight emerges, music changes.

### Dungeon
Ancient stone fortress, **not caves**. Hallways, storage, prison, armory, archives, crypt, underground river. **Modular generation:** a fixed set of room modules whose *order, enemy placement, loot, and secret rooms* shuffle each run, while overall progression (entrance→boss→return) stays fixed. This separates architectural layout from furnishing — navigable but replayable.

## Technical requirements

### Rendering
- Keep the current isometric camera (`TILE 18`, `144×138`). Do not re-derive `TILE`.
- Reduce tile size was already applied to increase environmental density while keeping sprites readable — do not change again in this phase.
- Layered rendering with correct band-sweep depth for walls/cliffs/trees/character. Draw order depends on tile position (band `tx+ty`), not fixed layers — already the rule in [[Isometric Rendering]] § depth order. No `SDL_Texture` path, no `SDL_image` — hand-rolled `draw_sprite` only.
- Cold daylight + soft fog outside, warm torch inside. Never bright magical lighting.

### Asset pipeline — two modular parts
Both from the same `art_data.h` contract (`art/<category>_<name>[_<variant>].png → tools/bake.ps1 → src/art_data.h`):

**Part 1 — Castle Island:** keep, courtyards, walls, towers, gatehouse, dock, dungeon entrance, cliff edges, internal paths.

**Part 2 — Mainland Approach:** forest, causeway, roads, watchtower, campsite, ruins, coastline, bridge connection.

Both sections connect at the causeway at runtime. Each part bakes independently so the team can iterate on one without rebaking the other. `assets/` already holds `buildings/`, `nature/`, `magical/` — new categories are `castle/` and `causeway/` (or reuse `buildings/` + `nature/` with `castle_` prefix; decide before baking and keep naming `category_name_variant`).

### Collision / gating
`tile_blocked` still reads only `solid` + `regions[].terrain`. The causeway's *closed* state is a gated region or a `solid` bridge segment cleared on unlock — not a second collision input. Reuse the portal-gating pattern (standable check) rather than inventing a new one.

## Asset list (for baking — not exhaustive, but the budget)

**Terrain:** coastal cliffs, stone paths, mossy ground, dirt paths, shoreline, rocky beach.
**Architecture:** castle wall modules, tower modules, gatehouse, staircases, battlements, arches, broken pillars, stone bridge, dock.
**Nature:** pine trees, dead trees, bushes, ivy, moss, ferns, tall grass.
**Props:** crates, barrels, broken carts, lanterns, campfire, wells, benches, wooden signs, rope bridge, stone statues.
**Ruins:** rubble piles, fallen walls, cracked floors, broken pillars, destroyed roofs, collapsed stairs.
**Dungeon:** stone walls, prison bars, torches, chains, wooden/stone doors, pressure plates, puzzle switches, broken furniture, bookshelves.
**Lighting:** torches, dust particles (drawn, not particles). Audio: wind/ocean/birds + echoes/drips/drones (synth, no samples).

## Player experience — the intended arc

```
Village → Curiosity → Castle on the Horizon → Story Progress → Cross the Causeway
  → Explore Ruins → Learn the History → Unlock the Dungeon → Recover a Major Memory → Return Home Changed
```

The castle protected the village. It is the first major chapter, revealing the history that shaped everything the player has been restoring.

## Approach — how this region is built, step by step

This is not a single "build a castle" task. It is sequenced so each slice is shippable and verifiable without blocking the others:

### Slice 1 — Causeway gating + mainland approach (vertical slice, no art)
- Define the second island's handcrafted mask (a `Uint8 castle_mask[WORLD_H][WORLD_W]` or equivalent) that marks *fixed* solid/ground vs *procedural* decoration tiles. For now, a flat meadow with a coastline and a single bridge tile closed by default.
- Gate it: `castle_unlocked` bool on `Game`, set by restoration count / story flag (reuse `Found Soul` count or a new `castle_key` — decide before coding, keep it one `Uint8`). `tile_blocked` consults the mask only when the gate is closed.
- Verify: `--aether-test` (new) can assert closed→no path to keep, open→path, and that `tile_blocked` still reads only `solid`/`terrain`/`mask`.

### Slice 2 — Outer courtyard ruins (first art, first storytelling)
- Author `castle/` wall modules + rubble + statues (6–8 sprites). Bake, wire through `art_data.h`.
- Populate outer courtyard from the authored mask + `tile_hash` decoration. Add 2–3 environmental storytelling vignettes (fallen banners, broken carts) that have no interaction — they exist to be seen.
- Verify by screenshot, not by test: does the courtyard read as ruined and navigable?

### Slice 3 — Inner courtyard + keep (verticality)
- Introduce elevation for the castle terraces (reuse `world_heights` + `ELEV_LEDGE` pattern — castle steps are 16px ledges, already legible). No new height system.
- Add keep interior (throne/library) as interior wall modules. Keep stays visible from the approach — verify from the causeway camera, not just from inside.
- Gate the keep door on the boss key (same bool pattern as the causeway).

### Slice 4 — Dungeon modules (procedural but pacing-fixed)
- Define 8–10 room modules as fixed `Uint8` tile patches (hallway, cell block, armory, archive, crypt, river). A shuffle picks order + enemy/loot placement but keeps entrance→boss linear.
- Dungeon entrance hidden (chapel/statue) — revealed when `castle_unlocked` and a restoration threshold is met. No new generation system: it reuses the room-module idea from village building placement.
- Verify: `--dungeon-test` can assert linear progression + shuffling + that no module walls off the exit.

### Slice 5 — Polish (lighting, audio, enemies, rewards)
- Cold daylight / torch inside via `fog_lerp` palette work (no new fog system). Minimal music (same 5-layer synth, no new layers).
- Guardian enemies as reskinned Found Souls with different palette/behaviour flags — no new AI system.
- Place 4–6 Aetherhold-specific Memory Fragments + 1 Major Memory behind the dungeon boss (tracked in the same `FRAGMENT_COUNT` mask, or a new `AETHER_COUNT` if the `Uint32` mask would overflow — decide before adding, hard cap 32).

## Definition of done

- [ ] Causeway exists as a handcrafted bridge segment, closed by default, opens on a single story flag, and `tile_blocked` invariant survives.
- [ ] Castle Island has a handcrafted mask + at least the outer courtyard readable as ruins (walls, gatehouse, paths) — screenshotted, not just counted.
- [ ] Keep is visible from the approach and reachable after the gatehouse.
- [ ] Dungeon entrance hidden and revealed by the same progression that opens the causeway (no separate key hunt).
- [ ] Dungeon modules shuffle order/loot/enemies but keep a linear critical path — verified by a test that would fail if the exit is walled off.
- [ ] Hybrid decoration: the same seed produces different rubble/vegetation placement but identical walls/paths — proven by a determinism check (two runs, fixed seed, diff the mask vs diff the decoration).
- [ ] No new external asset files ship; all new sprites go through `tools/bake.ps1 → src/art_data.h`; `nm` still shows no `SDL_image`/`SDL_ttf`/`SDL_mixer`.
- [ ] Full suite still green (`--land-test`, `--gating-test`, `--play-test` re-run not re-argued, plus new `--aether-test`/`--dungeon-test`), and render stays <2ms at `TILE 18`/`144×138`.

## Concrete tasks (ordered, with file references)

1. Decide the story flag (`castle_unlocked` vs reuse `souls_restored`) and the mask representation before writing any generation code — one `Uint8` flag and one `Uint8 mask[WORLD_H][WORLD_W]`, both beside `WORLD_W` in `src/main.c`.
2. Implement the mask + gate + `--aether-test` (closed/open path, `tile_blocked` still pure) in `src/main.c` beside `dream_sector`.
3. Author first wall/rubble sprites (`assets/castle/wall_*`, `assets/castle/rubble_*`), bake, wire `art_data.h` — keep naming `castle_*`, reuse `draw_building`/`draw_prop` dispatch.
4. Populate courtyard decoration via `tile_hash` (reuse `prop_at` pattern) — no new RNG stream.
5. Add keep elevation (stairs as `SURF_LEDGE` strips, reuse `world_heights` branch) and keep interior.
6. Define dungeon room modules as static `Uint8` patches + shuffle + `--dungeon-test`.
7. Place Aetherhold fragments + major memory, wire to win condition if needed, and run the submission checklist (second-machine, visibility, size).

## Verification gate

- `--aether-test` (closed vs open causeway, mask vs decoration determinism, keep visibility from approach).
- `--dungeon-test` (shuffle is real but exit stays reachable, no module walls off the linear path).
- Full suite re-run: `rng`, `iso`, `font`, `hud`, `fog`, `sprite`, `fade`, `rebuild`, `ground`, `motion`, `sector`, `portal`, `shard`, `save`, `land` (20), `village`, `play` (50), `gating`, `reach`, `bridge`, `region`, `move`, `audio` — all green at `144×138`.
- `nm` + build dir check (no `SDL_image`/`SDL_ttf`/`SDL_mixer`, no shipped PNG).
- Screenshots: approach with castle on horizon, causeway closed vs open, outer courtyard ruins, inner courtyard dead tree, keep interior, dungeon entrance opening.

## Traps specific to this phase

- **A fixed layout that shares the world grid with the procedural village will collide with it if the mask is not placed in the right sector.** The dream realm already occupies `DREAM_Y0`→`WORLD_H`. The castle's handcrafted island must be placed in a *new* sector or on a reserved rectangle that `gen_sector` is told to leave open — otherwise `place_buildings` or `gen_sector` will overwrite it. Decide the reservation before authoring a single wall.
- **A second island that is "just another landmass" re-creates Phase 12's void-band bug.** The castle island must be isolated by water with a single bridge chokepoint, not by adjacency. If the mask touches the overworld by even one tile, the causeway gate is cosmetic.
- **Room shuffling that shuffles walls can wall off the exit.** Keep wall topology fixed per module and only shuffle *contents* (enemies, loot, rubble) unless a connectivity check runs after each shuffle — otherwise the test that proves the exit is reachable will be flaky rather than load-bearing.
- **Restoring the whole castle re-creates the village's "fully restored" confusion.** The castle stays ruined by design — only mechanisms restore. If the restoration% readout is reused for the castle, it must be a separate float or the HUD's "the land is whole" will lie.
- **`tile_blocked` is still not allowed a third input.** The causeway's closed state is not a new collision signal — it is a `solid` tile or a gated region, same as the portal and the bridge. The moment `tile_blocked` reads `castle_mask` directly without going through `solid`/`terrain`, the completability proof must be re-argued.

## Evidence

Not yet built — spec approved 2026-08-06 with the two reference maps (Castle Island + Connecting Land). See `PHASE_13_AETHERHOLD_CHANGES.md` once Slice 1 lands.
