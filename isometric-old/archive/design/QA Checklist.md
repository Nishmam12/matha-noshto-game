---
tags: [design, qa, wayfarer]
---

# QA Checklist

Apply continuously, not just at the end. See [[Agent Prompt]] for the engineering loop this supports.

- [x] Every generation run is seed-reproducible and loggable — see [[World Generation]] (continuous practice; `--rng-test`, `--land-test`, `--save-test` all green)
- [x] Batch-test ≥20 seeds after any change to generation or placement logic (every phase since 02; last full run 2026-08-06, 20+ seeds per suite member, all green)
- [x] No unreachable fragments/Found Souls at any ability tier, in any tested seed — see [[World Generation]], [[Abilities]] (`--reach-test`, `--play-test`, `--gating-test` green)
- [x] Audio callback never allocates, never locks, profiled under full 5-layer load — see [[Audio and Synth]] (Phase 11: `--audio-test --layers --sfx`, worst 0.325 ms of a 21.333 ms deadline, NaN 0, clipped 0, partial writes 0)
- [x] `.exe` size measured and logged after every build; flag at 1.2MB soft threshold (786,432 bytes as of Phase 11; 653,568 headroom under the 1,440,000 ship target)
- [x] No SDL_image / SDL_ttf / SDL_mixer linked in final build — see [[Agent Prompt]] (Phase 11: `nm` on the final exe — 1 symbol, none of the three, no `SDL_LoadBMP`)
- [x] No shipped asset files of any kind in the final package (Phase 11: `build/` holds only `wayfarer.exe`, `wayfarer-selftest.exe`, `.last_size` and a runtime save)
- [ ] Runs clean on a machine without dev tools installed — **last remaining item.** Discharged in code terms (static, stripped, no assets, no external deps beyond SDL2), but the actual second-machine smoke test is a human task on the submission checklist
