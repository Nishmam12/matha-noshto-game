---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules.

**Current `.exe` size:** 670,208 bytes
**Current status:** verified — window, framebuffer and audio pipelines all proven; no game systems yet
**Headroom:** 769,792 bytes under the 1,440,000 ship target

## Sessions

- [[2026-08-01-session-01]]
  - **Session 01** — pipeline proof: SDL2 window + software framebuffer, static stripped build.
    Stock `libSDL2.a` blew the hard limit at 1,714,176 bytes; a cut-down SDL2 build brought it
    to 669,696. See [[Toolchain Setup]].
  - **Session 02** — audio pipeline: procedural synthesis in the callback, seeded RNG with
    verified replay. Callback uses 0.4% of its deadline. Found and fixed a launch-blocking bug
    on machines with no audio device.

## Size history

| Session | Bytes | Delta | Note |
|---|---:|---:|---|
| 01 | 1,714,176 | — | first build, stock prebuilt `libSDL2.a` — over the hard limit |
| 01 | 669,696 | −1,044,480 | after rebuilding SDL2 with unused subsystems cut |
| 02 | **670,208** | +512 | procedural audio + seeded RNG |

Self-test builds (`wayfarer-selftest.exe`) are not deliverables and are deliberately excluded
from this table and from the budget gate.

## Standing risks

- Audio has never been heard, only measured. See "NOT verified" in
  [[2026-08-01-session-01]].
- Nothing has been run on a machine other than the development box.
- No frame pacing yet — the loop uses `SDL_Delay(16)` and there is no vsync.
