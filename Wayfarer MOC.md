---
tags: [moc, wayfarer]
---

# Wayfarer — Project Hub

Exploration-driven memory-restoration game for the 2P Game Arcade 1.44MB Floppy Disk contest. Deadline: **September 4, 2026, 23:59**.

## Process
- **[[Handover]] — start here if picking this up cold.** Current state, build commands, test suite, decisions already made, and every trap that has already cost time once.
- [[Agent Prompt]] — role, engineering loop, QA rules, language/toolchain rationale for whoever (human or agent) is writing code against this plan.

## Design
- [[Overview]] — core concept, confirmed pillars/naming, loop, judging-criteria alignment
- [[World Generation]]
- [[Fog and Reveal]]
- [[Abilities]]
- [[Fragments]]
- [[Found Souls]]
- [[Audio and Synth]]
- [[Save and UI]]
- [[Open Decisions]] — unresolved questions to settle before/during Week 1–2
- [[Cut List]] — pre-committed descoping order if time runs short
- [[QA Checklist]] — continuous verification rules

## Progress
- devlog/INDEX.md — running log of every build session, current `.exe` size, current status. Every session note there links back to whichever system(s) it touched.

## Constraints (do not lose these)
- Byte budget: target comfortably under 1,440,000 bytes (safety margin below the 1,474,560-byte floppy-standard limit — "1.44MB" has three different definitions, so build in margin)
- Zero external asset files — everything procedural (terrain, visuals, music, SFX)
- C, SDL2 core only, static-linked, stripped — see [[Agent Prompt]] for full rationale
- Judging order: **finished → under size → fun**, in that priority order
