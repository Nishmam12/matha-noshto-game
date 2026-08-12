---
tags: [moc, wayfarer]
---

# Wayfarer — Project Hub

Exploration-driven memory-restoration game for the 2P Game Arcade 1.44MB Floppy Disk contest. Deadline: **September 4, 2026, 23:59**.

## Process
- **[[Project Status]] — progress to date and everything still outstanding**, prioritised against the deadline. Read this to decide *what to do next*; read [[Handover]] to understand *how anything works*.
- **[[Handover]] — start here if picking this up cold.** Current state, build commands, test suite, decisions already made, and every trap that has already cost time once.
- [[Agent Prompt]] — role, engineering loop, QA rules, language/toolchain rationale for whoever (human or agent) is writing code against this plan.

## Design
- [[Overview]] — core concept, confirmed pillars/naming, loop, judging-criteria alignment
- [[Art Bible]] — **provisional** visual identity: palette ramps, shape language, fog destination, and the sprite/bake asset contract
- [[World Generation]]
- [[Fog and Reveal]]
- [[Isometric Rendering]]
- [[Abilities]]
- [[Fragments]]
- [[Found Souls]]
- [[Audio and Synth]]
- [[Save and UI]]
- [[Phase 13 - Aetherhold Castle]] — **NEW** handcrafted castle island + connecting land + modular dungeon (spec + two reference maps)
- [[Open Decisions]] — unresolved questions to settle before/during Week 1–2
- [[Cut List]] — pre-committed descoping order if time runs short
- [[QA Checklist]] — continuous verification rules
- [[Bug Fix Plan]] — **NEW** 8 confirmed bugs from a full codebase audit (2026-08-11), with exact
  fixes and verification steps, ready to hand to an implementation session
- [[Character Switch Plan]] — **NEW** (2026-08-12) the new 6-direction player art measured and
  specced: sheet geometry, the bob-not-stride finding, the six-way facing, and what the baker still
  needs. Executable punch list in `handoff/TASK-01-character-switch.md`

## Roadmap
- [[Phase Roadmap]] — the forward plan, phase by phase, in `design/phases/`. Read this before picking a task; it sequences everything left with a definition of done and a verification gate per phase, so the plan doesn't get re-derived from scratch each session.

## Progress
- devlog/INDEX.md — running log of every build session, current `.exe` size, current status. Every session note there links back to whichever system(s) it touched.

## Constraints (do not lose these)
- Byte budget: target comfortably under 1,440,000 bytes (safety margin below the 1,474,560-byte floppy-standard limit — "1.44MB" has three different definitions, so build in margin)
- Zero external asset files — everything procedural (terrain, visuals, music, SFX)
- C, SDL2 core only, static-linked, stripped — see [[Agent Prompt]] for full rationale
- Judging order: **finished → under size → fun**, in that priority order
