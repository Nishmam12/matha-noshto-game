---
tags: [design, phase, wayfarer]
phase: 1
status: done
updated: 2026-08-04
---

# Phase 01 — Landform

**Status:** DONE — committed as `5ffdb38`, "Island landform, clustered villages, and rounded
foliage".
**Depends on:** [[Phase 00 - Foundations]] (the Art Bible's value hierarchy and "everything touches
the ground" principle directly shaped this phase's fixes).
**Blocks:** [[Phase 02 - Roof And Fog]] (shading fixes on top of an incoherent landform would have
been wasted work), [[Phase 05 - Verification Debt]] (owes `--land-test`), [[Phase 06 - Water And
Bridges]] (needs the ocean body this phase introduces).

## Why this phase

The user's original complaint was "the models are a mess" and, separately, that traversal felt
suffocating. Investigating the second complaint first (screenshotting the fogged play view) showed
the suffocation was fog, not the camera — but investigating the *first* complaint required looking at
the unfogged (`--overlay`) view, and that's where the real structural problem showed up:
`world_heights` derives terrace height from distance-into-solid-mass, and the solid mass came from a
cellular-automaton cave generator. **The landform itself — not just its shading — was cave noise.**
On screen this read as random brown lumps with no coastline anywhere, which no amount of texturing or
palette work could fix, because the problem was the shape, not the surface.

## Definition of done

- [x] World generation produces a coherent island: a coastline, an ocean body (not scattered water
      tiles), and inland rock structure that isn't uniform-random.
- [x] Collision truth is unchanged — `solid[][]` is still the only thing `tile_blocked` reads.
      Ocean and rock differ only in a new render-only field.
- [x] Buildings read as villages (clustered, with walkable ground between them), not as a suburb
      covering the island.
- [x] The full pre-existing test suite passes without modification to any test's *assertions* —
      only the generator changed, not what correctness means.
- [x] At least two visual defects found by screenshotting are fixed before calling this phase done,
      because the definition of done for a *landform* phase has to include "does it look like land."

## Concrete tasks (as executed)

1. **Replaced `world_gen`** (`src/main.c:659`, was a 4-pass cellular automaton) with a radial height
   field: `(1 - normalised_distance_from_centre)` plus two octaves of smoothstepped value noise
   (`land_lattice` + `land_noise`, `src/main.c:623`/`633`) pushing the coastline in and out, plus a
   third, lower-frequency octave (`LAND_ROCK_*` lattice) raising inland rock outcrops. Thresholded at
   `LAND_SEA` for ocean, `LAND_ROCK_T` for outcrops.
2. **Added `surf[][]`** to `World` (`src/main.c:525`) — `SURF_LAND` / `SURF_ROCK` / `SURF_OCEAN`,
   written by `world_gen` alongside `solid`, read only by rendering and `world_heights`. This is
   what let ocean and rock both stay `solid` (nothing can walk through either) while still being
   drawn differently.
3. **Extended `world_heights`** (`src/main.c:1415`) with an ocean case: a stepped sea-floor ramp
   (`ELEV_WATER` minus up to 3×4 px by distance from shore) instead of treating open water as the
   interior of a rock mass and raising it to `ELEV_MAX` — which is what the naive port of the old
   chamfer logic would have done.
4. **Reworked `place_buildings`** (`src/main.c:718`) to pick up to `VILLAGE_SITES` cluster centres
   first (rejected if too close to another site, via `VILLAGE_SPACING`), then draw plots from two
   summed `rng_below` calls so they bunch toward each site's centre and thin at the edge
   (`VILLAGE_RADIUS`). Footprint size cut from 2–4 tiles to 2–3, and the required open skirt around
   each footprint widened from 1 tile to 2, so clusters have walkable lanes rather than reading as
   one continuous terrace of roofs.
5. **Applied a first palette pass**: grass off the saturated `0x4e9e54` onto a sage `0x3e5c35`,
   water given a 4-step depth ramp instead of one flat blue, stone given tile-hashed tonal variation
   instead of one flat grey, and the two autumn canopy palettes **deleted** — at the reveal level
   walking alone ever reaches (then 0.42), they read as dead rather than autumn, and a quarter of
   the world's trees used them.
6. **Fixed three defects found only by screenshotting**, none of which any existing test could have
   caught because they're about *appearance*, not *correctness*:
   - Flat-topped outcrop "slabs" — every small rock mass had `dist == 1` everywhere, producing an
     identical flat 12 px platform. Fixed by making outcrops fewer and larger (lower `LAND_ROCK_W`/
     `LAND_ROCK_H` lattice resolution) so the chamfer distance transform has room to produce several
     terraces inside one mass, plus a small height jitter.
   - Checkerboard jitter — see the Traps section; the first jitter attempt was wrong and got fixed
     within this same phase.
   - Canopy "broccoli" — lobes were axis-aligned `fill_rect`s, which are a stepped pyramid however
     their widths are chosen. Added `fill_ellipse` (`src/main.c:2028`) and switched every canopy
     lobe, bush, and prop contact shadow to use it.

## Verification gate

Ran and passed (30 seeds unless noted): `--village-test` (mean 12 buildings/world, both negative
controls fire), `--region-test`, `--reach-test` (50 seeds, negative control fires, gating relaxed on
0/50), `--gating-test`, `--play-test` (50 seeds, **50/50 completed** — unchanged from before the
landform rewrite), `--move-test`, `--iso-test`, `--rng-test`. All PASS, zero warnings.

**Owed and not yet paid:** `--land-test` does not exist. Nothing currently asserts, across many
seeds, that the island generator reliably produces a connected coastline, a reasonable land/ocean
ratio, or rock coverage within a sane range — only that whatever it produces still satisfies the
*pre-existing* reachability and region invariants. See [[Phase 05 - Verification Debt]].

## Traps specific to this phase

- **A per-tile height jitter checkerboards.** The first attempt at breaking up flat outcrop tops
  hashed a ±4 jitter per individual tile. Adjacent tiles almost always disagreed, so the rasteriser
  drew a visible step between every pair — a checkerboard, not rock. Fixed by hashing on the 2×2
  block (`(x >> 1, y >> 1)`) instead of the tile, at a smaller ±2. Full detail in [[Handover]] §7.
- **A `village_selftest` mean reflects threshold choices, not bugs.** Moving `VILLAGE_RADIUS` from 6
  to 9 changed the mean building count from 5 to 12 with every existing assertion still passing —
  the test checks placement *validity*, not density against a design target.
- **Rock threshold has a wide sensitive range.** `LAND_ROCK_T` at 0.68 covered roughly 40% of a
  typical screen in rock (a "quarry" read); 0.74 was needed to bring it back to something that reads
  as hillside terrain. If retuning, screenshot before trusting the number.

## Evidence

- `690,176 bytes` after this phase (was `689,152` before), **+1,024 bytes total** across all of
  Phase 01's changes.
- Full suite output and screenshots are logged in `devlog/2026-08-04-session-01.md` under
  "Session 02 — the landform, and a first honest look at it."
