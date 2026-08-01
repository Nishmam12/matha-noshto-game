---
tags: [design, qa, wayfarer]
---

# QA Checklist

Apply continuously, not just at the end. See [[Agent Prompt]] for the engineering loop this supports.

- [ ] Every generation run is seed-reproducible and loggable — see [[World Generation]]
- [ ] Batch-test ≥20 seeds after any change to generation or placement logic
- [ ] No unreachable fragments/Found Souls at any ability tier, in any tested seed — see [[World Generation]], [[Abilities]]
- [ ] Audio callback never allocates, never locks, profiled under full 5-layer load — see [[Audio and Synth]]
- [ ] `.exe` size measured and logged after every build; flag at 1.2MB soft threshold
- [ ] No SDL_image / SDL_ttf / SDL_mixer linked in final build — see [[Agent Prompt]]
- [ ] No shipped asset files of any kind in the final package
- [ ] Runs clean on a machine without dev tools installed
