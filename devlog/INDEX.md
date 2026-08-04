---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules. Picking this up cold? Start with [[Handover]].

**Current `.exe` size:** 684,544 bytes
**Current status:** verified — isometric renderer in place; village generation and audio ahead
**Headroom:** 755,456 bytes under the 1,440,000 ship target

## Sessions

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
| 08-04 | **684,544** | +1,536 | layered trees and bushes, two-sub-pass depth sort |

Self-test builds (`wayfarer-selftest.exe`) are not deliverables and are deliberately excluded
from this table and from the budget gate.

## Standing risks

- **Nobody has played it by hand.** Every playthrough so far was driven by the autopilot. The
  interact affordance, reach radius, movement speed and confirm beat have never been judged by a
  human, and none of that can be verified from here.
- **Pacing may be short.** A shortest-path full clear is 30–82 seconds of walking. Real play with
  fog will be longer by an unknown multiplier. Feeds the region-count question in
  [[Open Decisions]].
- Audio has never been heard, only measured.
- Nothing has been run on a machine other than the development box — [[QA Checklist]]'s
  "runs clean without dev tools" is still unchecked.
- The gating-relaxation fallback in world generation has never fired (0 of 50 seeds), so that
  code path is untested against real failure.
- Physical keyboard input verified via posted window messages, not a real key press.
- **The world is still the cave generator, dressed.** The village layout the isometric look was
  chosen for does not exist yet, and it is the one remaining slice that can invalidate the
  reachability guarantee rather than merely need re-running against it.
- Render cost is measured on one machine with the window unoccluded. `present` is an OS blit whose
  cost depends on the compositor and window state.
