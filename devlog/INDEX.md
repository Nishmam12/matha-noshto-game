---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules.

**Current `.exe` size:** 674,304 bytes
**Current status:** verified — Week 1 code complete; awaiting the milestone *feel* call in [[Overview]]
**Headroom:** 765,696 bytes under the 1,440,000 ship target

## Sessions

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
| 08-02 | **674,304** | +1,024 | frame cap + centroid spawn |

Self-test builds (`wayfarer-selftest.exe`) are not deliverables and are deliberately excluded
from this table and from the budget gate.

## Standing risks

- **Week 2 is blocked on a human judgement**, not on engineering: [[Overview]]'s Week 1 milestone
  asks whether the reveal *feels* good, which cannot be verified from screenshots.
- Audio has never been heard, only measured.
- Nothing has been run on a machine other than the development box — [[QA Checklist]]'s
  "runs clean without dev tools" is still unchecked.
- Reachability is not yet guaranteed; the flood fill only guarantees a large spawn region.
- Physical keyboard input verified via posted window messages, not a real key press.
