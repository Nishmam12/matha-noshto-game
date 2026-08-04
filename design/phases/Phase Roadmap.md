---
tags: [design, phase, wayfarer, moc]
updated: 2026-08-04
---

# Phase Roadmap — Wayfarer

Hub for the forward plan. [[Handover]] is the single-file context dump for picking the project up
cold; this folder is the detail behind its §11. Each phase gets its own file: why it exists, why it
sits where it does in the sequence, what "done" means, exactly which functions and lines it touches,
and how it gets verified. Read the phase file before starting its work — don't re-derive the plan
from memory each session.

**Deadline: 2026-09-04.** Today: 2026-08-05. **Hard stop on art/backbone work: 2026-08-14** — Phase
11 (ship-critical) begins on that date regardless of how much of Phases 03–10 is finished, because
[[Overview]] and [[Handover]] both name the judging order as **finished → under size → fun**, and a
beautiful, unfinished game scores worse than a plain, complete one.

## Sequence and status

| # | Phase | Status | Depends on | Why here |
|---|---|---|---|---|
| 00 | [[Phase 00 - Foundations]] | **DONE** — `5ffdb38` | — | Memory, skill policy and a provisional Art Bible had to exist before any art judgement call could be made consistently |
| 01 | [[Phase 01 - Landform]] | **DONE** — `5ffdb38` | 00 | The world's *shape* was cave noise; every later visual decision was being made on top of a landform that couldn't be fixed by shading alone |
| 02 | [[Phase 02 - Roof And Fog]] | **DONE** — `e5c8942` | 01 | The two defects that most directly caused the user's original complaints ("models are a mess" → roofs; "traversal feels suffocating" → fog), fixed once the ground they sit on was coherent |
| 03 | [[Phase 03 - Legibility Tools]] | **DONE** — `60b4e3a`, +0 bytes | 02 | Phase 02's colour tuning took three guess-rebuild-screenshot passes. That loop does not scale to the remaining art work and must be replaced before more of it happens |
| — | **World rescale** (unplanned) | **DONE** — `e03138d`, −512 bytes | — | Not a planned phase. The user reported everything reading as too big; the cause was that all art was authored in absolute px against `TILE == 32`, so `TILE` did not actually scale the art. `PX()` fixes that and `TILE` 32→24 applies it |
| 04 | [[Phase 04 - Traversal]] | **DONE** — `dd8cfef`, +512 bytes | 03 | Screen-aligned input is a simulation change and needs isolation from every render-only phase around it, per [[Agent Prompt]]'s rule on flagging architectural changes |
| 05 | [[Phase 05 - Verification Debt]] | **NEXT** | 01, 02 | `--land-test` and `--fog-test` don't exist yet. Cheapest to write while the systems they test are still fresh, and before more is built on top of them |
| 06 | [[Phase 06 - Water And Bridges]] | Planned | 01, 05 | Needs the island's water body to exist (01) and needs its own reachability extension tested (05's discipline) before rivers can cut through walkable ground |
| 07 | [[Phase 07 - Asset Seam]] | Planned | 03 | The bake pipeline's output needs to be *judged* against something, which means the tuning overlay (03) should exist first |
| 08 | [[Phase 08 - Save Load]] | Planned | — | Independent of the art work; scheduled here because it's cheap, high-value for a team, and Week 5 work worth pulling forward |
| 09 | [[Phase 09 - Placeholder Art]] | Planned | 03, 07 | The actual art pass (buildings, restoration rebuild, player, paths) — needs the seam (07) to exist so it's placed correctly relative to the team's future handoff |
| 10 | [[Phase 10 - Motion]] | Planned | 09 | Motion on unfinished art just animates the wrong thing; static art should be right first |
| 11 | [[Phase 11 - Ship Critical]] | **Begins 2026-08-14 regardless** | — | Audio, font-dependent HUD, save wiring, QA, submission. Not negotiable against the deadline |

## How to read a phase file

Each one follows the same shape:

- **Status / depends on / blocks** — where it sits
- **Why this phase** — the concrete problem it exists to solve, not a generic description
- **Definition of done** — a checklist, not a feeling
- **Concrete tasks** — ordered, with file/function/line references where they're known
- **Verification gate** — which tests must pass, and which negative control (if any) is owed
- **Traps specific to this phase** — pointers into [[Handover]] §7 plus anything phase-local
- **Evidence**, for done phases — commit hash, byte delta, actual test output

## Cross-cutting rules that apply to every phase

These are restated from [[Agent Prompt]] and [[Handover]] because they're easy to let slip mid-phase:

- **plan → implement → build → measure → verify → report**, one slice at a time. Don't batch
  unverified features across phase boundaries either.
- Report the exact `.exe` byte size and delta after every build.
- Every new checker needs a negative control.
- Collision reads `solid` and `regions[].terrain` and nothing else. If a phase's code ever makes
  collision read `height`, `bld_at`, or `surf`, stop — that's a decision to flag, not a detail to
  fix later.
- State plainly what a phase did **not** verify when it's marked done. "Done" means the definition
  of done was met, not that everything about the feature is known-good.

## What's explicitly NOT in this roadmap

- **Re-litigating the isometric renderer, the C+SDL2 toolchain choice, or the region-graph world
  model.** Those are closed decisions — see [[Handover]] §6 items 1–15.
- **A phase for "make it look like the reference image."** That's not a phase, it's the standard
  every phase from 03 onward is held to — see [[Art Bible]] §9's divergence rule.
- **Anything from [[Cut List]].** That list activates only if the deadline forces it; it is not part
  of the plan of record.
