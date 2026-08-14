---
tags: [design, wayfarer, art, character]
date: 2026-08-12
status: superseded-in-part
---

# Character Switch Plan — the new player art

> **§1a and §D2 below are SUPERSEDED. Read [§6](#6-superseded-the-walk-set-ships-instead) first.**
> They describe the *idle* sheets, which are no longer the shipped art. Their conclusion — that the
> art is a bob with no gait, so one cycle can serve both standing and moving — was correct about the
> sheets it measured and is wrong about the sheets that ship. The walk set replaced them on
> 2026-08-12; everything else in this note still holds.

Hub: [[Wayfarer MOC]] · Process: [[Agent Prompt]] · Visual identity: [[Art Bible]] ·
The seam this goes through: [[Phase 07 - Asset Seam]] · Renderer: [[Isometric Rendering]] ·
Cold-start context: [[Handover]]

The team delivered a new player character in `assets/player_new/`. This note records what the art
**actually is** (measured, not assumed), the decisions the switch requires, and why. The executable
punch list is `handoff/TASK-01-character-switch.md`.

> **No prior plan for this switch existed.** Searched: every `.md` in the vault, full git history
> across all branches, stashes, untracked and ignored files, the project's Claude memory directory,
> `assets/generated/*.metadata.json`, and Cline's own storage. The nearest thing,
> `char_player_8dir.metadata.json`, is the generation prompt for the **old** brown-tunic adventurer.
> This note is that missing plan.

---

## 1. What the art is — measured

Six PNGs, one per facing: `idle_down`, `idle_up`, `idle_left_down`, `idle_left_up`,
`idle_right_down`, `idle_right_up`.

| | Old (`assets/player/`) | New (`assets/player_new/`) |
|---|---|---|
| File layout | one 48×48 PNG per frame, 16 files | **384×64 sheet per direction, 8 frames at pitch 48** |
| Frame cell | 48×48 | **48×64** |
| Directions | 4 (`n`/`e`/`s`/`w`) | **6** — down, up, and four diagonals |
| Character bbox | ~17×34 | **~12–15 × 22–26** |
| Feet line | y = 42 | y = 42–43 |
| Distinct colours | — | 19–25 per sheet |

**The frame pitch is 48, not 64.** A first pass assuming 64 produced a 12 px-wide character reading
as 60 px wide — the slot boundaries were cutting across neighbouring frames. At pitch 48 every
frame's bounding box is identical within a sheet, which is the check that confirms it. `384 / 48 = 8`.

**Feet sit at the same y in both sets** (42 vs 42–43), and the baker trims to the opaque box and
anchors bottom-centre, so the taller 64 px cell is harmless padding. No anchor work is needed.

**The art is clean against the baker's key-colour stripper.** None of the four magenta shades in
`$script:KeyMagenta` (`tools/bake.ps1`) appear in any sheet — checked explicitly, because the new
character has prominent magenta accents and a silent strip would have punched holes in it.

### 1a. It is a bob, not a stride

`opaque_x` is **identical across all 8 frames of every sheet** (`19-30` for all of `idle_down`,
`17-31` for `idle_right_up`, and so on). There is zero horizontal displacement, both feet stay
planted, and there is no leg alternation. The motion is a **vertical bob** — the body rises and
falls, with the head-and-torso band carrying 86–93 px of change per frame against the legs' 15–50.

This is recorded so nobody later files it as a bug. **It is a deliberate locomotion style, and it is
what makes the switch cheap:** because the cycle is a bob rather than a gait, *the same seven frames
serve both standing and moving*. There is no missing walk set. A ~26 px character bobbing while the
world translates underneath reads as locomotion at this scale.

### 1b. Seven frames, not eight

**Frame 7 is pixel-identical to frame 0 in all six sheets** — a loop-closing duplicate. `idle_down`
additionally repeats frame 2 at frame 6. Across all six directions, **41 of the 48 authored frames
are unique.**

The cycle is therefore **frames 0–6**. Baking frame 7 would spend a full record and pixel stream on
a duplicate of frame 0, against the project's standing rule that baking a sprite nothing draws is
pure byte cost.

---

## 2. Decisions

### D1 — Six-way facing, render-only

Add `FACE6_*` alongside the existing four-way `FACE_*`, ordered around the circle:

```
FACE6_DOWN, FACE6_RIGHT_DOWN, FACE6_RIGHT_UP, FACE6_UP, FACE6_LEFT_UP, FACE6_LEFT_DOWN
```

Derived from the **screen intent** `(sx, sy)` that already drives `facing`, in the same block — not
from world velocity `(mx, my)`. The comment at `src/main.c:2988-2991` already explains why: a screen
axis becomes a diagonal in world space, so deriving from world velocity would make every key produce
a diagonal. Render-only, exactly like `facing` and `anim` — nothing in movement, collision, save or
the verifier may read it.

The game's eight movement intents map onto the six directions cleanly except for pure horizontal:

| Screen intent | Facing |
|---|---|
| down / up | `DOWN` / `UP` |
| down+right, up+right | `RIGHT_DOWN`, `RIGHT_UP` |
| down+left, up+left | `LEFT_DOWN`, `LEFT_UP` |
| **pure right / pure left** | **`RIGHT_DOWN` / `LEFT_DOWN`** |

**The art has no pure left or right view**, so pure-horizontal intent must pick a diagonal. It picks
the *down* diagonal, keeping the character's face toward the viewer — the same reasoning the existing
code gives for favouring the vertical on ties (`src/main.c:2994-2995`).

### D2 — One cycle for standing and moving, driven by `g->clock`

`player_sprite_id` currently indexes the walk cycle off `p->anim`, which is **reset to zero the moment
the keys are released** so a standing player shows frame 0. That is right for a stride and wrong for a
bob: it would freeze the breath whenever the player stood still.

Drive the frame index from **`g->clock`** instead — the clock that deliberately does not stop when the
player does (`src/main.c:3008-3012`, added for exactly this class of problem). The character then
breathes when standing and bobs when moving through one code path with no state distinction.

`player_sprite_id` takes the clock as a parameter. Both callers (`src/main.c:6113` depth-sort box,
`src/main.c:6659` the draw) have `g` in scope.

Start at a single rate of 8 fps, matching the existing `WALK_FPS`. **Judge it on screen.** If the
standing breath reads too fast, split into separate idle and moving rates then — not before.

### D3 — Bake 7 frames per direction; stop baking the old set

- **Add** the six sheets as **42 records** (6 × 7), sliced by the baker.
- **Remove** `"player"` from `$Categories` in `tools/bake.ps1`. Nothing will reference
  `ART_CHAR_PLAYER_*` once the table is replaced, and baking it would be pure byte cost.
- The old PNGs stay on disk and in git history — unbaked, not deleted. That keeps the switch
  reversible without a `git revert`.

Net record count: **110 − 16 + 42 = 136**. The byte delta must be **measured**, not estimated —
`tools/bake.ps1` prints the exact record/stream count and const-data total on every run, and
`build.ps1` prints the release delta. Current release is 1,080,320 bytes with 359,680 of headroom
against the ship target, so there is room; the flag-and-stop threshold at 1,200,000 is the number to
watch.

> **Optional saving, not required:** if `tools/bake.ps1` gains pixel-stream deduplication — hash the
> stream, reuse `data_off`/`data_len` on a match — the one duplicate frame in `idle_down` collapses
> automatically. This is the same mechanism the 11 dream variants already use to share a twin's
> stream, so the precedent and the format support both exist.

### D4 — No dream palette variant

`$script:DreamCategories` stays `nature` only. `tools/bake.ps1:64-67` records the reasoning and it
still holds: the player is the same person on both sides of the portal, so a recoloured character
would read as a costume change rather than a change of place.

---

## 3. What this needs from the baker

`tools/bake.ps1` **cannot slice sheets today** — one PNG becomes one sprite record (`:303`), and
`generated/` is excluded partly *because* it contains sheets (`:41`). This is new work regardless of
every other decision above.

Add a sheet table in the style of the existing `$script:FxFrames`, giving path, identifier stem,
frame width and frame count, and emitting **contiguous ids** the way `ART_FX_PORTAL_0..14` already
are, so the renderer can index a run.

---

## 4. Risks and open items

- **The character is ~25% shorter than the old one** (bbox 22–26 px against 34). Nothing in the game
  is authored against the player's pixel height — collision uses `PLAYER_SIZE`, and the sprite has
  always been deliberately larger than the collision box (`src/main.c:6656-6658`) — so this is a
  judgement call, not a correctness one. **It has to be looked at on screen at `TILE 18`.**
- **Nobody has seen this character in the world.** Every visual bug of consequence in this project
  was found by a screenshot and never by a test. The screenshot gate is not optional here.
- **The bob cadence is unjudged.** 8 fps is a starting guess, not a measured choice.
- The two pure-horizontal facings are a designed compromise (D1). If walking due left or right reads
  wrong, the fix is art — a pure side view — not code.

---

## 6. Superseded: the walk set ships instead

**2026-08-12.** The six `walk_*.png` sheets replaced the idle sheets as the baked player art. §1a and
§D2 above are retained as a record of the idle sheets, not as a description of the game.

### What changed and why

§D2 chose a single shared cycle **because** §1a measured the idle art as a bob: `opaque_x` identical
across all 8 frames, both feet planted, no leg alternation. That reasoning does not transfer. The
walk sheets were measured the same way and are **a genuine stride**:

| | Idle sheets (retired) | Walk sheets (**shipped**) |
|---|---|---|
| Geometry | 384×64, pitch 48 | **identical** — the baker's dimension check took them unchanged |
| Unique frames | 41 of 48; frame 7 duplicates frame 0 in all six | **8 of 8 in every sheet** |
| Horizontal motion | none — `opaque_x` fixed per sheet | diagonals vary 1–2 px; cardinals still fixed |
| Leg separation | none | **2–3 scanlines split into two runs on frames 0/3/4/7**, closing to one run on 1/2/5/6 |
| Key-magenta hits | 0 | **0** — checked again, not assumed |
| Distinct colours | 19–25 | 20–27 |
| Baked records | 42 (6 × 7) | **48 (6 × 8)** |

Frames 0 and 4 are the two contact poses and 2/6 the passing poses — the classic eight-frame,
two-contact structure. **All eight frames must be baked**; there is no loop-closing duplicate to drop
the way there was for idle.

### The decisions that replaced D2

**D2′ — the frame index comes off `p->anim`, not the world clock.** `player_sprite_id` takes the
`Player` alone. `g->clock` is no longer passed in, so "the clock must not drive the character" is
enforced by the function signature rather than by a test.

**D5 — standing holds the last frame reached.** `update_player` advances `anim` only under movement
intent and **no longer resets it on release**. A standing player therefore holds whichever frame the
stride stopped on, and stepping off again resumes from that pose. The old `anim = 0` reset was right
for the idle art, whose frame 0 was a true rest pose; frame 0 of a stride is mid-step, so snapping to
it on release would read as a flinch. There is no idle state, no "is moving" flag and no second
sprite table — the behaviour falls out of not resetting a float.

A wrap keeps `anim` inside one cycle, so it cannot accumulate across a session and lose the
resolution the frame quantiser needs.

**Consequence, accepted deliberately: a standing character is completely static.** No breath, no
sway. At ~24 px this is a judgement call that needs a human, not a correctness one.

**D3′ — the idle sheets stay on disk, unbaked.** Same treatment `assets/player/` got when the old
four-way set was retired: unbaked art costs zero shipped bytes and keeps the decision reversible.
`$script:DreamCategories` is still `nature` only (D4 unchanged).

### Measured cost

| | |
|---|---|
| Walk records | 48 — 13,889 B data + 1,009 B palette + 768 B record = **15,666 B** |
| Idle records removed | 42 — 12,459 + 911 + 672 = **14,042 B** |
| Net const data | **+1,896 B** attributable to the character |
| Release binary | 1,085,440 → **1,091,584** (+6,144) |

The binary delta is larger than the const-data delta because a concurrently untracked
`assets/buildings/bld_house_small_v4_frame_0.png` is being auto-globbed by the `buildings` category
and baked at a cost of **4,285 B**, with no caller in `art_bld_small[]`/`art_bld_large[]`. That is
separate from this work and wants its own decision — wire it or move it out.

### `WALK_FPS` — the one number nobody can derive

**12.0, and it is unjudged.** Foot-lock is unreachable at this speed-to-size ratio: `PLAYER_SPEED` is
6.9 tiles/sec under a ~24 px character, so the feet skate at any cadence. The old four-frame set ran
8 fps = 2 cycles/sec; eight frames at 8 fps halves that to 1 and reads as slow motion. 12 gives 1.5
cycles/sec — 3 steps/sec against two contacts. **16 restores the old cadence** if 12 reads slow. One
`#define`, next to `PLAYER_SPEED`.

### Verification

`--fade-test` carries the pure-function contract: one cycle visits 8 distinct sprites, the quantiser
holds within a slot, the frame depends on `facing6` and `anim` alone, and the six facings map to six
distinct rows. Two controls — a snap-to-frame-0 variant (the reverted behaviour) caught on all 7
moving slots, and a four-way row collapse caught on exactly the 4 diagonals.

`--move-test` carries the sim-level half: `anim` survives 120 no-input steps unchanged, and the
sprite id with it.

> **A trap worth keeping.** The first version of the `--move-test` check read `anim` straight after
> the 8-direction sweep and **passed against a deliberately broken build**. 240 steps per direction
> is a whole multiple of the 40-step wrap cycle at `TICK_DT`, so the sweep leaves `anim` at exactly
> 0 — which is also the value the broken behaviour produces. Every assertion held vacuously. The fix
> is 7 extra input steps (coprime with the cycle) plus a live guard asserting the phase is non-zero
> before anything is concluded from it. This was caught only by breaking the fix on purpose and
> re-running, which is why that step is not optional.
