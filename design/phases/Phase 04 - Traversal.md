---
tags: [design, phase, wayfarer]
phase: 4
status: planned
updated: 2026-08-04
---

# Phase 04 — Traversal

**Status:** Planned.
**Depends on:** [[Phase 03 - Legibility Tools]] (not a hard technical dependency, but sequenced
after it so the tuning overlay is available if camera-feel constants need iterating the way fog
constants did in Phase 02).
**Blocks:** Nothing downstream directly, but this is the last **simulation** change planned before
Phase 11's ship-critical work begins, and [[Handover]] names it as still mattering to how the game
feels to actually play.

## Why this phase

Two related but distinct traversal defects, both diagnosed, neither fixed:

1. **Input is world-aligned, not screen-aligned.** `sim_step` (`src/main.c:1328`) takes `W`/`S`/`A`/
   `D` directly as `±y`/`±x` in world space, but the isometric projection means world `+x` draws as
   screen down-right. Pressing `W` moves the player up-*right* on screen, not up. This has been a
   named open decision since before the isometric pivot and was deliberately deferred because fixing
   it rewrites every trajectory in the simulation.
2. **The camera hard-snaps to the player with no easing.** `camera_follow` (`src/main.c:2788`)
   centres the view on the player every frame with only edge clamping, no smoothing or deadzone —
   every step is a visible camera jump at pixel scale, most noticeable on diagonal movement.

Both were left explicitly out of Phases 01–02 because [[Agent Prompt]] requires flagging and
isolating changes to the core simulation, and input orientation is exactly that: it invalidates
`--move-test`'s diagonal-speed assertion, `--input-test`'s expected deltas, and the autopilot's
steering logic in `--play-test`/`--autoplay`. Bundling it with any render-only change would make it
impossible to tell, if something broke, whether the cause was the simulation change or the render
change.

## Definition of done

- [ ] Pressing `W` moves the player up on screen; `D` moves right; diagonals compose correctly.
- [ ] Collision behaviour (swept AABB, wall sliding via independent-axis resolution) is unchanged —
      only the mapping from input to world-space velocity changes, not how movement resolves once
      that velocity is known.
- [ ] `--move-test`'s diagonal-speed assertion still holds (both straight and diagonal movement at
      220.00 px/60 ticks) — the assertion itself may need updating to reflect which *input* now
      counts as diagonal, but the underlying physical invariant (direction-independent speed) must
      still be true and still be tested.
- [ ] `--input-test` and the autopilot steering in `--play-test`/`--autoplay` are updated to the new
      mapping, not deleted or skipped.
- [ ] `--play-test --seeds 50` is still 50/50 after the change.
- [ ] The camera has some form of easing/deadzone and no longer produces a visible snap on every
      simulation step; this part is render-only and can be verified by eye plus a perf check that it
      didn't add meaningful render cost.

## Concrete tasks

1. **Isolate the input-orientation change into its own commit**, separate from the camera-easing
   change, even though both live in this phase — they're independent enough (one is simulation, one
   is render) that mixing them in one diff makes a future regression harder to bisect.
2. In `sim_step` (`src/main.c:1328`) or `input_poll` (`src/main.c:1247`), rotate the raw up/down/
   left/right input through the inverse of the isometric projection before it becomes a world-space
   velocity: `wx = (sx + sy) * 0.70710678`, `wy = (sy - sx) * 0.70710678`, where `sx`/`sy` are the
   screen-space intent (`W` = screen-up = `sy -1`, etc.). Keep `move_axis` (`src/main.c:1189`)
   untouched — it resolves a world-space velocity into collision-respecting motion and has no
   opinion about where that velocity came from, which is exactly why it doesn't need to change.
3. Update `move_selftest` (`src/main.c:2883`), `input_selftest` (`src/main.c:3613`),
   `move_determinism_test` (`src/main.c:3673`), and the autopilot's direction table in
   `autopilot_tick` (`src/main.c:3232`) to the new mapping. Do not delete any test's *assertion* —
   only the direction-to-input mapping feeding it should change.
4. Rewrite `camera_follow` (`src/main.c:2788`) to ease toward the player position (exponential or
   linear-with-cap smoothing) with a small deadzone before it starts moving at all, keeping the
   existing edge-clamping logic (`max_x`/`max_y`) as the outer bound on top of the eased position.
5. Re-run the full suite, not just the tests this phase touches — a simulation change is exactly the
   kind of thing that can have non-obvious effects on reachability or gating.

## Verification gate

`--move-test`, `--input-test`, `--play-test --seeds 50` (must stay 50/50), plus the full remaining
suite (`--village-test`, `--region-test`, `--reach-test`, `--gating-test`, `--iso-test`, `--rng-test`)
re-run and green, since this touches the simulation core that every other invariant assumes is
stable. Camera easing verified by eye (screenshot a movement sequence, or watch `--autoplay`) plus a
perf check via `--frames 400 --perf` showing no meaningful regression.

## Traps specific to this phase

- **This is the one phase in the current plan explicitly called out by [[Agent Prompt]]'s rule on
  architectural changes needing to be flagged before proceeding.** Input orientation was deferred
  this long specifically to keep it isolated — don't fold it into a "quick fix" alongside something
  else.
- **The autopilot's steering logic doesn't just consume `Input` — it *chooses* directions based on
  target deltas** (`ddx`/`ddy` comparisons in `autoplay_selftest` and `autopilot_tick`). Rotating
  input orientation means either rotating the autopilot's own target-delta-to-input logic to match,
  or the autopilot silently starts walking toward the wrong screen direction while still reaching
  its target by accident (because it's still choosing world-space deltas correctly) — which would
  make `--play-test` pass while the actual on-screen behavior driving it is now wrong. Check this
  specifically, not just whether the test passes.
- **Camera easing interacts with `render`'s band-sweep culling range** (`b0`/`b1` computed from
  `g->cam_y`, `src/main.c:2496`). An eased camera can lag the player by enough pixels that the
  existing cull margins need re-checking, especially the "clamp the player into the drawn range" a
  few lines below — that logic assumed a snapped camera.

## Evidence

Not yet started.
