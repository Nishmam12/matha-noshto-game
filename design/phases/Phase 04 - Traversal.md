---
tags: [design, phase, wayfarer]
phase: 4
status: done
updated: 2026-08-05
---

# Phase 04 — Traversal

**Status:** DONE — `dd8cfef`, +512 bytes. One item left to a human: whether the camera easing
*feels* right (see Evidence).
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

- [x] Pressing `W` moves the player up on screen; `D` moves right; diagonals compose correctly.
      Asserted per direction by `speed_selftest`, all 8 "screen dir correct".
- [x] Collision behaviour (swept AABB, wall sliding via independent-axis resolution) is unchanged —
      `move_axis` was not touched at all; only the input→velocity mapping rotated.
- [x] The direction-independent-speed invariant still holds and is still tested — **strengthened**
      from 2 directions to all 8, at 165.00 px/60 ticks each (220 scaled by the TILE 32→24 rescale).
      The old assertion measured world-x displacement and could not survive a basis change; the new
      one measures travel *distance* and is basis-independent.
- [x] The autopilot steering in `--play-test`/`--autoplay` is updated to the new mapping, not
      deleted or skipped — and doing it naively livelocked, see Evidence.
- [x] `--play-test --seeds 50` is still 50/50 after the change.
- [x] The camera has a deadzone plus an exponential ease and no longer re-centres every frame.
- [ ] **Whether the easing feels right has not been judged by a human.** The constants are first
      guesses. Same shape of gate as Phase 03's overlay.

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

Built 2026-08-05, session 02 — `dd8cfef`. Full write-up in [[2026-08-05-session-01]] §Session 02.

**Size: 690,688 bytes, +512.** Render 0.97 ms mean (was 0.824 ms), measured over three runs so it
is real rather than noise — but pixel and call counts are *identical*, so it is code layout, not
added work. 5.9% of the 16.67 ms budget.

**The rotation.** Screen-right is world (+1,−1), screen-down is world (+1,+1), so
`wx = sx + sy`, `wy = sy − sx`, normalised by the vector's actual length (√2 for a screen axis, 2
for a screen diagonal). The old code special-cased 0.7071 on diagonals only, which is why the new
version is direction-independent across all 8 directions rather than the 2 it happened to cover.

### The livelock — the thing this phase actually cost

The phase file's trap section predicted the autopilot would need rotating too. It did. What it did
**not** predict is that rotating it the obvious way deadlocks the playthrough:

> Rotating the raw world deltas and thresholding afterwards means `ddx=+0.5, ddy=−0.5` — inside the
> rest zone on *both* world axes — rotates to `sdx=1.0`, which clears the 0.6 threshold. The
> autopilot twitches where it used to sit still, overshoots by a full 2.75 px step, and oscillates
> between two tiles forever.

3 of 3 seeds hit the 200,000-step cap having restored 0–4 of 19. **Fixed by applying the deadband
first, in world space, and rotating only the resulting −1/0/+1 intent** — the rest condition is then
identical to before the phase. Steps per seed: 200,000 (capped) → 2,676–3,441.

Worth generalising: **when you rotate a control signal, rotate the decision, not the measurement.**
A threshold is a judgement about the space the target lives in.

### Verification

`speed_selftest` rewritten and given a negative control it did not previously have:

```
right/left/down/up/4 diagonals : 165.00 px in 60 ticks each, screen dir correct
direction-independent speed, all 8 directions: yes
negative control (world-aligned input rejected): PASS  [4 of 4 caught]
```

The negative control replays the *old* world-aligned mapping and requires the direction check to
reject it. Without it, "screen dir correct" would prove only that the checker agrees with the code.

Full suite re-run because this is a simulation change: play 50/50, reach 50 + control, gating 30,
region 30, village 30, move 20, iso, rng, font — all PASS.

### Deviations and gaps, stated plainly

- **Task 1 asked for input and camera as separate commits; they landed as one** (`dd8cfef`). The
  livelock diagnosis needed both in the tree, and re-verifying separately costs two more full
  50-seed playthrough runs against the 2026-08-14 hard stop. Recorded rather than left implicit.
- **The camera's feel is unjudged.** `CAM_DEADZONE` / `CAM_EASE` are first guesses.
- **The ease runs per frame, not per tick.** With the frame rate capped at `FRAME_HZ` the time
  constant is stable in practice, but it would drift on a machine that cannot hold the cap. Known
  simplification, not an oversight.
