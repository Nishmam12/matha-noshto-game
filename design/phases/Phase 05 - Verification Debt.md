---
tags: [design, phase, wayfarer]
phase: 5
status: planned
updated: 2026-08-04
---

# Phase 05 — Verification Debt

**Status:** Planned.
**Depends on:** [[Phase 01 - Landform]], [[Phase 02 - Roof And Fog]] (both systems already exist and
are stable; this phase writes the tests that should have shipped alongside them).
**Blocks:** [[Phase 06 - Water And Bridges]] — rivers and bridges extend both the landform generator
and the fog-adjacent rendering, and both should have their own checker *before* being extended
further, not after.

## Why this phase

[[Agent Prompt]] and [[Handover]] both state a rule that has held for every system in this project
except the two most recent ones: **any new checker needs a negative control, and every generator or
render contract needs its own checker.** The island generator (Phase 01) and the fog rewrite
(Phase 02) both shipped without one. This isn't a hypothetical gap — it already had a real
consequence: the fog constants went through three rebuild-and-screenshot tuning passes in Phase 02
partly because there was no automated way to check whether a candidate value preserved shade
separability; every check was a human looking at a picture.

This phase exists to pay that down explicitly, before it compounds. Every phase after this one that
touches the landform (Phase 06) or the palette (Phase 07, 09) builds on systems that currently have
zero automated protection against a regression a screenshot-based session might not catch.

## Definition of done

- [ ] `--land-test --seeds N` exists and asserts, across many seeds: the walkable landmass is a
      single connected region before the region graph fragments it further (or a bounded, justified
      number of components); ocean forms a contiguous body reachable from the map edge, not isolated
      lakes with no path to open water; rock coverage stays within a sane range (bounding the
      "quarry" failure mode from Phase 01's tuning); every seed produces *some* buildable flat
      ground (an implicit precondition `place_buildings` currently assumes but nothing checks).
- [ ] `--land-test` has a negative control: a seed or a parameter deliberately pushed to produce a
      broken world (e.g., `LAND_SEA` pushed to a value that drowns the entire map) must be *rejected*
      by the checker, proving it can fail, not just always pass.
- [ ] `--fog-test` exists and asserts: for a representative set of source colours (grass, foliage
      shades, stone, water), the luminance ordering among them is preserved after `fog_lerp` at
      reveal 0.0, 0.25, 0.5, 0.75, 1.0 — i.e., a lighter shade stays lighter than a darker one at
      every reveal level, which is the property Phase 02 needed and never measured.
- [ ] `--fog-test` has a negative control: a deliberately broken blend (e.g., one that clamps toward
      a single flat value regardless of input) must be caught and reported as a failure.
- [ ] Both new tests are added to the standard verification routine listed in [[Handover]] §4 and
      run as part of every subsequent phase's gate.

## Concrete tasks

1. Design `--land-test`'s assertions against the actual generator in `world_gen`
   (`src/main.c:659`) and `land_noise`/`land_lattice` (`src/main.c:623`/`633`) — read the current
   implementation before writing the test, since the thresholds (`LAND_SEA`, `LAND_ROUGH`,
   `LAND_ROCK_T`) are the specific values whose *sensitivity* this test needs to bound (Phase 01's
   devlog already found `LAND_ROCK_T` has a narrow good range; encode that finding as an assertion,
   not just a comment).
2. Reuse the existing `flood_open`/`bfs_open` machinery (`src/main.c:1362`/`813`) for the
   connectivity assertions rather than writing a second flood-fill — this project already has one
   that's proven correct via the reachability tests.
3. Write the negative control by parametrizing the threshold constants for the test build only (or
   by constructing a synthetic broken `World` directly, whichever is cheaper) — follow the existing
   pattern in `solvable_negative_test` (`src/main.c:3425`) and `village_negative_test`
   (`src/main.c:4112`) for how this project structures a negative control.
4. Design `--fog-test`'s assertions against `fog_lerp` (`src/main.c:1868`) directly — this is a pure
   function of `(r, g, b, reveal)`, so the test can call it directly with representative palette
   values pulled from the current ramps (`wall_pal`, `canopy_pal`, `terrain_colour`'s cases) rather
   than needing a rendered frame.
5. Add both to the standard command block in [[Handover]] §4 once they exist, and note in that
   section that the "rule debt" callout can be removed.

## Verification gate

Both new tests pass, including their negative controls, and both are demonstrated to actually fail
when their assertion is violated (run the negative control, confirm it reports failure) before being
trusted as gates for later phases.

## Traps specific to this phase

- **A test that never rejects anything proves nothing** — this is the exact rule this phase exists
  to satisfy, so it would be a particular kind of failure for either new test to ship without a
  working negative control. Verify the negative control actually fires by running it, not by reading
  the code and assuming it's correct.
- **Relative assertions are not correctness** ([[Handover]] §7) — this project has been burned once
  already by a structural test that checked counts against counts and passed on a partition covering
  a tiny fraction of the walkable map. `--land-test`'s connectivity assertion should check against an
  absolute expectation (e.g., "the largest component contains at least N% of non-solid tiles"), not
  merely "the components found are internally consistent with each other."
- **`--fog-test` needs representative colours, not arbitrary ones.** Picking test colours that
  happen to already be far apart in luminance would make the test pass trivially without actually
  exercising the failure mode Phase 02 hit (two colours close enough in value that the blend's
  rounding or clamping could reorder them). Pull test values from the *actual* palette tables so the
  test exercises real adjacency, not a convenient synthetic case.

## Evidence

Not yet started.
