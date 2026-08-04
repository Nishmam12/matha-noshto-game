---
tags: [design, phase, wayfarer]
phase: 3
status: next
updated: 2026-08-04
---

# Phase 03 — Legibility Tools

**Status:** NEXT.
**Depends on:** [[Phase 02 - Roof And Fog]] (this phase exists *because* of how Phase 02 went).
**Blocks:** [[Phase 07 - Asset Seam]] (baked sprites need to be judged against something better than
a rebuild cycle), [[Phase 09 - Placeholder Art]] (all remaining palette/shading decisions), and
indirectly every later phase that involves a colour or shading judgement call.

## Why this phase

Phase 02's fog rewrite took three rebuild-and-screenshot passes to settle two constants, and one of
those passes was wrong *because of* a value chosen in Phase 01 under a different fog model — a
relationship nothing caught automatically. This is not a one-off; every remaining phase involves
colour and shading decisions (water, paths, the player character, the restoration rebuild, motion
highlights). Continuing to tune by editing a `#define`, rebuilding, taking a screenshot, and looking
does not scale, and it is expensive in session time even though it's free in bytes.

There is also a second, independent problem this phase solves: **nothing can print text on screen.**
No bitmap font exists. That blocks Found Soul restoration lines, a minimal HUD, and — the reason it's
bundled into *this* phase rather than deferred to Phase 11 — the tuning overlay itself, which needs
to display constant names and values somehow.

## Definition of done

- [ ] A bitmap font renders readable ASCII text into the framebuffer, with a drop-shadow variant
      legible over arbitrary terrain.
- [ ] A live tuning overlay, built on the font and gated entirely behind `WAYFARER_SELFTEST`, lets a
      person adjust palette ramps, fog constants (`FOG_TINT_*`, `FOG_KEEP`, `SIGHT_MAX`), and
      density constants (`VILLAGE_*`, `LAND_ROCK_T`) at runtime and see the change immediately.
- [ ] The overlay compiles to **zero bytes** in the shipping (`wayfarer.exe`, non-selftest) build —
      verified by a build showing +0 byte delta when the feature is added, the same technique
      already used to prove the perf counters cost nothing in the shipping binary.
- [ ] `--font-test` exists: renders the full supported charset to a BMP for inspection, with a
      negative control (an off-by-one glyph stride must be rejected, not silently misrendered).

## Concrete tasks

1. Design a compact glyph set — 5×7 or 6×8 px, uppercase + digits + basic punctuation is likely
   sufficient for restoration lines and a tuning HUD; confirm against what [[Found Souls]] actually
   needs to display before over-building the charset.
2. Encode glyphs as a bit-packed static array (not a sprite — this is exactly the kind of thing
   that should stay hand-rolled procedural data, consistent with decision 3 in [[Handover]] §6: no
   `SDL_ttf`, ever).
3. Write `draw_text(fb, x, y, str, colour)` plus a shadowed variant (`draw_text_shadow` or a flag) —
   place near the other graphics primitives, `src/main.c` around the `fill_rect`/`vspan`/`iso_tile`
   cluster.
4. Write `--font-test`: render the full charset in a grid to a BMP via the existing `--shot`
   mechanism, and add a negative control that corrupts the glyph stride and confirms the renderer
   either rejects it or the test catches the visual corruption programmatically (pixel-count based,
   not eyeballed — consistent with [[Handover]] §7's "relative assertions are not correctness" trap).
5. Build the tuning overlay as a new debug key (candidate: `F3`, since F1/F2/F11 are taken) behind
   `WAYFARER_SELFTEST`, listing the constants named in the Definition of Done with +/- adjustment and
   immediate re-render. Consider whether constants need to become runtime variables (a struct) rather
   than compile-time `#define`s for the ones the overlay controls — this is a real architectural
   question specific to this phase and should be decided explicitly, not by accident.
6. Confirm the shipping build delta is +0 bytes with the overlay code added but compiled out.

## Verification gate

`--font-test` PASS including its negative control. A build showing the shipping `wayfarer.exe` delta
is +0 bytes with this phase's code present. Manual confirmation (screenshot) that the overlay is
usable — this is explicitly a tool for humans, so "does a person find it usable" is part of its own
definition of done, checked by having someone actually use it once before calling the phase closed.

## Traps specific to this phase

- **Turning tuning constants from `#define` into runtime-adjustable struct fields changes what
  "recompute once" means.** Several derived values in this codebase (e.g., anything computed once in
  `world_heights` or `game_init`) currently assume their inputs are compile-time constants. If a
  constant the overlay controls feeds into world generation rather than just rendering, changing it
  at runtime needs to trigger regeneration (the existing `R` key path), not just a redraw — get this
  wrong and the overlay will silently show stale terrain next to live colour changes.
- **`WAYFARER_PERF` defaults to `WAYFARER_SELFTEST`** ([[Handover]] §6 decision 8) specifically so
  debug code structurally cannot leak into the shipping build. Follow the same pattern for the
  overlay rather than inventing a new guard — consistency here is what makes the +0-byte proof
  trustworthy without re-deriving it each time.
- **A font is exactly the kind of feature that invites "just get something on screen" corner-cutting**
  on legibility (contrast against terrain, readable at 1x before assuming 2x/3x scale rescues it).
  Screenshot it at `--scale 1` specifically, since that's the worst case.

## Evidence

Not yet started.
