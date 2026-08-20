---
tags: [process, status, wayfarer]
updated: 2026-08-12
exe_size_bytes: 1091584
deadline: 2026-09-04
---

# Project Status — Wayfarer

**Progress to date and the work still outstanding.** Written 2026-08-12, **23 days before the
deadline**.

Cold-start context: [[Handover]] · Rules of engagement: [[Agent Prompt]] · Hub: [[Wayfarer MOC]] ·
Phase-by-phase plan: [[Phase Roadmap]] · Session log: [[INDEX]] · Descoping order: [[Cut List]] ·
Human QA items: [[QA Checklist]]

> **Working mode changed 2026-08-12.** The project is now worked solo by Claude. The short-lived
> Claude-plus-Cline tandem arrangement is retired — see §6 for the files it left behind.

---

## 1. Where things stand

| | |
|---|---|
| **Release binary** | **1,091,584 bytes** — 348,416 under the 1,440,000 ship target, 382,976 under the 1,474,560 hard limit |
| **Baked art** | 143 sprites, 376,571 bytes of const data |
| **Build** | Clean, **zero warnings** under `-Wall -Wextra`, `-std=c99` |
| **Test suite** | **25/25 default gate green**; `--play-test --seeds 50` **50/50**; `--audio-test` PASS (0.196 ms of a 21.333 ms deadline) |
| **Known suite failure** | `--land-test --seeds 500` fails seeds **85 / 417 / 430** — pre-existing, unchanged since `428c9fd` |
| **Branch** | `feat/phase-14-bug-fixes`, **32 commits ahead of `origin/main`** (`b4f2fde`) |
| **Deadline** | **2026-09-04** — 23 days |
| **Judging order** | **finished → under size → fun** |

**The size budget is not the constraint and never has been.** There is a third of a megabyte
spare. The real risks are all at the other end: nothing is merged, nothing has run on a second
machine, and almost nothing has been judged by a human for whether it is any *fun*.

## 2. What is built

All twelve planned phases are complete, plus a thirteenth and fourteenth that were added along the
way. [[Handover]] §2 carries the detailed inventory; the short version:

- **World** — procedural island, elevation and cliffs, rivers/bridges/waterfalls, fog-as-haze
  reveal, a 164×157 two-sector grid holding both the overworld and the dream realm.
- **Loop** — 14 fragments and 5 Found Souls, restoration returning colour and synth layers,
  ability gating (Wade/Climb/Kindle), a portal that is a graph edge rather than a collision case,
  8 dream shards feeding a Dream Well, win condition, save/load.
- **Presentation** — isometric renderer with depth sort and prop-fade, 5-layer deterministic
  softsynth plus SFX, HUD with minimap/toasts/win banner, ambient motion, a bitmap font.
- **Aetherhold** — a water-locked top-right castle island reached by a causeway that opens when the
  Dream Well's Soul is restored.
- **Verification** — 23 self-tests, **each with a negative control**, plus headless playthroughs.

### Recent sessions

| Session | What landed |
|---|---|
| 14 | [[Bug Fix Plan]] in full — 8 issues including a real heap overflow in `iso_diamond` and three self-tests that could not fail on the fault they claimed to check |
| 15 | Invisible-wall squares, foliage bans, 30% prop thinning, single-castle island, cluster fog, `--path-test` green |
| 16 | **Character switch** — the new six-way character baked and wired (136 sprites, bob cycle on `g->clock`) |
| 17 | Review of 16: two self-tests that asserted nothing rebuilt with real controls; the baker's duplicated sprite encoder collapsed into one (proven by a byte-identical re-bake) |
| 17b | **Levitating houses fixed** — a Session 15 regression that stood every building on a packed-earth plinth |
| 18 | **Walk cycle wired** — 48 walk records replace the 42 idle ones, driven by `Player.anim`; standing holds the last frame. The `--move-test` check for it passed against a broken build on the first try and had to be rebuilt around a vacuity guard |

---

## 3. Outstanding work

Ordered by what actually threatens the submission, not by how interesting it is.

### P0 — submission blockers

1. **Commit the working tree.** The character switch, review fixes and plinth fix landed in
   `a7884c0` and are pushed — that entry is discharged. **Session 18's walk cycle is uncommitted
   right now** (`src/main.c`, `src/art_data.h`, `tools/bake.ps1`, two design notes). Same standing
   risk, new contents.
2. **Merge to `main`.** `origin/main` is `b4f2fde`; the working branch is **33 commits ahead**.
   Local `main` is a further 39 commits stale and should be reset to the remote before anything is
   merged into it.
3. **Second-machine smoke test.** Never run, on any machine, ever. The build is static, stripped and
   asset-free so it *should* be clean, but "should" is not a test. This is a **human task** and the
   last unchecked item on [[QA Checklist]].
4. **Repo visibility.** Private today. The contest's requirement is unconfirmed — confirm it, then
   act on it.
5. **Tag the submission commit** and do a final size audit against the exact byte limit.

### P1 — visible defects and half-finished work

6. **The restoration rebuild is invisible for baked buildings.** The finished house sprite draws at
   *every* phase, so a village looks restored before you have restored it — the ruin→whole
   progression that Phase 09 built is not on screen. `--rebuild-test` passes because it tests
   `bld_phase`, the pure predicate, not the render path. Fix by either giving the procedural ruin
   path visible geometry at ground level, or recomputing footprint heights when a region crosses a
   phase boundary. **Needs a negative control that fails without the fix.**
7. ~~**The walk cycle is delivered but not wired.**~~ **DONE (Session 18).** The six `walk_*` sheets
   are baked as **48 records** and replace the 42 idle ones; `player_sprite_id` indexes them off
   `Player.anim`, which is no longer reset on key release, so **standing holds the frame the stride
   stopped on**. Idle sheets stay on disk unbaked. Const data **+1,896 B** for the character;
   release 1,085,440 → **1,091,584**. Full gate green, with the new checks proven against a
   deliberately broken build. See [[Character Switch Plan]] §6.
8. **The walk cadence is an unjudged guess.** `WALK_FPS` is **12.0** — 1.5 cycles/sec, chosen
   because 8 fps over 8 frames would halve the old set's cadence and read as slow motion. Nobody
   has watched it. **16.0 restores the old cadence.** One `#define` next to `PLAYER_SPEED`.
9. **Only two of six facings have been seen on screen.** `idle_down` was confirmed before the
   switch; the walk `down` frame is confirmed at spawn now. The other five are correct by
   construction and covered by the row-distinctness check, but unphotographed. The walk *cycle* is
   likewise proven by test, not by eye — `--autoplay` is wall-clock driven and the camera deadzone
   moves the player off centre, so an animated strip needs a debug flag that does not exist yet.
10. **The standing pose is completely static** — no breath, no sway, by design (D5). Whether that
    reads as intentional or as a hang is a human call nobody has made.

### P2 — quality, and the "is it fun" question

11. **Nobody has heard the audio.** Five layers, deterministic, 0.196 ms worst case — all *measured*,
    never *listened to*. "Does it sound good" is unanswered.
12. **`--land-test` fails 3 of 500 seeds** (85, 417, 430): the player reaches only 22–23% of open
    tiles against a 50% bar. Pre-existing generation quality, not a regression. Get it green before
    trusting the suite to catch a new one.
13. **Shard-vs-decoration legibility.** The collect-8-find-6 loop's pickups still read like ambient
    `PROP_CRYSTAL` decoration in stills. A palette/size pass, unjudged.
14. **Pacing has never been re-measured.** The 30–82 second full-clear figure predates two tile-size
    changes and a grid that is now 1.8× larger.
15. **One playtest is not QA.** The game has been played by hand a handful of times, mostly by the
    person who designed it.
16. **Wade-splash SFX** — deferred per [[Cut List]] #5. Documented, not forgotten.

### P3 — cleanup

17. **Dead code from the character switch.** `Player.facing` and the four-way `FACE_*` enum are
    written every frame and read by nothing. **`Player.anim` is no longer dead** — Session 18
    revived it as the walk phase, and it is now the field the held standing pose depends on, so it
    must not be removed. Deleting the other two touches the `Player` struct, so it wants its own
    pass with `--save-test` re-run.
18. **An untracked `assets/buildings/bld_house_small_v4_frame_0.png` is being baked with no caller.**
    The `buildings` category is globbed, so it rode into the Session 18 bake at a cost of **4,285 B**
    — it is not in `art_bld_small[]`/`art_bld_large[]`, so nothing draws it. Wire it or move it out;
    it is the whole reason the release grew 6,144 B for a 1,896 B character change.
19. **`assets/castle/` (126 PNGs) and `assets/magical/`'s remaining frames are committed but
    unbaked**, having no caller. That is correct under the "never bake what nothing draws" rule, but
    the folders are worth a decision rather than drift.
20. **[[Handover]]'s git section is stale** — it names `main` as the merge base without noting the
    32-commit divergence, and the local `main` branch is 39 commits behind the remote.
21. **The `.import` sidecars** (Godot editor metadata) are committed and read by nothing. Still
    undecided whether they belong in the repo.

---

## 4. Open decisions

- **The watchtower sits across the forest road.** At `(74,33)` its 3×3 ring turns two road tiles to
  rock. Inherent to the "6 tiles west of the gate" brief; reachability is unaffected. Either accept
  it or move to the wide shelf branch near `(79,25)`, which is clear of the road but no longer due
  west of the gate. See [[2026-08-11-session-01]].
- **The new character is ~25% shorter than the old one** (bbox 22–26 px against 34). It reads
  legibly at `TILE 18`, but it is a look, not a fact. See [[Character Switch Plan]].

## 5. Suggested order

With 23 days and the game already feature-complete and comfortably under budget, the highest-value
work is **not more features**.

1. **Today** — commit everything (P0.1). Then merge to `main` (P0.2).
2. **This week** — the remaining visible defect: the restoration rebuild (P1.6). The walk cycle
   (P1.7) is done. Both were the kind of thing a judge notices.
3. **Then** — the human passes nothing else can substitute for: play it, listen to it, watch the
   walk cadence and the held standing pose, and run it on a second machine (P0.3, P1.8, P1.10,
   P2.11, P2.15).
4. **Only if time remains** — the 3/500 land seeds and the shard legibility pass (P2.12, P2.13).
5. **Last week** — freeze, final size audit, tag, repo visibility (P0.4, P0.5).

**Judging order is finished → under size → fun.** The project is already "under size" by a third of a
megabyte. Everything above is in service of the other two.

## 6. Files left by the retired tandem workflow

Three artifacts exist from the brief Claude-plus-Cline arrangement, all untracked:

- **`.clinerules`** — standing rules that Cline auto-loaded. Now redundant with [[Agent Prompt]],
  which is the authoritative version. Its one genuinely new contribution is that it states the traps
  compactly in one place.
- **`handoff/TASK-01-character-switch.md`** — the brief for the character switch. Complete; kept as a
  record of what was specified versus what was built.
- **[[Character Switch Plan]]** — **keep this one.** It is a design note, not a workflow artifact,
  and it holds the measured facts about the new art that nothing else records.

Decide whether to commit or delete the first two; do not delete the third.
