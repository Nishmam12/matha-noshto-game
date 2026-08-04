---
tags: [design, phase, wayfarer]
phase: 9
status: planned
updated: 2026-08-04
---

# Phase 09 — Placeholder Art

**Status:** Planned.
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

- [ ] Buildings show windows, a door, and a foundation course on their visible wall faces — currently
      walls are drawn only by the tile rasteriser's front-face extrusion (`bld_at` height feeding
      `iso_tile`'s side faces), with no additional detail.
- [ ] The **restoration-driven rebuild** exists: a building's drawn state is a function of its
      region's `restoration` float, expressed as parts suppressed rather than a second art set —
      `0.0` walls only and gapped, `0.4` roof partial, `0.7` roof and windows complete, `1.0` windows
      lit and chimney smoking. This is named in the original Handover as "the highest-value item
      remaining" because it makes the game's own stated hook (*restore memories, rebuild lives*)
      literally visible, and it reuses `regions[].restoration`, which already eases smoothly.
- [ ] The player is a layered character (body/head/cloak in a small shade ramp, one accent colour),
      not a 24×24 flat orange square — with 8-way facing and a 2-frame walk bob at minimum.
- [ ] Worn paths render between buildings within a village cluster, derived from the building layout
      — render-only, no collision impact — because the reference image's roads are what makes a
      cluster of houses read as *inhabited* rather than *placed*.
- [ ] Ground texture gains more mark *kinds* (grass tufts with a lit edge, mortar courses on cliff
      faces, shoreline pebbles) rather than more marks of the existing kind — [[Handover]] already
      notes the current two-tone dither is the weakest surface treatment in the renderer.
- [ ] Everything in this phase is built through the Phase 07 dispatch seam, even where it's
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

Not yet started.
