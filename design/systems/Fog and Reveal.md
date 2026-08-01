---
tags: [system, wayfarer]
---

# Fog and Reveal

Part of [[Overview]]. The core visual hook. Depends on [[World Generation]].

## Approach
Direct pixel-buffer rasterization via `SDL_LockTexture`, **or** procedurally generated textures baked once at init into `SDL_Texture`s. Pick one path — don't mix.

> **Settled by the build, 2026-08-02.** Neither option survives as written: SDL's entire render
> subsystem is compiled out of our SDL2 build (it cost more than the rest of SDL combined — see
> [[Toolchain Setup]]), so `SDL_Texture` and `SDL_LockTexture` do not exist for us. Rendering is
> direct pixel writes into the window surface from `SDL_GetWindowSurface`. This is the
> pixel-buffer path in spirit, and it is the only one available. Implemented in
> [[2026-08-02-session-01]].

Fog-to-color is a blend effect over generated geometry, not a sprite swap:
- Track per-region "restoration %" as a float.
- Render color = `lerp(grey, true color, restoration%)`.
- Aggregate across all regions maps onto the 4-stage world-growth read (Unexplored / Partly Revealed / Many Memories Restored / Fully Restored) — see [[World Generation]].

No SDL_image. No PNGs, no sprite sheets — see [[Agent Prompt]] for the excluded-libraries rule. This is the rendering technique that makes the "world remembers itself" hook visible without costing asset bytes — it's the visual half of the audio/visual coupling described in [[Audio and Synth]].

## Note on the mockup's visual style
The mockup's painted, atmospheric before/after art is pitch art, not the in-engine target — see [[Overview]]'s art-style note. What ships is flat-shaded procedural geometry with this same fog-to-color logic, not a painted scene.
