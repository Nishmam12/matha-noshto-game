---
tags: [design, phase, wayfarer]
phase: 5
status: done
updated: 2026-08-05
---

# Phase 05 — Verification Debt

**Status:** DONE — `4847bf5`, +0 shipping bytes.
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

- [x] `--land-test --seeds N` exists and asserts, across many seeds: **the bounded-and-justified
      option was taken**, and the justification is in Evidence — the assertion is about the
      component the player *spawns in*, not about the map being one piece. Ocean is a contiguous
      body touching the map border; rock stays under 40%; buildable ground exists at all.
- [x] `--land-test` has a negative control — two, in fact: a drowned map and a shattered one.
- [x] `--fog-test` exists and asserts luminance ordering across reveal 0.0/0.25/0.5/0.75/1.0, plus a
      second property the phase file did not ask for but Phase 02 actually needed: **shade
      separability within a palette ramp at reveal 0**.
- [x] `--fog-test` has a negative control: a contrast-crushing blend, caught by both checks.
- [x] Both added to [[Handover]] §4's command block, and its "rule debt" callout removed.

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

Built 2026-08-05, session 02 — `4847bf5`, **691,200 bytes, +0**. Both checkers live entirely
inside `#if WAYFARER_SELFTEST`.

```
land : PASS over 100 seeds; both negative controls fire
fog  : separability 0 collapsed ramps; ordering 45 colours x 5 reveals,
       0 inversions, 3 collapses; negative control caught with
       14 collapsed ramps and 206 collapsed pairs
```

### The finding, and why `--land-test` asserts what it does

The obvious assertion — *the walkable landmass is a single connected component* — **fails on 3 of
100 seeds** (16, 85, 100). That number is the whole reason this phase was worth doing, and the
interesting part is what it turned out to mean.

Inspecting those seeds shows the generator producing a **detached lobe across open water**. That is
not a defect:

- ocean is `solid` and never walkable, so a lobe across it is unreachable by construction;
- `place_entities` only ever places into regions reachable from the spawn, so no content is
  stranded there;
- all three seeds still play to completion — `--play-test` is 50/50 throughout.

An unreachable islet on the horizon is a *feature* of an island game, not a bug. What would
genuinely be broken is the player spawning on a small lobe with most of the world across water.

So the assertion measures **the component the player actually spawns in, as a fraction of the whole
map** — absolute, per [[Handover]] §7's warning that relative assertions compare counts to counts.
Seed 16 is the worst case at 56% of open ground and 17.8% of the map, and it passes **on merit**,
not because a bound was moved to let it through. A second, looser bound (the player must reach at
least half the walkable ground) catches genuine shattering.

### Why the negative control shatters the map into four, not two

A two-way split leaves the player reaching ~50% of open ground — exactly on the fragmentation
bound. **A control that only just fires is a control that stops firing the first time somebody
nudges a threshold.** At four quarters the player reaches ~25% and it fires with room to spare.

### What this phase did NOT verify

- **`--fog-test` reports 3 "collapses"** — pairs of colours that become equal under rounding at
  some reveal level. They are reported rather than failed, on the grounds that a tie is a loss of
  information but not a *lie* about the value hierarchy the way an inversion is. Whether 3 is
  acceptable has not been judged by eye.
- **Neither test says anything about whether the result looks good.** `--land-test` bounds coverage
  and connectivity; it has no opinion on whether a coastline is attractive. `--fog-test` bounds the
  value hierarchy; it cannot tell you the haze is the right colour. Those remain screenshot-and-F3
  judgements.
- **The 40% rock bound and the 12.5%/87.5% land bounds are chosen, not derived.** They bound the
  failure modes Phase 01 actually hit; they are not claims about the ideal range.
