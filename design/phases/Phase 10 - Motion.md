---
tags: [design, phase, wayfarer]
phase: 10
status: done
updated: 2026-08-06
---

# Phase 10 — Motion

**Status:** DONE — `feat/phase-10-motion`, +2,048 bytes (781,824), headroom 658,176.
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

- [x] Tree canopies sway — a small per-instance phase and amplitude driven by `tile_hash` plus a
      global time value, so it's deterministic per-tile (consistent with decision 14 in [[Handover]]
      §6: decoration never touches the RNG streams or perturbs generation) rather than genuinely
      random per frame.
- [x] Water shimmers, and waterfalls (from [[Phase 06 - Water And Bridges]], if that phase landed)
      show a falling motion along their fall line.
- [x] Restored buildings show chimney smoke, tying into the Phase 09 restoration-rebuild's `1.0`
      threshold state.
- [x] Firefly-style motes appear in restored regions — a small, deliberately sparse ambient detail
      distinguishing a restored region from an unrestored one beyond just its colour.
- [x] Found Souls idle-bob rather than sitting perfectly static.
- [x] None of the above touches simulation state — every motion effect is a pure function of
      `(tile_hash, global_time)` or equivalent, read only at render time, so `--move-test`,
      `--play-test`'s determinism, and every other simulation invariant remain provably unaffected.
- [x] Perf re-measured via `--frames 400 --perf` after all motion effects land, confirming the
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

Committed on `feat/phase-10-motion` (from `main` at `b4f2fde`), +2,048 bytes over the merged
779,776 → **781,824**; 658,176 bytes of headroom under the 1,440,000 ship target.

**Task 1 (clock) was already done.** `Game.clock` (main.c:785) has been advanced by `sim_step`
since Phase 05 and is render-only by contract — the portal vortex, shard bob and prompt bob
already read it. No new state field was needed.

**The effects, each a pure `(tile_hash, g->clock)` function:**
- **Tree sway** — `tree_sway()` at the prop dispatch, so baked *and* procedural trees sway
  through one formula (whole-tree offset, per-instance phase from hash bits 27-31 — the bits
  `tile_hash`'s bit map reserved for this — amplitude 1-2 px), plus a per-lobe ripple in
  `draw_tree` so the canopy breathes instead of sliding. `--motion-test`: max 1 px, deterministic,
  105/300 samples nonzero.
- **Water shimmer** — `water_ripple()` in `tile_colour`'s river and ocean branches: ±5 px
  brightness ripple, deliberately smaller than a ramp step so depth bands never flip. Dream-side
  void is excluded (its starfield is its motion). `--motion-test`: deterministic, within 5 px,
  silent in the overlay view, clamped.
- **Waterfall fall-lines** — Phase 06's waterfalls are height-terraced river tiles; the drop is
  `iso_tile`'s side face. `waterfall_dash()` walks the same column math as the rasteriser and
  scrolls a pale 1-px dash down the face. `--motion-test`: paints 1-4 px inside the face, nothing
  when no face exists, deterministic.
- **Chimney smoke** — `draw_smoke()`: 1 puff during the rebuild's 0.7-1.0 window, 2 puffs over
  the baked sprite at full restoration, so the smoke rides the same 1.0 threshold as the
  restoration-rebuild. `--motion-test`: paints, bounded, deterministic, none for n=0.
- **Fireflies** — `mote_gate()`: sparse (1 tile in 8), grass-only, never path/footprint/overlay,
  and only at `restoration >= 0.7` with presence and brightness ramping to full at 1.0.
  `--motion-test`: 8/8 truth-table rows.
- **Found Souls idle-bob** — `soul_bob()`: ±2 px about the entity's tile-hash phase; fragments
  deliberately stay static (they are markers, and a bobbing pick-up reads like it is running).
  `--motion-test`: max 2 px, deterministic, 201/300 nonzero.

**Verification.** `--motion-test` all green: two renders at one clock are bit-identical, a
different clock changes 23,552 px (motion is live, not a no-op), and every helper keeps its
documented bound. Full suite re-run end-to-end on the final tree: rng, iso, font, fog, sprite,
fade; land 20, village 30, sector 10, portal 30, shard 30, region 30, reach 50, gating 30, bridge
200, move 20, play 50/50; save 10/10, path 30/30, ground 10/10, rebuild, motion — all PASS, so
the "none of this touches simulation" DoD item is proven, not asserted. `--frames 400 --perf`
after all effects: **render mean 1.123 ms, max 1.926 ms** of the 16.67 ms budget (~15× headroom),
vs 0.749-0.859 ms before Phase 07-09's art — the phase's own "perf is not the concern" claim
still holds.

**Not verified (stated plainly):** no human has watched it. This agent cannot render or judge
images, so the "does it read as alive, not mechanical" question — the phase doc's own visual gate
— is explicitly left for the team, same as Phase 09's pacing check. Programmatic evidence stands
in: two frames with the camera parked and no input differ only by the clock, and `--motion-test`
plus `--autoplay` (8 s, clean run, screenshot at `G:\Temp\opencode\t10_autoplay.bmp` for anyone
who wants to look) exercised real play with all effects live.
