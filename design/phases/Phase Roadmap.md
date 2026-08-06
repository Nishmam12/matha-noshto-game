---
tags: [design, phase, wayfarer, moc]
updated: 2026-08-06
---

# Phase Roadmap — Wayfarer

Hub for the forward plan. [[Handover]] is the single-file context dump for picking the project up
cold; this folder is the detail behind its §11. Each phase gets its own file: why it exists, why it
sits where it does in the sequence, what "done" means, exactly which functions and lines it touches,
and how it gets verified. Read the phase file before starting its work — don't re-derive the plan
from memory each session.

> **Picking this up cold? Phases 00–12 are DONE.** Phase 12 (dream realm) is feature-complete;
> Phase 11 (ship-critical) landed audio, the HUD, and the QA/submission items. Phase 13
> (Aetherhold Castle) is **READY** — plan for Slice 1–2 approved (new key @ mainland watchtower,
> NE coast reserve `92,8,52×34`, causeway + courtyard first, dungeon deferred). Spec + two
> reference maps (Castle Island + Connecting Land) are in `Phase 13 - Aetherhold Castle.md`; the
> sliced plan is `Phase 13 - Aetherhold Castle Plan.md`. The build is complete through ship-
> critical: real art, world motion, five-layer synth, HUD, save/load. What remains is the
> Aetherhold Slice 1 build + the submission checklist — second-machine smoke test, repo visibility,
> final wrap.

**Deadline: 2026-09-04.** Today: 2026-08-06. **Hard stop on art/backbone work: 2026-08-14** — Phase
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
| — | **House geometry fix** (unplanned) | **DONE** — `b4bf446`, +512 bytes | — | Not a planned phase. The user reported the houses as "wrong size and wrong geometry, a mess overall". The roof had been drawn half a tile off its own walls since buildings landed, which also put the windows on the roof; walls were half the height the footprints needed |
| 05 | [[Phase 05 - Verification Debt]] | **DONE** — `4847bf5`, +0 bytes | 01, 02 | `--land-test` and `--fog-test` didn't exist. Cheapest to write while the systems they test are still fresh, and before more is built on top of them |
| 06 | [[Phase 06 - Water And Bridges]] | **DONE** — `e42f2f4` (rivers + bridges) + part 2 (waterfalls, `--bridge-test`), +0 bytes | 01, 05 | Needs the island's water body to exist (01) and needs its own reachability extension tested (05's discipline) before rivers can cut through walkable ground |
| 07 | [[Phase 07 - Asset Seam]] | **DONE** — bake pipeline + 37 real sprites wired, +62,976 bytes | 03 | The bake pipeline's output needs to be *judged* against something, which means the tuning overlay (03) should exist first |
| 08 | [[Phase 08 - Save Load]] | **DONE** — `feat/phase-08-save-load`, +1,536 bytes | — | Independent of the art work; scheduled here because it's cheap, high-value for a team, and Week 5 work worth pulling forward |
| 09 | [[Phase 09 - Placeholder Art]] | **DONE** — `feat/phase-09-restoration` (`57b7de9`), merged into `main` via Phases 08+09 PR. Restoration rebuild (ruin→whole), worn paths, ground marks — all three remaining items shipped with tests | 03, 07 | The actual art pass — the team's handoff arrived early, so 07 did the placement work this phase was holding. The three items that survived into this phase (rebuild, paths, marks) were the ones only a focused art pass could do |
| 10 | [[Phase 10 - Motion]] | **DONE** — `feat/phase-10-motion`, +2,048 bytes (781,824), `--motion-test` green | 09 | Motion on unfinished art just animates the wrong thing; static art should be right first |
| 12 | [[Phase 12 - Dream Realm]] | **DONE** — all 11 tasks, all 5 slices, `feat/phase-12-dream-realm` | 01, 05, 06, 07 | A portal in the `TERRAIN_DARK` region to a second biome in the team's Lumiara style. Resolves three things at once: the dark region had no reason to visit, Kindle had no reason to exist, and 56 delivered magical FX frames had no caller. Timeboxed into five separately shippable slices — it did not move the 2026-08-14 hard stop |
| 11 | [[Phase 11 - Ship Critical]] | **DONE** — `feat/phase-11-ship-complete` (`8a27404` + shard fix `7cd408a` + rescale `068fea8` + minimap `a8b178f`), 786,944 bytes (+5,120), 653,056 headroom. 5-layer softsynth (deterministic, 0.325 ms worst case), chime/shard/portal SFX, HUD (counters, minimap 1px, toasts, win banner) on the font extended with lowercase, `--hud-test` green, full suite green, `nm` clean of SDL_image/ttf/mixer | — | Audio, font-dependent HUD, save wiring, QA, submission. Not negotiable against the deadline |
| 13 | [[Phase 13 - Aetherhold Castle]] | **READY** — plan for Slice 1–2 approved (new key @ `112,18` watchtower, NE reserve `92,8,52×34`, causeway + courtyard first, `Phase 13 - Aetherhold Castle Plan.md`) | 12, 07 | First handcrafted chapter outside the village: causeway-gated castle island + connecting forest, hybrid fixed-layout + procedural decoration, modular dungeon deferred — Slice 1 is NE reserve + gate + watchtower key + `--aether-test` + save `v2` |

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
