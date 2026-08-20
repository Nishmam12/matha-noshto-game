---
tags: [design, phase, wayfarer]
phase: 2
status: done
updated: 2026-08-04
---

# Phase 02 — Roof And Fog

**Status:** DONE — committed as `e5c8942`, "Roof face shading, and rewrite the fog so distance reads
as haze".
**Depends on:** [[Phase 01 - Landform]] (fixing roof/fog shading on top of cave-noise terrain would
have been wasted effort — the ground had to be coherent first).
**Blocks:** [[Phase 03 - Legibility Tools]] (this phase is the direct cause of Phase 03 existing —
see the Traps section), [[Phase 05 - Verification Debt]] (owes `--fog-test`).

## Why this phase

Two specific, previously-diagnosed defects, both left as known issues at the end of Phase 01:

1. **Buildings read as flat plates.** The roof is a stack of concentric `iso_diamond`s — one flat
   colour per slice. However carefully the per-step colours were chosen, nothing in that *shape*
   distinguishes a left-facing slope from a right-facing one, so it reads as a plate no matter what.
2. **Traversal felt suffocating, and it was never the camera.** `fog_lerp` scaled a tile's luminance
   to 0.55 and pulled it toward `(44, 52, 68)` — a *dark* blue-grey. Since walking only ever reveals
   terrain to `SIGHT_MAX` (then 0.42), roughly 95% of any given screen was that one dead colour. The
   world read as a cave regardless of what was actually built on it.

## Definition of done

- [x] Roofs have a visible left/right face distinction, the same way terrain tiles do via
      `FACE_L`/`FACE_R`.
- [x] Unrevealed land reads as **distance** (lighter, cooler, lower-contrast) rather than as
      **absence** (dark, flat, cave-like).
- [x] The fog blend still routes every surface through a single function (`fog_lerp`) — the
      contract that [[Fog and Reveal]] depends on is not broken, only its destination colour is.
- [x] A four-shade canopy or a two-face cliff does not collapse into a single flat blob at low
      reveal — relative luminance ordering survives the blend.
- [x] Full suite still green, playthroughs still 50/50, render cost does not regress.

## Concrete tasks (as executed)

1. **Added `iso_diamond_lr`** (`src/main.c:1989`) — the same column geometry as `iso_diamond`, split
   down its vertical centre line into a left colour and a right colour. Used for every roof slice
   and the ridge cap in `draw_building` (`src/main.c:2304`), each computed at a new `ROOF_L` percent
   for the down-left slope and full colour for the down-right, mirroring `FACE_L`/`FACE_R`.
2. **First attempt at the roof fix was an eave-shadow diamond, not the face split** — tried, made
   the roof look *worse* (a light rim around a flat plate, because the roof above it was still
   concentric rings), and was reverted. A comment documenting the failed attempt was left in
   `draw_building` specifically so it isn't retried by a future session working from the same
   symptom.
3. **Rewrote `fog_lerp`** (`src/main.c:1868`): replaced the fixed 0.55 luminance scale + 0.45
   tint-pull with a `FOG_KEEP` fraction (new constant) of the source's own luminance contrast
   preserved against a new, lighter `FOG_TINT_R/G/B`. Raised `SIGHT_MAX` from 0.42 to 0.50 alongside
   it so walked ground keeps more of its colour.
4. **Tuned the constants — three passes, not one**, all before landing on the committed values:
   - Pass 1: `(86, 100, 122)` at `FOG_KEEP 0.42` — overcorrected into a washed-out, uniformly light
     grey. The cave read was gone but so was any sense of depth.
   - Pass 2: settled toward `(60, 70, 86)` / `0.50`, but the stone ramp — lightened in Phase 01 —
     turned out to now be the **brightest** large surface in the world under the new fog, so rock
     outcrops read as glowing white shapes floating in a dark field and pulled the eye off the
     player.
   - Pass 3: pulled stone back down to just above grass in value, matching the wall-lightest /
     foliage-darkest hierarchy the Art Bible specifies. This is the version that shipped.

## Verification gate

Full suite re-run after both the roof and fog changes: `--village-test` (30 seeds), `--region-test`
(30), `--reach-test` (50, negative control, gating relaxed 0/50), `--gating-test` (30), `--play-test`
(50, **50/50**), `--move-test` (20), `--iso-test`, `--rng-test` — all PASS, zero warnings. Perf
re-measured: render 0.844 ms mean, versus 0.859 ms measured before this phase's `fill_ellipse`
addition in Phase 01 — no regression, arguably an improvement.

**Owed and not yet paid:** `--fog-test` does not exist. The claim that `FOG_KEEP` preserves shade
separability across the reveal range is arithmetic-on-paper plus eyeballing screenshots, not a
measured invariant with a negative control. See [[Phase 05 - Verification Debt]].

## Traps specific to this phase

- **A stack of concentric diamonds has no volume, however its steps are shaded.** This presents as
  a colour problem (the steps look flat) but is a *geometry* problem (no shape-level left/right
  distinction exists). Don't iterate on per-step colour trying to fix it — add the face split.
- **This phase is the direct argument for [[Phase 03 - Legibility Tools]].** Three rebuild-and-
  screenshot passes to settle two colour constants is not a scalable process, and it cost real
  session time for zero byte cost. Any future colour/palette work should happen with the live
  tuning overlay, not by repeating this loop.
- **A palette change in one phase can silently invalidate an assumption from an earlier phase.**
  Stone was lightened in Phase 01 against the *old* fog; it became wrong the moment the *new* fog
  landed in this phase, and nothing caught that automatically — it was caught by looking again.
  Colour decisions are relational (this surface relative to that one), not absolute, and should be
  re-checked whenever anything else in the same value range changes.

## Evidence

- `690,688 bytes` after this phase (was `690,176` before), **+512 bytes**.
- Full test output, the three-pass tuning narrative, and screenshots are logged in
  `devlog/2026-08-04-session-01.md` under "Session 03 — roof volume, and the fog rewrite."
- This session also recorded the **first human playtest of the game**: *"even though the gameplay
  is in early stage, it did feel slightly enjoyable."* Not this phase's verification gate, but it
  landed in the same session and retires a risk four consecutive handovers had carried.
