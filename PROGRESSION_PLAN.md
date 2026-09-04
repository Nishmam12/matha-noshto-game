# Wayfarer — Progression Overhaul Plan

> **Superseded.** None of the phases below landed. The branch instead kept the original
> one-way / full-completion gate and extended it to a 4th area (the Dungeon) — `try_use_portal`
> (main.c:8302-8306) explicitly rejects this document's return-travel premise as unwanted scope.
> Kept here as the historical record of why the redesign below was proposed. For current status
> and the one real gap still open, see `HANDOVER.md`.

> Carry-over document. Written for whoever picks this branch up next, on any machine.
> Status when written: planned, not yet implemented. Branch `npc-addition`.

## Context

The brief asks for a centralised progression system delivering the existing story. An audit of
`src/main.c` shows **most of it already exists** and should not be rebuilt: the derived progression
predicates (`story_area_frags` / `story_area_souls` / `castle_state` / `castle_key` /
`story_area_done`, main.c:5155-5217), the star guidance with its special targets (`star_target`,
main.c:7334), the nine one-shot soul events (`story_event_begin`, main.c:6950), NPC dialogue and
disappearance (`story_talk` / `entity_npc_art`), the map fragment (`try_take_map`, `World.map_tile`),
the King/beast timeline (`story_end_tick`) and save v4 carrying `talk[]`, flags, `maps` and the
30-bit restored mask.

`--story-test`, `--npc-test`, `--save-test` and `--map-test` already cover those with negative
controls. So this is **not** a rewrite. It closes the specific places where the gameplay does not yet
deliver the story, plus the topology change chosen for this pass:

1. The corner minimap draws from frame one — the brief requires starting without a map.
2. The gate needs 100% of an area, and travel is one-way, so the world reads as a checklist.
3. `castle_state` only fires a toast, from anywhere, and nothing about the castle ever looks
   different.
4. Dialogue is gated uniformly, with no NPC-to-NPC strand.
5. Pressing E on a closed gate does nothing at all — no refusal, no feedback.

**Decisions taken** (do not re-litigate without the owner): portal opens at **4 Mainland memories +
all 3 souls**; **return portals**; the Mainland gate's **art changes with castle state**;
**lightweight NPC cross-gating**.

Budget: shipping build is 837,120 of 1,440,000 bytes — ample headroom, but stay inside the existing
architecture (no new subsystems, no new art, no new save fields).

---

## New world topology

The single change that makes the brief's loop fit the three existing areas, with **no new placement
pass and no new art**:

| Gate | Leads to | Condition |
|---|---|---|
| Area 1 (Mainland) `portal_tile` | Area 2 | `portal_open(g)` — 4 memories + 3 souls in Area 1 |
| Area 1 `portal_tile` | Area 3 (castle) | `castle_key(g)` — Area 2 finished; takes priority |
| Area 2 (Lumiara) `portal_tile` | Area 1 | always — it is the way back |
| Area 3 `portal_tile` | — | the final chamber; Area 3 stays terminal |

This is the brief's §25/§26 flow (key from the mystical realm → return to the Mainland → the castle
recognises her) and it reuses `world_place_portal` untouched.

**Assumption to confirm in play: Area 3 remains terminal.** It has one special tile and it is the
King's chamber; entering the castle is the last step, announced by the existing
`say_open("the gate is already open." ...)` line.

---

## Phase 0 — Write this plan into the repo

This file. Kept at the repo root beside `LUMIARA_BIOME_PLAN.md` and `UNDERWORLD_BIOME_PLAN.md`, so
the plan travels with the code and can be amended as the work lands.

## Phase 1 — Central progression predicates

`src/main.c`, beside the existing derived block at main.c:5144-5217. Keep the file's rule:
**derive, never store.**

- Name the magic numbers: `PORTAL_MEMORY_THRESHOLD 4`, `CASTLE_DISTURBED_MEMORIES 4`,
  `CASTLE_REVEALED_MEMORIES FRAGMENT_COUNT`, `CASTLE_APPROACH_TILES 10`. `castle_state` and the new
  gate predicate read these instead of literals.
- Add mask-level primitives `mask_area_frags(Uint32 mask, Uint8 area)` / `mask_area_souls(...)`, and
  refactor `story_area_frags` / `story_area_souls` to call them. This is what lets the **save header
  check and the live gate share one definition** (Phase 6) instead of restating the rule.
- Add `portal_open(const Game *g)` — `story_area_frags(g,1) >= PORTAL_MEMORY_THRESHOLD &&
  story_area_souls(g,1) >= SOUL_COUNT` for Area 1; Area 2 always true; Area 3 false.
- Add `portal_dest(const Game *g)` — the table above, in one function, so the render, the prompt, the
  key handler and the tests cannot disagree (the same rule that makes `entity_npc_art` the one answer
  to "what is standing here").
- `area_complete` stays as-is: it now means "everything here is restored", used by the end card and
  the Area 3 banner, no longer by the gate.

## Phase 2 — Return travel

- Factor the replay loop out of `game_load` (main.c:5731-5735) into `game_replay_area(Game *g)`: for
  each bit of this area's slice of `g->restored`, call `game_restore`. That already re-grants
  abilities, re-lights regions and rebuilds `frags_restored` / `souls_restored`.
- `game_load` calls it — behaviour unchanged, same construction path.
- `game_transition_to_area` (main.c:7632) calls it after `game_init_area`, plus
  `if (g->maps & bit) g->w.map_tile = -1;` — the map she already found in that area must not be lying
  there again. Snap `regions[].restoration` to `restore_to` as the load does, so a re-entered area is
  not re-fogged mid-walk.
- `try_use_portal` (main.c:7651) becomes: reach test → `portal_dest` → transition →
  `audio_request_reset` with the destination biome. Refusal path (Phase 5) when there is no
  destination.
- **Risk to verify, not assume:** re-entering Area 1 must restore her abilities from the replayed
  mask, or gated terrain becomes impassable. `--save-test`'s byte-20 ability checksum is the existing
  tripwire; the new `--story-test` round-trip in Phase 9 asserts it directly.

## Phase 3 — Map gating (§9, §10)

- Gate the corner minimap in `mm_draw` (main.c:6039) on `game_has_map(g)` — return early before
  allocating. The M-key screen is already gated; the fog/reveal system is reused untouched, so no
  second exploration system appears.
- On a fresh game set `story.star_left = STAR_TICKS` in `game_init` so the first memory is findable
  without a map. `star_target` with 0 memories already returns the nearest one.
- `hud_selftest` sets `g->maps` before calling `mm_draw`, and gains a **negative control**: with
  `maps == 0` the minimap box must be empty.

## Phase 4 — Castle states made visible (§21-24)

- `story_tick` (main.c:7540): only advance `story.castle_seen` and announce when `g->area == 1` and
  she is within `CASTLE_APPROACH_TILES` of `g->w.portal_tile`. The state itself stays derived and
  immediate; only the *telling* waits for her to come back and look.
- `render_world` (main.c:4610-4626): pick the gate's art and brightness from `portal_dest` /
  `castle_state` rather than from the biome alone —
  - no destination yet → still `ART_LUM_PORTAL`, drawn at the tile's own fog level;
  - castle stirring (state 1, then 2) → the same sprite drawn at a raised fog level (the existing
    `level` argument of `draw_list_push`, so it brightens through `fogpal` with no new primitive);
  - castle key held → the animated `ART_UW_PORTAL_A..F` mouth, already the "leads somewhere deeper"
    sprite.

  This keeps the file's stated rule — *a gate is drawn as the place it leads to* — and adds no assets.

## Phase 5 — Gate refusal feedback (§24, §39)

`try_use_portal` returns 1 having said something whenever she presses E on a gate with no
destination: `SFX_DENY` plus a toast that names no numbers (e.g. *the way between is still closed*;
for the castle mouth before the key, *the gate does not know you yet*). Ordering in the key handler
(main.c:15082) is unchanged. Any new string must pass the `--story-test` font sweep — no `(`, `)`,
`%`, `"` and the rest of the all-zero glyphs.

## Phase 6 — Save/load correctness (§31, §38)

`save_header_ok` (main.c:5505-5567) currently encodes the *old* invariant — "you cannot be in Area N
unless every earlier area is 100%" — in four places: mask, `maps`, `talk[]` and the story flags.
Under return travel and a partial threshold these would **reject legitimate saves** (back in Area 1
with Area 2 progress). Rewrite them against what the gates actually require, using the Phase 1 mask
primitives so the file check and the live gate cannot drift:

- Area 2 bits / Area 2 map / Area 2 conversations ⇒ Area 1 met `PORTAL_MEMORY_THRESHOLD` + all souls.
- Area 3 bits / Area 3 map / Area 3 conversations ⇒ Area 2 fully restored (the key).
- `SF_KING` / `SF_BEAST` ⇒ `area == 3`; `SF_BEAST` ⇒ `SF_KING`. Unchanged.

**No `SAVE_VERSION` bump**: the layout does not change and the new rules are strictly looser, so
every existing v4 save still loads. Say so in the comment, since the file's convention is that a bump
is a deliberate cost.

## Phase 7 — NPC cross-gating (§16)

In `story_talk` (main.c:7018), keep the memory gate and add one rule: within an area, a person may
not get **ahead of the person before them** — `story.talk[k] <= story.talk[k-1]` for the 2nd and 3rd
of each area's three. A person who is not ready still repeats their last line (silence reads as a
broken key). No dialogue tree, no new state, nothing new saved.

Deadlock-freedom is not asserted, it is proved: `--story-test`'s existing per-seed simulation
(main.c:11480-11500) sweeps every seed and every area for "every soul comes free within the memories
its area holds". Extend that sweep to converge under the chain and keep the negative control.

## Phase 8 — End card honesty

With optional memories now leavable behind, `END_CARD` (main.c:7492) can state something untrue.
Select between two four-line variants on whether `game_live_mask` is complete. Both variants go in
the file-scope table so the font sweep reaches them — that array was made file-scope precisely
because a local one shipped four holes.

## Phase 9 — Tests

Extend, do not add suites. **Every new check carries a negative control**, per the project rule.

- `--story-test`: the gate opens at exactly the threshold and not before; `portal_dest` returns the
  castle once the key is held and Area 2 before it; a full round trip 1→2→1 preserves the mask,
  abilities, `maps`, `talk[]` and NPC disappearance; the castle announcement fires only near the gate.
- `--save-test`: save/load either side of each of the brief's §37 Test 7 events — memory, soul,
  disappearance, portal opening, castle step, key, castle entry, King, beast — plus rejection
  controls for the *new* impossible combinations (Area 3 progress without the key).
- `--hud-test`: minimap hidden without the fragment, shown with it (negative control above).
- `--map-test`, `--npc-test`, `--play-test`, `--reach-test`, `--gating-test` must stay green
  untouched — they are what proves collision, placement and solvability did not move.

---

## Verification

Run from the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1     # all 20 suites + the size gate
```

Zero warnings is enforced by `-Werror`; the size gate asserts `build\wayfarer.exe` < 1,440,000 bytes.

Then **look at the screen** — this project's rule, and how the moon-square and panel-collision bugs
were found after the tests passed:

```powershell
.\build\wayfarer-selftest.exe --frames 1 --shot    # fresh game: no minimap, star lit
.\build\wayfarer-selftest.exe --map --shot         # map screen after the fragment
.\build\wayfarer-selftest.exe --restored 4 --shot  # castle stirring: the gate is brighter
.\build\wayfarer-selftest.exe --area2 --lit --shot # the return gate
.\build\wayfarer-selftest.exe --endbeat 11 --shot  # the beast
```

Finally a **clean-save playthrough** of the brief's §37 flows: wake with no map → first memory → star
→ map fragment → minimap appears → talk the cast down the chain → souls and their events → gate opens
at the threshold, and refuses before it → Lumiara → key → **return to the Mainland** → the gate is now
the castle mouth → castle → King → beast → cliffhanger, saving and reloading at each step.

## Out of scope

Not touched, deliberately: dialogue and memory text (`CAST`, `MEMORY`, `END_SAY`, `SEV_SAY`), the art
bake, the audio callback, world generation, `tile_blocked`, the menu, and the save file's *layout*.
