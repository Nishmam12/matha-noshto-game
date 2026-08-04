---
tags: [design, phase, wayfarer]
phase: 0
status: done
updated: 2026-08-04
---

# Phase 00 — Foundations

**Status:** DONE — committed as part of `5ffdb38` (the Art Bible file) plus out-of-vault memory
writes that predate any commit (memory isn't tracked in git).
**Depends on:** nothing.
**Blocks:** every later phase, because it's what makes later phases *consistent* with each other
rather than each session re-deciding the same questions.

## Why this phase

Two things were true before this session and would have stayed true indefinitely without a
deliberate fix: **there was no art-direction note anywhere in the vault** (palettes existed only as
undocumented hex constants inside `src/main.c`), and **there was no record of which Claude Code
skills apply to this specific project**, which meant every session would re-evaluate ~40 installed
skills from scratch or, worse, guess. Both are the kind of thing that should be decided once and
referenced forever after, not re-litigated per session.

## Definition of done

- [x] A persistent memory record exists (outside the vault, at
      `C:\Users\nabil\.claude\projects\g--1-44mb-game\memory\`) covering: which skills apply and
      which don't, how to screenshot the game, the zero-external-assets rule, the backbone/team
      framing, and why subagents are avoided on this project.
- [x] `design/Art Bible.md` exists, covering all 9 sections of the visual-identity structure
      (identity statement, mood, shape language, colour system, character direction, environment
      language, UI language, asset standards, reference direction), explicitly marked provisional.
- [x] Both are linked into the vault graph — no orphan notes.

## Concrete tasks (as executed)

1. Wrote five memory files and updated the memory index:
   - `wayfarer-skill-policy.md` — the short list of skills that apply (`art-bible`, `run`,
     `code-review`, `simplify`, `caveman` if spend tightens) versus the long list that target
     web/mobile stacks and don't transfer to a C program writing pixels into an SDL surface.
   - `wayfarer-screenshot-recipe.md` — `--shot`/`--overlay` exist only in the self-test binary,
     output BMP, and need a `System.Drawing` conversion step before they're readable as an image.
   - `wayfarer-asset-pipeline.md` — the zero-external-files rule and the build-time bake decision.
   - `wayfarer-team-context.md` — the backbone/provisional-art framing and the user's own words
     about what "good enough" means.
   - `no-subagents-on-wayfarer.md` — two `Explore` subagents died on a monthly spend limit
     mid-session; this project reads `src/main.c` directly instead.
2. Attempted the installed `art-bible` skill directly. It requires `design/gdd/game-concept.md`
   (from a `/brainstorm` step not installed here) and mandates spawning `art-director` /
   `technical-artist` / `creative-director` agents that don't exist in this environment — it would
   hard-fail at its own Phase 0. **Used its 9-section structure, authored the content directly**
   rather than running the skill mechanically. No fabricated agent sign-off; the gap is recorded as
   an open item in the Art Bible itself.
3. Linked `[[Art Bible]]` from `Wayfarer MOC.md` under Design, and cross-referenced it from
   `[[Fog and Reveal]]` and `[[Isometric Rendering]]`'s existing "mockup vs. in-engine" caveats.

## Verification gate

None — this phase produces documentation and persistent memory, not code. Its only "test" is
whether later phases actually reference it instead of re-deriving the same decisions, which is
checked qualitatively per session, not by an automated gate.

## Traps specific to this phase

- **A skill that looks like it fits can still hard-fail on a missing prerequisite.** Check what a
  skill actually requires (`design/gdd/*`, specific agent types) against what the project actually
  has before assuming it will run cleanly. `art-bible` was the right *structure* and the wrong
  *mechanism* for this project.
- **Memory outside the vault is invisible to `git status` and to anyone reading only the repo.**
  If a future session's behaviour seems to ignore the skill policy or the team framing, check
  whether the memory files actually loaded — don't assume the policy was never written.

## Evidence

- `design/Art Bible.md` created, 5 memory files created, `MEMORY.md` index updated with pointer
  lines for all 5. No build was required for this phase; it's a documentation-only change bundled
  into the same commit as Phase 01's code (`5ffdb38`) since they landed in the same session.
