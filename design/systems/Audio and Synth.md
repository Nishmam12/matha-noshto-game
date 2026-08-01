---
tags: [system, wayfarer]
---

# Audio and Synth

Part of [[Overview]]. Triggered by restoration events in [[Fragments]] and [[Found Souls]]. Uses a PRNG stream from [[World Generation]].

## Softsynth
Oscillator (saw/square/sine/noise) → ADSR envelope → simple filter → optional short delay.

## Music
Stored as compact pattern data (note/instrument/effect per row), not sampled audio.

## Named layers (confirmed — resolves the earlier "cap the voice count" open question to exactly 5)
- **Base** — always-on foundation, present from the start (sparse, minimal)
- **Strings** — activated by place/region fragment restorations
- **Pad** — activated by place/region fragment restorations
- **Bells** — activated by place/region fragment restorations
- **Voice of Souls** — activated specifically by [[Found Souls]] reaching Remembered; distinct timbre from the place-layers above

This is the audio half of the audio/visual coupling that's the strongest, hardest-to-fake idea in the whole design — see [[Fog and Reveal]] for the visual half. Ambient track starts as just Base; each restoration activates the next appropriate layer, capped at these 5.

## SFX
Parametric (sfxr-style, ~20 floats per sound), synthesized at trigger time or cached at load. Covers: restoration chime, footsteps, ability-use cue.

## Real-time constraint
The audio callback runs on a real-time thread with a hard deadline — no allocation, no locks in the callback. Render into a lock-free ring buffer from the main/game thread. Profile this early; see [[QA Checklist]] and [[Agent Prompt]]'s "release blocker" rule for the audio callback.

## Excluded
No SDL_mixer, no sampled/compressed audio files (no Ogg/MP3) — see [[Agent Prompt]] for the full excluded-libraries list.
