---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules. Picking this up cold? Start with [[Handover]].

**Current `.exe` size:** 692,224 bytes
**Current status:** verified — island landform, clustered villages, a bitmap font and F3 fog-tuning
overlay (self-test-only, +0 shipping bytes), the whole world **rescaled to 24 px tiles on a 108×60
grid** with all art routed through `PX()`, **screen-aligned input** (`W` finally moves up) and an
eased follow camera. No animation, no character, no audio.
**Forward plan:** see [[Phase Roadmap]] — **Phases 00–07 all done**, and 07 absorbed most of 09.
Next: the player-occlusion decision, then Phase 08 (save/load)
**Headroom:** 747,776 bytes under the 1,440,000 ship target

## Sessions

- [[2026-08-05-session-01]] *(Session 05)* — **The waterfalls, and bridges stop being an argument.**
  [[Phase 06 - Water And Bridges]] closed at **+0 bytes**. `--bridge-test` regenerates each seed
  twice, once with decking suppressed, and compares the player's spawn component: **200/200
  bridge-bearing seeds shrank**, so "bridges are load-bearing" is measured rather than argued —
  retiring the item four handovers carried as the highest-value test left. Waterfalls came from
  carrying `place_rivers`' BFS field out to a render-only `sea_dist` and quantising it, so
  `iso_tile` draws each drop with no new routine. Two faults found by **screenshot, not test**
  (`height` is render-only, so no checker can see either): the descent had to be **depth, not
  elevation** — terracing upward clamped at ground level and turned the channel into a blue path on
  the grass — and a bridge deck, still `SURF_RIVER`, drew at the bottom of the channel as a walkable
  pit until it got its own height branch. Also: **probe the data for existence, screenshot for
  judgement**; a wrong `RIVER_FALL_EVERY` was invisible by eye and instant in the numbers.
  Re-run not re-argued: play 50/50, reach 50/50, land 30/30, gating 30/30.

- [[2026-08-05-session-01]] *(Session 04)* — **Rivers reach the sea, and the chat handed over.**
  [[Phase 06 - Water And Bridges]] part 1: rivers descend a BFS **distance-to-sea** field rather
  than the height field, which designs out the local-minima trap the phase file flagged — a BFS
  field has none, so the walk provably terminates at water. Bridges **clear `solid`** rather than
  special-casing `tile_blocked`, so collision, the region graph, the verifier and the autopilot all
  see a crossable tile through the path they already used; the phase adds **zero** new collision
  inputs. Completability re-run not re-argued: play 50/50, reach 50/50. Also answered the team's
  asset question (PNGs are bake *inputs*, never shipped) and flagged that "lacks the pixelated
  feel" is a resolution/palette question, not an asset one. **Left half-done on purpose**:
  waterfalls and the bridge-suppression negative control are both still owed.

- [[2026-08-05-session-01]] *(Session 03)* — **The houses were a mess, and the rule debt is paid.**
  Three defects in `draw_building`, all present since buildings landed: the roof was drawn half a
  tile below its own walls (`iso_tile` takes `ax` as the diamond's **centre**, so `world_to_iso`
  already agrees with it and the roof's extra `ISO_HH` was pure error); the facade was positioned
  relative to that same broken reference, so fixing the roof put the windows on it until they were
  re-derived from the wall-top diamond; and walls were half the height their footprints needed.
  Then [[Phase 05 - Verification Debt]]: `--land-test` and `--fog-test`, each with a working
  negative control, at **+0 shipping bytes**. `--land-test`'s obvious assertion failed 3 of 100
  seeds — and looking at those seeds showed the *assertion* was wrong, not the generator: a
  detached islet across unwalkable ocean is scenery, so the check was re-aimed at the component the
  player spawns in. Suite green, play 50/50.

- [[2026-08-05-session-01]] *(Session 02)* — **The world was too big, and `W` finally points up.**
  Diagnosed "everything seems too big" to its actual cause: every prop was authored in absolute
  pixels against `TILE == 32`, so `TILE` scaled the ground but not the art. New `PX()` makes `TILE`
  one honest knob for the whole visual scale; `TILE` 32→24 and the world 80×45→108×60 so the island
  keeps its extent and gains resolution (safe because `land_noise` is normalised). **−512 bytes, and
  render unchanged despite 37% more draw calls.** Then [[Phase 04 - Traversal]]: input rotated
  through the inverse projection basis, `move_axis` untouched. **Rotating the autopilot's deadband
  naively livelocked every playthrough** — 3 of 3 seeds capped at 200,000 steps, presenting as a
  hang rather than a failure; fixed by thresholding in world space and rotating only the discrete
  intent. `speed_selftest` rewritten to assert a basis-*independent* invariant (travel distance, all
  8 directions) and given the negative control it never had. Camera got a deadzone + exponential
  ease. Suite green throughout, play 50/50.

- [[2026-08-05-session-01]] — **[[Phase 03 - Legibility Tools]]: a bitmap font, and the live tuning
  overlay built on it.** 5×7 bit-packed glyphs (`0x20`–`0x5F`), `draw_text` / `draw_text_shadow`, and
  an F3 overlay (`TAB` row, `-`/`=` adjust) that tunes `FOG_TINT_*` and `FOG_KEEP` live — replacing
  the three-pass edit-rebuild-screenshot loop that settled those same constants last session. Overlay
  scope was **narrowed to render-only constants by an explicit decision**; the generation constants
  would need a full `game_init` per keypress and are excluded on the record. New `--font-test` with an
  off-by-one-stride negative control, its reference count derived from the glyph table so it tracks
  edits automatically. **Shipping delta +0 bytes**, re-confirmed after `fog_lerp` was rewritten. Whole
  suite re-run, all PASS. Two glyphs (`=`, `>`) were missing while every test stayed green — found by
  screenshot, which is the same lesson as the lollipop trees. Phase is code-complete, **not closed**:
  its gate requires a human to actually use the overlay once.

- [[2026-08-04-session-01]] *(Session 04)* — **Handover rewrite, and a phase-by-phase roadmap.**
  Documentation only, ahead of a fresh chat. [[Handover]] rewritten against a fresh build and a
  fresh full test run (nothing carried forward unverified). New `design/phases/` folder: an index
  ([[Phase Roadmap]]) plus 12 phase files, Phases 00–02 marked done with commit/byte evidence,
  Phases 03–11 planned with a definition of done, concrete file/line-referenced tasks, a
  verification gate, and phase-specific traps each. No code changed.

- [[2026-08-04-session-01]] *(Session 03)* — **Roof volume, and the fog rewrite.** `iso_diamond_lr`
  gives roofs the same left/right face split terrain gets from `FACE_L`/`FACE_R`, fixing the flat-plate
  read diagnosed last session. `fog_lerp` rewritten: unrevealed land now resolves toward a light cool
  haze keeping `FOG_KEEP` of its own luminance contrast, instead of a dark grey that crushed 95% of
  the screen to one dead colour — the actual cause of "traversal feels suffocating". Took three
  passes; the first two overcorrected, and stone had to be pulled back down after it became the
  brightest surface in the world. **First human playtest ever.** Render 0.844 ms, suite PASS,
  play 50/50. Still owed: `--fog-test`, `--land-test`, screen-aligned input, camera easing.

- [[2026-08-04-session-01]] *(Session 02)* — **The landform, and a first honest look at it.**
  Replaced the cave generator with an island height field: coastline, ocean, inland rock outcrops,
  villages clustered onto sites instead of scattered over every open plot. Found three defects by
  screenshotting rather than testing — flat-slab outcrops, a checkerboard height jitter, and canopy
  lobes that were axis-aligned rectangles — and fixed all three, adding `fill_ellipse` and prop
  contact shadows. New provisional [[Art Bible]]. **No collision input changed**, so the 50-seed
  completability proof was re-run rather than re-argued; whole suite PASS. Left explicitly undone:
  `--land-test` and its negative control, the fog rewrite, screen-aligned input, and building roof
  face-shading (diagnosed, with a note in `draw_building` recording a failed attempt).
- [[2026-08-04-session-01]] — **Isometric pivot, part 1.** Render instrumentation first (the old
  "56 fps" measured `SDL_Delay`, not drawing). Then 32 px tiles, a 2:1 isometric projection with a
  provably gap-free column rasteriser, elevation with cliff faces, a 960×540 logical buffer
  integer-scaled into the window, per-tile surface detail, and layered procedural trees with a
  real two-sub-pass depth sort. New `--iso-test` with a negative control. No simulation code
  touched; all existing tests pass unchanged. Corrupted the source encoding with a PowerShell text
  round-trip and had to repair it.
- [[2026-08-02-session-03]] — **Week 3.** Ability gating in collision, the restoration loop,
  Found Soul states, win condition, restore confirm beat. 50/50 seeds played to completion by
  autopilot through real collision. Caught a counter-reset bug that would have carried progress
  across in-game regeneration.
- [[2026-08-02-session-02]] — **Week 2.** Region graph (farthest-point sampling + multi-source
  BFS), terrain/ability gating, the reachability invariant with a negative control, and the
  debug overlay + 12-seed grid view. Caught a `sizeof`-on-pointer bug that every *relative*
  structural test had passed.
- [[2026-08-02-session-01]] — **Week 1.** PCG32 with independent terrain/entity/audio streams;
  continuous movement + swept AABB tile collision on a fixed 60 Hz step; first-pass fog-to-color
  reveal. Fixed sealed spawn pockets, corner spawns, an uncapped render loop, and recovered
  39,424 bytes of zeros wrongly stored in `.data`.
- [[2026-08-01-session-01]]
  - **Session 01** — pipeline proof: SDL2 window + software framebuffer, static stripped build.
    Stock `libSDL2.a` blew the hard limit at 1,714,176 bytes; a cut-down SDL2 build brought it
    to 669,696. See [[Toolchain Setup]].
  - **Session 02** — audio pipeline: procedural synthesis in the callback, seeded RNG with
    verified replay. Callback uses 0.4% of its deadline. Found and fixed a launch-blocking bug
    on machines with no audio device.

## Size history

| Date | Bytes | Delta | Note |
|---|---:|---:|---|
| 08-01 | 1,714,176 | — | first build, stock prebuilt `libSDL2.a` — over the hard limit |
| 08-01 | 669,696 | −1,044,480 | after rebuilding SDL2 with unused subsystems cut |
| 08-01 | 670,208 | +512 | procedural audio + seeded RNG |
| 08-02 | 670,720 | +512 | PCG32 with independent streams |
| 08-02 | 712,704 | +41,984 | world + movement + fog — **40 KB of zeros in `.data`** |
| 08-02 | 673,280 | −39,424 | statics moved to stack; see [[Toolchain Setup]] |
| 08-02 | 674,304 | +1,024 | frame cap + centroid spawn |
| 08-02 | 678,400 | +4,096 | region graph, reachability invariant, debug overlay, grid view |
| 08-02 | 679,424 | +1,024 | ability gating, restoration loop, Found Soul states, confirm beat |
| 08-04 | 679,424 | +0 | render instrumentation — byte-identical, which is the proof it is absent |
| 08-04 | 680,448 | +1,024 | 32 px tiles, isometric projection, `vspan`/`iso_tile`, `--iso-test` |
| 08-04 | 680,960 | +512 | elevation: derived heights, cliff side faces, terraced rock |
| 08-04 | 682,496 | +1,536 | 960×540 logical backbuffer, integer upscale, F11 fullscreen |
| 08-04 | 683,008 | +512 | per-tile hash, ground grain, per-terrain surface marks |
| 08-04 | 684,544 | +1,536 | layered trees and bushes, two-sub-pass depth sort |
| 08-04 | 686,592 | +2,048 | rock/reed/flower/crystal/stump props, isometric interact ring |
| 08-04 | 689,152 | +2,560 | procedural buildings, mix-and-match house parts, `--village-test` |
| 08-04 | 690,176 | +1,024 | island landform, village clustering, `fill_ellipse`, palette pass |
| 08-04 | 690,688 | +512 | roof face split (`iso_diamond_lr`), fog rewrite |
| 08-05 | 690,688 | +0 | bitmap font, `--font-test`, F3 tuning overlay — all self-test-only |
| 08-05 | 690,176 | −512 | world rescale: `PX()`, `TILE` 32→24, grid 108×60 — *saved* bytes |
| 08-05 | 690,688 | +512 | screen-aligned input, eased camera, 8-direction speed test + control |
| 08-05 | 691,200 | +512 | houses: roof back on the box, windows back on the wall, taller storeys |
| 08-05 | 691,200 | +0 | `--land-test` and `--fog-test`, both with negative controls |
| 08-05 | **692,224** | +1,024 | rivers via BFS-to-sea, bridges that clear `solid`, river/plank colours |

Self-test builds (`wayfarer-selftest.exe`) are not deliverables and are deliberately excluded
from this table and from the budget gate.

## Standing risks

- **One playtest is not QA.** The game has been played by hand exactly once (2026-08-04, "slightly
  enjoyable"), by the person who wrote the design. The interact affordance, reach radius, movement
  speed and confirm beat have had one informal look and no more. [[QA Checklist]]'s "runs clean on a
  machine without dev tools" is a *different* item and is still unchecked.
- **Pacing may be short.** A shortest-path full clear is 30–82 seconds of walking. Real play with
  fog will be longer by an unknown multiplier. Feeds the region-count question in
  [[Open Decisions]].
- Audio has never been heard, only measured.
- Nothing has been run on a machine other than the development box — [[QA Checklist]]'s
  "runs clean without dev tools" is still unchecked.
- The gating-relaxation fallback in world generation has never fired (0 of 50 seeds), so that
  code path is untested against real failure.
- Physical keyboard input verified via posted window messages, not a real key press.
- **Nothing in the world moves.** No sway, shimmer, bob or smoke; the player is still a 24×24
  orange square with no facing or walk cycle. A static isometric scene reads as a diorama.
- **Buildings do not respond to restoration yet.** The ruin→whole rebuild is designed but not
  built, and it is what would make the game's own hook literally visible.
- **Pacing has not been re-measured** and is now further out of date: the tile size has changed
  twice (16→32→24) and the grid is 1.8× larger in tiles than anything measured. The 30–82 s figure
  predates all of it.
- **The new camera and the new art scale have never been judged by a human.** `CAM_DEADZONE`,
  `CAM_EASE` and `TILE` itself are all defensible numbers nobody has actually played with.
- **Rock outcrops read as pale scattered blocks under fog** at the new scale — stone is still the
  lightest large surface, so more, smaller outcrops pop out of the haze. A palette job for the F3
  overlay, not a renderer bug.
- Render cost is measured on one machine with the window unoccluded. `present` is an OS blit whose
  cost depends on the compositor and window state.
