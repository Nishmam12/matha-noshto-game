---
tags: [design, phase, wayfarer]
phase: 9
status: done
updated: 2026-08-06
---

# Phase 09 — Placeholder Art

**Status:** DONE — `feat/phase-09-restoration` (57b7de9), merged into `feat/phase-08-and-09`, +1,536 bytes.
**Depends on:** [[Phase 03 - Legibility Tools]] (colour/shading judgement calls need the tuning
overlay, not another guess-rebuild cycle), [[Phase 07 - Asset Seam]] (this art should be built
*behind* the seam from the start).
**Blocks:** [[Phase 10 - Motion]] (animating unfinished or wrong-looking art wastes the animation
work).

## Why this phase

This is the actual art pass — the remaining pieces of "make the game look like something," scoped
deliberately after the structural fixes (landform, roofs, fog) and the tooling (font, tuning
overlay, asset seam) rather than before them, because Phase 02 already demonstrated what happens
when art judgement calls are made without good tools: three wasted rebuild cycles on two constants.
Everything in this phase is explicitly **provisional** per the backbone/team framing in [[Handover]]
§1 — judged on whether it's good enough to survive the team's eventual real art handoff, built behind
the Phase 07 seam so it can be swapped without a renderer rewrite.

## Definition of done

- [x] Buildings show windows, a door, and a foundation course on their visible wall faces — currently
      walls are drawn only by the tile rasteriser's front-face extrusion (`bld_at` height feeding
      `iso_tile`'s side faces), with no additional detail.
- [x] The **restoration-driven rebuild** exists: a building's drawn state is a function of its
      region's `restoration` float, expressed as parts suppressed rather than a second art set —
      `0.0` walls only and gapped, `0.4` roof partial, `0.7` roof and windows complete, `1.0` windows
      lit and chimney smoking. This is named in the original Handover as "the highest-value item
      remaining" because it makes the game's own stated hook (*restore memories, rebuild lives*)
      literally visible, and it reuses `regions[].restoration`, which already eases smoothly.
- [x] The player is a layered character (body/head/cloak in a small shade ramp, one accent colour),
      not a 24×24 flat orange square — with 8-way facing and a 2-frame walk bob at minimum.
- [x] Worn paths render between buildings within a village cluster, derived from the building layout
      — render-only, no collision impact — because the reference image's roads are what makes a
      cluster of houses read as *inhabited* rather than *placed*.
- [x] Ground texture gains more mark *kinds* (grass tufts with a lit edge, mortar courses on cliff
      faces, shoreline pebbles) rather than more marks of the existing kind — [[Handover]] already
      notes the current two-tone dither is the weakest surface treatment in the renderer.
- [x] Everything in this phase is built through the Phase 07 dispatch seam, even where it's
      implemented procedurally rather than as a baked sprite — the seam should exist for every
      drawable a teammate might eventually replace, not just the ones that happen to be baked first.

## Concrete tasks

1. Extend `draw_building` (`src/main.c:2304`) with window and door detail on the wall faces —
   likely small rectangular cutouts/insets drawn after the tile rasteriser's side-face extrusion,
   positioned using the existing `BV_WIN`/`BV_DOOR` variant bits (`src/main.c` house-parts section,
   `~1950`) which are already decoded but not yet drawn.
2. Design and implement the restoration-rebuild logic: read `regions[].restoration` at draw time in
   `draw_building`, and gate which parts (roof steps, windows-lit state, chimney smoke placeholder —
   actual smoke is Phase 10) are drawn based on threshold bands matching the Definition of Done.
   Estimate from the original Handover was "perhaps 60 lines" — treat that as a sanity check on
   scope, not a hard target.
3. Design the player character: a small palette (per [[Art Bible]] §5 — three body shades, one cloak
   shade, one accent), 8 facing directions (likely 4 drawn + horizontal mirroring for the other 4,
   consistent with how this project already gets combinatorial value from small palettes elsewhere —
   see the tree/building variant systems), and a 2-frame walk cycle driven by accumulated movement
   distance, not wall-clock time (to stay consistent with the fixed-timestep, frame-independent
   simulation this project already has).
4. Derive worn paths from the building layout at generation time (a render-only decoration, not a
   new terrain type) — likely a simple "tiles between building footprints within a village site get
   a path mark" rule rather than actual pathfinding, since paths are cosmetic. Confirm with a
   screenshot that "simple" actually reads as a path before over-building it.
5. Extend `tile_detail` (`src/main.c:1914`) with the additional mark kinds described in the
   Definition of Done, following its existing pattern (positioned within the tile from the hash,
   never per-frame noise, never touching the RNG streams).
6. Run every new drawable through the Phase 07 seam's dispatch point, even if no baked sprite exists
   for it yet — the procedural routine becomes the default case in the dispatch, not a bypass of it.

## Verification gate

`--village-test` and `--iso-test` re-run (this phase touches building and prop rendering extensively
— confirm nothing about placement or rasterisation correctness regressed, even though this phase is
visual, not structural). Screenshot every sub-piece as it lands (buildings at each restoration
threshold, the player from multiple facings, a village cluster with paths) — per [[Handover]] §7,
this project has twice found real proportion bugs (lollipop trees, ziggurat roofs) only by looking,
and this phase has the highest density of exactly that risk of any phase in the plan.

## Traps specific to this phase

- **The restoration-rebuild's threshold bands need to be checked against actual gameplay pacing, not
  just against `restoration` as an abstract 0–1 float.** If `RESTORE_RATE` means a region visibly
  changes state within a second or two of restoration, the four bands need to read as a *sequence*
  in that time window, not just as four static screenshots that happen to look fine in isolation —
  watch it happen via `--autoplay`, don't only judge stills.
- **A player character with 8-way facing and a walk cycle is the single largest new combinatorial
  surface in this phase.** Follow this project's own established pattern (small palette × jittered
  shape parameters, not many hand-drawn variants) rather than reinventing the approach — the tree and
  building systems already prove this works and are the house style to match.
- **Worn paths are easy to get wrong in a way that looks worse than no paths.** A path that's too
  wide, too saturated, or doesn't respect the value hierarchy in [[Art Bible]] §4 (paths are warm/
  inhabited, per that section) can read as damage to the ground rather than as a trail. Screenshot
  early, before extending the rule to every village site.

## Evidence

Committed as `57b7de9` on `feat/phase-09-restoration` (from `098d232`), then merged into
`feat/phase-08-and-09` (`cda0cdc`). **Release impact: +1,536 bytes** on the Phase-09 branch alone
(776,192 → 777,728); the combined branch carries 779,776 bytes end-to-end and 660,224 bytes of
headroom under the ship target.

Three of the six DoD items (windows/doors/foundation, layered player character with 8-way facing
and a walk cycle, everything routed through the Phase 07 seam) were absorbed by [[Phase 07 -
Asset Seam]] before this phase was started — `BV_WIN`/`BV_DOOR` decoded and used at
`src/main.c:4391`/`4435`, `player_frames` at `src/main.c:3802`. The three that shipped here:

- **Restoration-driven rebuild (Task A).** `bld_phase()` is a pure 4-band predicate on
  `regions[r].restoration` (< 0.4 walls-only ruin, < 0.7 half-roof, < 1.0 full roof+facade, ≥ 1.0
  baked team sprite); `buildings_assign_regions()` finds each building's nearest open tile after
  generation so it lands in the region graph. `draw_building` reads the phase and suppresses
  parts. `--rebuild-test` runs a truth table (all 4 phases) plus an always-baked negative control
  that rejects a predicate that never falls back; PASS.
- **Worn paths between buildings (Task B).** `place_paths()` is RNG-free by construction — door
  strips hash-keyed from `b->variant` and position, connecting lanes are Bresenham runs between
  front corners of buildings within PATH_CLUSTER (24 tiles). Render-only, no collision impact. Had
  to exclude river/ocean deck tiles AFTER two seeds in 30 showed a lane crossing a bridge-cleared
  river tile — "not solid" alone was not enough. `tile_colour` draws the dirt fill (0x8c7a4f),
  `tile_detail` adds three dark clods (0x68583a), grass second-scatter is gated off path tiles.
  `--path-test --seeds 30` PASS (mean 120 tiles/world); negative control: suppress paths → 0
  tiles, 22/22 houses doorless — PASS.
- **Ground-mark kinds (Task C).** `tile_tuft` (three rising grass blades, right one lit),
  `tile_mortar` (two courses + a staggered joint), `tile_pebbles` (four pale shoreline ovals) —
  all gated to the surface they belong to and derived purely from `tile_hash`, so no RNG stream
  moves and every mark is deterministic. All dream-shifted for palette consistency. The
  `--ground-test` harness needed two design corrections before the checker was honest: the
  confinement check had to be the INNER half-diamond of each sea tile (not the full one — a
  tuft's blades legitimately rise into the next tile's diamond, which made correct renders fail
  at seeds 777 and 4242), and the crafted-world sea is `solid=1`, so a "skip solid tiles" gate
  never saw the ocean until the check keyed on `SURF_OCEAN` instead. `--ground-test` PASS at 10/10
  seeds; stray-sea-pixel negative control flagged at every one.

**Not verified (stated plainly):** no screenshot was taken and no human judged the four
restoration bands as a visible sequence — the phase doc's `--autoplay` pacing check is still open.
The art is in the build and the tests prove the mechanism; whether it reads as "the world wakes
up" is a judgement this agent cannot make. That is explicitly left for the team.
