---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules. Picking this up cold? Start with [[Handover]].

**Current `.exe` size:** 679,424 bytes
**Current status:** verified — Weeks 1–3 complete, core loop closes; Week 4 (audio) is next
**Headroom:** 760,576 bytes under the 1,440,000 ship target

## Sessions

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
| 08-02 | **679,424** | +1,024 | ability gating, restoration loop, Found Soul states, confirm beat |

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
