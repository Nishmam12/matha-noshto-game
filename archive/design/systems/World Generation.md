---
tags: [system, wayfarer]
---

# World Generation

Part of [[Overview]]. Feeds [[Fog and Reveal]], [[Abilities]], [[Fragments]].

## Representation
- Single seed generates the full landmass at startup (or in chunks if generation time becomes an issue — measure before optimizing).
- World is a **region graph**: each region has a terrain type, an ability requirement to enter, a restoration state, and a list of [[Fragments|fragment]]/[[Found Souls]] IDs it contains.

## Reachability invariant
At every ability tier the player currently has, at least one un-restored fragment must be reachable. Enforce in the generator, not just hope for it: generate-then-verify — run a graph reachability check after placement, regenerate region connections if it fails.

## World growth stages (confirmed 4-stage read)
An aggregate readout over all regions' restoration%, used for the minimap in [[Save and UI]] and any debug overlay: Unexplored → Partly Revealed → Many Memories Restored → Fully Restored. Purely a display bucketing of the same underlying float — see [[Overview]].

## Seed display format
Mockup showed a short shareable code (e.g. `8124_07A3`). Nice-to-have, not core: if adopted, format the numeric seed as a short alphanumeric string for save-file display / sharing. Doesn't affect the underlying PRNG seed value.

## PRNG
Custom PRNG (xorshift or PCG32), not libc `rand()`. Independent seeded streams for terrain / entities / audio so tuning one doesn't reshuffle another — see [[Audio and Synth]] for the audio stream's use.

## QA
Batch-test ≥20 seeds after any change to generation or placement logic — see [[QA Checklist]]. Build a debug overlay early: regenerate + grid-view multiple seeds at once.

## Open questions
See [[Open Decisions]] — world size (region count) is unresolved.
