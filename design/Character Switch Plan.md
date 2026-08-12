---
tags: [design, wayfarer, art, character]
date: 2026-08-12
status: planned
---

# Character Switch Plan — the new player art

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
