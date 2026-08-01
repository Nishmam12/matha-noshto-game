---
tags: [moc, wayfarer]
---

# Wayfarer — Project Hub

Exploration-driven memory-restoration game for the 2P Game Arcade 1.44MB Floppy Disk contest. Deadline: **September 4, 2026, 23:59**.

## Process
- [[Agent Prompt]] — role, engineering loop, QA rules, language/toolchain rationale for whoever (human or agent) is writing code against this plan.

## Design
- [[Pending Team Discussion]] — game plan is not finalized yet; will be filled in here, one linked note per system, once the team has decided
- [[Toolchain Setup]] — build environment, why we compile our own minimal SDL2, and the ~250 KB size reserve we have not spent yet

## Progress
- [[INDEX]] — running log of every build session, current `.exe` size, current status. Every session note there links back to whichever system(s) it touched.

**Current build:** 669,696 bytes — 770,304 under the ship target. See [[2026-08-01-session-01]].

## Constraints (do not lose these)
- Byte budget: target comfortably under 1,440,000 bytes (safety margin below the 1,474,560-byte floppy-standard limit — "1.44MB" has three different definitions, so build in margin)
- Zero external asset files — everything procedural (terrain, visuals, music, SFX)
- C, SDL2 core only, static-linked, stripped — see [[Agent Prompt]] for full rationale
- Judging order: **finished → under size → fun**, in that priority order
