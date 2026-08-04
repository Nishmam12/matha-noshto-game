---
tags: [design, phase, wayfarer]
phase: 10
status: planned
updated: 2026-08-04
---

# Phase 10 — Motion

**Status:** Planned.
**Depends on:** [[Phase 09 - Placeholder Art]] (animating unfinished or soon-to-change art wastes
effort — the things this phase animates should be settled first).
**Blocks:** Nothing downstream; this is the last phase before the hard stop. If time runs out, this
is the correct phase to leave partially done or skip entirely — see the note on sequencing below.

## Why this phase

`devlog/INDEX.md` has carried the same standing risk across multiple sessions: *"Nothing in the
world moves. No sway, shimmer, bob or smoke... a static isometric scene reads as a diorama."*
Motion is scoped last, deliberately, for two reasons: it's the highest-risk-of-wasted-effort phase if
done before the art it animates is settled (Phase 09), and it's genuinely cheap — this project has
~150× render headroom measured repeatedly (0.749–0.859 ms of a 16.67 ms budget across every session
so far), so the byte and performance cost of motion is not the concern. Session time is.

## Definition of done

- [ ] Tree canopies sway — a small per-instance phase and amplitude driven by `tile_hash` plus a
      global time value, so it's deterministic per-tile (consistent with decision 14 in [[Handover]]
      §6: decoration never touches the RNG streams or perturbs generation) rather than genuinely
      random per frame.
- [ ] Water shimmers, and waterfalls (from [[Phase 06 - Water And Bridges]], if that phase landed)
      show a falling motion along their fall line.
- [ ] Restored buildings show chimney smoke, tying into the Phase 09 restoration-rebuild's `1.0`
      threshold state.
- [ ] Firefly-style motes appear in restored regions — a small, deliberately sparse ambient detail
      distinguishing a restored region from an unrestored one beyond just its colour.
- [ ] Found Souls idle-bob rather than sitting perfectly static.
- [ ] None of the above touches simulation state — every motion effect is a pure function of
      `(tile_hash, global_time)` or equivalent, read only at render time, so `--move-test`,
      `--play-test`'s determinism, and every other simulation invariant remain provably unaffected.
- [ ] Perf re-measured via `--frames 400 --perf` after all motion effects land, confirming the
      ~150× headroom claim still holds rather than assuming it does.

## Concrete tasks

1. Add a global elapsed-time value accessible at render time (likely already implicit via the fixed
   60 Hz tick count, `src/main.c`'s main loop) — confirm what's already available before adding a
   new state field.
2. Tree sway: perturb each tree's lobe offsets in `draw_tree` (`src/main.c:2117`, post-Phase-01
   `fill_ellipse` version) by a small sinusoidal function of `(tile_hash, time)`, keeping the
   amplitude small enough that silhouette identity (per [[Art Bible]] §1, "silhouette before detail")
   isn't lost — this is a wobble, not a different tree shape each frame.
3. Water shimmer: a similar per-tile time-varying perturbation to the water ramp shade selection in
   `tile_colour`'s ocean/river branch, plus a directional scroll for waterfall fall-lines if Phase 06
   landed.
4. Chimney smoke: a small particle-like effect (a handful of rising, fading marks) triggered by the
   Phase 09 restoration-rebuild's fully-restored state — likely reusing `tile_detail`'s positioning
   approach (hash-derived offsets within a bounded area) rather than inventing a new particle system.
5. Fireflies: sparse, restored-region-only motes — position from `tile_hash`, motion from time, kept
   deliberately rare (this is ambient detail, not a light show) per [[Art Bible]] §3's hero-vs-
   supporting-shape distinction.
6. Found Soul idle bob: a small vertical sinusoidal offset applied wherever Found Souls are currently
   drawn — check `draw_prop`/entity-drawing code for where they're currently rendered statically.
7. Re-run `--frames 400 --perf` after each sub-piece lands, not just at the end — motion effects are
   exactly the kind of feature that can silently accumulate render cost across several small
   additions, and per-piece measurement catches that before it compounds.

## Verification gate

`--frames 400 --perf` showing render cost remains a small fraction of the 16.67 ms budget. Full
existing suite re-run to confirm zero simulation impact — `--move-test`, `--play-test --seeds 50`
still 50/50, `--rng-test` still passing (motion must not touch any RNG stream). Visual confirmation
via `--autoplay` (watching motion happen over a real play session) rather than only static
screenshots, since motion is definitionally not verifiable from a still frame.

## Traps specific to this phase

- **This is the correct phase to cut or partially skip if the 2026-08-14 hard stop arrives first.**
  Unlike Phases 01–08, nothing downstream depends on this phase being complete — [[Phase 11 - Ship
  Critical]] does not need motion to exist. If forced to choose, ship fewer motion effects rather
  than delaying Phase 11's start.
- **A time-driven effect that isn't seeded from `tile_hash` will look identical across every
  instance of the same object** (every tree swaying in exact lockstep, for instance), which reads as
  worse than no motion at all — mechanical rather than alive. Always combine the global time value
  with a per-tile hash offset, never use global time alone.
- **Perf headroom being large today doesn't mean every motion effect is free** — several small
  additions (sway + shimmer + smoke + motes + bob) each individually cheap can still add up across a
  full frame with many trees, several water tiles, and multiple restored buildings on screen at once.
  Measure per-piece, per the Concrete Tasks list, not only once at the end.

## Evidence

Not yet started.
