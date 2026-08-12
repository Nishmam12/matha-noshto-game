---
tags: [handoff, wayfarer, character]
date: 2026-08-12
task: 01
---

# TASK 01 — switch the player to the new character art

Design decisions and the measurements behind them: **[[Character Switch Plan]]** — read it first.
Standing rules: `.clinerules` and [[Agent Prompt]]. Cold-start context: [[Handover]].

Replace the old 4-direction walk character with the new 6-direction bob-cycle character in
`assets/player_new/`. Everything below is in `src/main.c` (single translation unit) and
`tools/bake.ps1`.

**Do the groups in order — each depends on the one before it.** Within a group, follow the stated
sequence; where a test is named *first*, write it first. Skipping that has cost this project a
wasted debugging loop before.

---

## Facts you do not need to re-derive

Measured already; trust these:

- Each sheet is `384×64` = **8 frames at a pitch of 48**, cell `48×64`. (Not 64 — assuming 64
  produces garbage.)
- **Frame 7 is pixel-identical to frame 0 in all six sheets.** The cycle is **frames 0–6**. Do not
  bake frame 7.
- The motion is a **vertical bob, not a stride** — `opaque_x` is identical across every frame. The
  same cycle therefore serves both standing and moving. There is no missing walk set.
- The art contains **none** of the four `$script:KeyMagenta` colours, so nothing will be stripped.
- Feet sit at y≈42–43 on both old and new art, and the baker anchors bottom-centre, so the taller
  cell needs no anchor work.

---

## Group A — teach the baker to slice sheets (do first)

**Where:** `tools/bake.ps1`. One PNG currently becomes one record (`:303`); `generated/` is excluded
partly *because* it holds sheets (`:41`). There is no slicing today.

**The change:** add a sheet table in the style of the existing `$script:FxFrames` (`:77`), giving
path, identifier stem, frame width and frame count. Emit **contiguous ids** the way
`ART_FX_PORTAL_0..14` already are, so the renderer can index a run:

```
ART_CHAR_IDLE_DOWN_0 .. _6,  ART_CHAR_IDLE_RIGHT_DOWN_0 .. _6,  ART_CHAR_IDLE_RIGHT_UP_0 .. _6,
ART_CHAR_IDLE_UP_0 .. _6,    ART_CHAR_IDLE_LEFT_UP_0 .. _6,     ART_CHAR_IDLE_LEFT_DOWN_0 .. _6
```

Sheets live in `assets/player_new/`, which is **not** in `$Categories` — leave it that way. The
folder glob would bake each sheet as one wide sprite; only the sheet table may touch these files.

**Verify — before looking at anything on screen:**
```powershell
powershell -File tools\bake.ps1
```
- Record count goes **110 → 152** (42 added, nothing removed yet). The script prints the exact
  count and const-data bytes — trust that over this document.
- Then `.\build.ps1 -SelfTest` and `--sprite-test`. It already checks RLE round-trip, bottom-centre
  anchors and key-magenta absence; all three must pass on the new records.
- **Sanity-check one slice against the source** before proceeding: assert that a sliced frame's
  opaque pixel count matches the same region of the PNG. A slicer with an off-by-one in the x offset
  will still produce plausible-looking sprites.

## Group B — the six-way facing, test first

**Where:** `src/main.c`. `FACE_*` enum at `:1104`, `Player.facing` at `:1112`, and the derivation
block at `:2998-3006`.

**Write the checker before the wiring.** `facing6_from_intent(sx, sy)` is a pure function, so sweep
it directly the way `--fade-test` already sweeps `prompt_bob` and `well_frame` — do not infer it
from pixels.

**The change:**
1. Add `FACE6_DOWN, FACE6_RIGHT_DOWN, FACE6_RIGHT_UP, FACE6_UP, FACE6_LEFT_UP, FACE6_LEFT_DOWN,
   FACE6_COUNT`, ordered around the circle.
2. Add `facing6_from_intent(float sx, float sy)`, pure. Mapping and the pure-horizontal tie-break
   are specified in [[Character Switch Plan]] §D1 — **pure left/right resolve to the *down*
   diagonal**, because the art has no side view.
3. Add `Uint8 facing6` to `Player` beside `facing`, set in the **same block** at `:2998` from the
   same screen intent `(sx, sy)`. **Not** from world velocity `(mx, my)` — the comment at
   `:2988-2991` explains why. Leave the existing `facing` in place for now.

**Render-only.** Nothing in movement, collision, save/load or the verifier may read `facing6`. The
save format does not change.

**Verify:** a new selftest case asserting all eight intents including both pure-horizontal
tie-breaks, **plus a negative control** — e.g. a world-velocity-derived variant — shown *failing*
before the fix and passing after. A checker that has never rejected anything proves nothing.

## Group C — draw the new character

**Where:** `player_frames[][]` (`:5037`), `player_sprite_id` (`:5044`), and its two callers at
`:6113` (depth-sort box) and `:6659` (the draw). Both have `g` in scope.

**The change:**
1. New `player_idle[FACE6_COUNT][7]` table beside `player_frames`, in the same style — the art's
   naming is translated **exactly once**, here.
2. `player_sprite_id` takes the clock: index the row by `p->facing6` and the frame by
   `(int)(clock * IDLE_FPS) % 7`.
3. **Drive it from `g->clock`, not `p->anim`.** `p->anim` is reset to zero the moment keys are
   released, which is correct for a stride and wrong for a bob — it would freeze the breath whenever
   the player stood still. `g->clock` exists for exactly this (`:3008-3012`).
4. `IDLE_FPS` starts at `8.0f`, matching `WALK_FPS`. This is a starting guess to be judged on
   screen, not a measured choice.
5. Delete `player_frames[][]` and the now-unused `WALK_FRAMES`/`WALK_FPS` if nothing else reads them.

## Group D — stop baking the old set, then measure

**The change:** remove `"player"` from `$Categories` (`tools/bake.ps1:47`). Nothing references
`ART_CHAR_PLAYER_*` after Group C, and baking art with no caller is pure byte cost.

**Leave the old PNGs on disk.** Unbaked, not deleted — that keeps the switch reversible without a
`git revert`.

**Verify:**
```powershell
powershell -File tools\bake.ps1     # record count 152 -> 136
.\build.ps1                          # report exact bytes and delta from 1,080,320
```
Release **must** stay under 1,440,000. Flag and stop if it crosses 1,200,000 — currently only
~120 KB above the present size, so this is a live threshold, not a formality.

## Group E — look at it, then write it down

**This is the real gate.** Every visual bug of consequence in this project was found by a screenshot
and never by a test.

- Screenshot **all six facings**, standing and moving, at `TILE 18`.
- Confirm `idle_down` shows the character's **face**, not their back. The old art's compass naming
  was inverted (`n` = front, `:1102-1103`); the new files are named by **screen direction**, so
  `idle_down` = moving down-screen = toward the viewer. Getting this backwards is the single easiest
  mistake in this task.
- Confirm the bob is visible when standing, and that the cadence does not read as frantic.
- **The new character is ~25% shorter than the old one** (bbox 22–26 px vs 34). Judge whether it
  still reads at this scale. This is a judgement call, not a correctness one — report your opinion,
  do not silently rescale anything.
- Full default gate: **25/25** `--*-test` at `--seeds 20`. This change is render-only and touches no
  generation input, so no wide seed sweep is needed — but confirm `--land-test --seeds 500` still
  fails **exactly** seeds 85/417/430 and no others.

**Then document, per `.clinerules`:** a devlog entry (a `2026-08-12-session-01.md` already exists —
**append a new `## Session NN` section**, do not overwrite), update `devlog/INDEX.md` with the new
size and status, and **append** a row to the Agent Log in [[Handover]] as
`cline (muse-spark-1.2-contributor)`. Never rewrite another agent's row.

---

## Out of scope

Do not: change `FACE_COUNT` or the four-way model for anything other than the player sprite; add a
dream palette variant for the player (`$DreamCategories` stays `nature` — `tools/bake.ps1:64-67`
records why); rescale either art set; touch the watchtower-on-the-road open decision; or attempt the
pre-existing `--land-test` 3/500 failures.

Do not push, merge, or open a PR. Do not add `Co-Authored-By` trailers.

## Report back

What changed, the exact byte delta, what you verified, **what you did not verify**, and your
judgement on the two open questions: the bob cadence and the character's size on screen.
