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

> **Implementation note, 2026-08-02 — an interpretation worth confirming.** Taken literally, the
> world stays fully fogged until the first fragment is restored, including the fragment you must
> find first. So the blend takes the stronger of two contributions: **sight** (walking somewhere
> reveals its shape, capped at 0.42) and **restoration** (a restored region goes to full colour,
> permanently). Restoration is still the only thing that brings colour back — sight only shows
> terrain shape. Built and confirmed visually in [[2026-08-02-session-03]]; flagged here because
> the note does not specify it.
- Aggregate across all regions maps onto the 4-stage world-growth read (Unexplored / Partly Revealed / Many Memories Restored / Fully Restored) — see [[World Generation]].

No SDL_image. No PNGs, no sprite sheets — see [[Agent Prompt]] for the excluded-libraries rule. This is the rendering technique that makes the "world remembers itself" hook visible without costing asset bytes — it's the visual half of the audio/visual coupling described in [[Audio and Synth]].

## Note on the mockup's visual style
The mockup's painted, atmospheric before/after art is pitch art, not the in-engine target — see [[Overview]]'s art-style note. What ships is flat-shaded procedural geometry with this same fog-to-color logic, not a painted scene.

> **Updated 2026-08-04 — the geometry changed, this system did not.** The renderer is now
> isometric 2.5D (see [[Isometric Rendering]]), so "flat-shaded procedural geometry" above is out
> of date as a description of the *look*. It remains exactly right about the *method*: still
> procedural, still no sprites, still no asset files.
>
> **`fog_lerp` survived the rewrite unchanged and is still the single path from true colour to
> screen colour.** Every new surface routes through it — tile top faces, both cliff side faces,
> every tree lobe, every roof ring, every window. That was a deliberate constraint on the
> isometric work, because [[Cut List]] lists the fog-reveal core feel as never-cut.
>
> One thing to watch that did not exist before: a tree canopy is four shades and a cliff has two
> face shades, so the blend now has to preserve *relative* luminance or detail collapses to a
> grey blob in fog. It does — `fog_lerp` scales luminance rather than replacing it — but this has
> only been checked by eye at a few reveal levels, not measured.
