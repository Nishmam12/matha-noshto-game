# Wayfarer — Progression Handover

> For whoever picks up progression work on this branch next. Written 2026-09-04, against
> `new-update` at commit `410c660` ("feat: the Dungeon (Area 4), the king, and god mode").
> Supersedes `PROGRESSION_PLAN.md`, which is kept in place as the historical record of a
> redesign that was proposed and then not built — see that file's own text for why it was
> written, and the "Status" section below for why it no longer applies.

## Status

`PROGRESSION_PLAN.md` proposed a 3-area redesign: return portals, a partial-memory threshold to
open the first gate, and a `castle_key` that gated the Underworld/castle. None of it landed. The
branch instead kept the game's original one-way, full-completion gate and extended it forward to
a 4th, terminal area — the Dungeon, with the king, the beast ending, and the god-mode cheat.
`try_use_portal` carries the reasoning in its own comment (main.c:8302-8306):

> "One-way by design (no return portal from a later area to an earlier one): the design is
> symmetric enough that a return trip would be nearly free to add later... but it is a second
> interactive object, HUD affordance and test surface the source plan does not ask for."

That is a decision, not a lapse — treat return travel, partial thresholds, and a `castle_key`
gate as closed, not as a backlog. Two things the old plan worried about turned out to already be
fine under the design that actually shipped:

- **Save/load** already extends cleanly to Area 4 with no `SAVE_VERSION` bump (`SAVE_VERSION`
  stays 4, `SAVE_SIZE` stays 48) — `save_header_ok` (main.c:6103-6194) widened instead.
- **The end card can't lie.** The old plan worried a single fixed `END_CARD` could describe a
  completeness she hadn't earned, if gates ever opened on a partial threshold. Since every gate
  still requires full completion, that scenario can't occur — the fixed card
  (main.c:8164-8169) is honest by construction, not by any variant-selection logic.

The narrative loop itself — wake with nothing, Mainland, Lumiara, the Underworld/castle, the
Dungeon, the king, the beast, the ending — is complete and playable start to finish. It is
covered by 20 self-test suites plus the size gate (`tools/run-tests.ps1`), and the shipping
build sits at 877,056 of the 1,440,000-byte cap — about 563 KB of headroom. This handover is not
a feature backlog. It records one small, real gap, and closes two questions a first pass over
the code raised but which turned out, on closer reading, not to be gaps at all.

## Current topology

| Area | Biome | Portal leads to | Gate condition | Anchor |
|---|---|---|---|---|
| 1 Mainland | Forest | Area 2 | `area_complete(g)`: 7 memories + 3 souls | main.c:8347-8354 |
| 2 Lumiara | Lumiara | Area 3 | `area_complete(g)` of Area 2 | main.c:8355-8362 |
| 3 Underworld/castle | Underworld | Area 4 | `area_complete(g)` of Area 3 | main.c:8363-8371 |
| 4 Dungeon | Dungeon | — terminal | `story_area_done(g,3)` gates the king instead | main.c:7847-7862 |

`castle_key` and `castle_state` (main.c:5768-5792) are real, still-live derived predicates — but
they only drive one dialogue line and a "the castle is stirring" toast, not a second gate on top
of the Area 2→3 portal. Every transition in the table above is gated the same single way: the
area being left has to be `area_complete`. God mode (main.c:8336) suspends exactly that one
clause and nothing else; it's already documented accurately in `CLAUDE.md` and needs no change.

## The one open gap: silent gate refusal

Pressing E while standing in reach of a portal that isn't open yet does nothing at all — no
sound, no toast, nothing on screen. That's inconsistent with every other refusal in the game:
walking into terrain she lacks the ability for fires `SFX_DENY` through `gate_report`
(main.c:17216), and the king, the newest gate in the game, already says "the chamber is not
finished with you yet" and fires `SFX_DENY` when she reaches him too early
(`try_king_audience`, main.c:7847-7862, the refusal at 7851-7855). The three portals never got
the equivalent treatment.

**Where:** `try_use_portal`, main.c:8326-8379. Right now the check order is:

1. area/portal-tile guard (8331-8332)
2. `if (!area_complete(g) && !god.on) return 0;` (8336-8337) — **silent exit happens here**
3. reach/distance check (8338-8343)

**Fix:** swap steps 2 and 3, so reach is established before completion is checked, then give
step 2 a voice:

```c
/* reach check moves up here, unchanged in content */
ex = (float)(g->w.portal_tile % WORLD_W) * TILE + TILE * 0.5f;
ey = (float)(g->w.portal_tile / WORLD_W) * TILE + TILE * 0.5f;
dx = ex - g->p.x;
dy = ey - g->p.y;
if (dx * dx + dy * dy > INTERACT_RADIUS * INTERACT_RADIUS)
    return 0;

if (!area_complete(g) && !god.on) {
    sfx_fire(a, SFX_DENY);
    hud_toast("the way is not open yet");
    return 0;
}
```

The reorder matters, not just the toast: adding the toast at the *current* check location would
fire it on every E press anywhere in the level while the area is unfinished, not only near the
gate. Reach has to be established first, the same way `try_king_audience` already does it.

**Keep the `0` return on refusal** — do not give this the king's "return 1 on a refusal, it was
still handled" shape. Three existing call sites depend on `try_use_portal` returning falsy
specifically when she's in reach but refused:

- main.c:15587 — the god-mode negative control places her exactly on the portal tile (distance
  0, trivially in reach) of an unfinished area and asserts `try_use_portal` returns 0. Verified
  this still holds after the reorder: reach passes trivially, the completion check still returns
  0, just with a toast/SFX side effect the test doesn't inspect.
- main.c:13124, 14009 — `--story-test` portal assertions on the same contract.

No new save field, no new subsystem, no `SAVE_VERSION` bump — `sfx_fire` and `hud_toast` already
exist and are used exactly this way elsewhere.

**Test plan** (extend `--story-test`, near the existing castle-state subtest at
main.c:12947-12977 or the portal assertions at 13124/14009):

- Positive control: stand in reach of an unfinished area's portal, press-equivalent call to
  `try_use_portal`, assert the toast and `SFX_DENY` both fired.
- Negative control: same unfinished state, positioned outside `INTERACT_RADIUS` — assert neither
  fires.
- Re-run the god-mode negative control (main.c:15575-15610) and confirm it still passes
  unchanged.

**Screenshot check** (per this repo's look-at-the-screen rule): `--restored 0 --shot` standing
on an unfinished area's portal tile, to see the new toast rendered.

## Closed questions — considered, not left open

**NPC cross-gating** (an idea from the discarded plan: a person shouldn't get ahead of the
person before them in conversation). Declined, not unbuilt. `CLAUDE.md`'s "The cast" section
already frames the existing memory-count-only gate as the complete design: *"Four lines each,
one per press of E, gated on memories found... That one rule is the whole 'controlled sequence'
— no quest graph, and it cannot deadlock."* Adding an ordering rule between NPCs would be adding
something this project's own documentation says was deliberately left out, not finishing
something unfinished. No code change proposed.

**Castle-state announcement scope.** A first read of `story_tick` (main.c:8195-8219) looks like
it announces "the castle is stirring" from anywhere, with no check that she's in Area 1 or near
the gate — `if (cs > (int)story.castle_seen)` and nothing else. That reads like a gap. It isn't
one: `castle_state` (main.c:5768-5772) is a pure function of `story_area_frags(g, 1)` — Area 1's
own memory count — and that count is frozen the instant Area 1 is left, because leaving requires
`area_complete`, i.e. all 7 memories already found. So `castle_state` cannot produce a value
higher than `story.castle_seen` while `g->area != 1`; the transition that would need guarding
against literally cannot happen once she's moved on. Adding a proximity/area guard here would be
validating a scenario the mask already makes impossible — against this project's own stated
rule not to add checks for things that can't happen. It would also make the moment worse, not
better: there's no gate-art payoff to walk back and discover (gate art keyed to progression state
was tried once and reverted — see `CLAUDE.md`'s "Core Invariants" section on why the two portal
styles ran backwards for a while), so gating the toast behind a walk to the portal would just add
friction to telling her something the moment it becomes true. No code change proposed.

## Verification

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1     # 20 suites + the size gate
```

Then, once the one open gap above is implemented: `--restored 0 --shot` standing on an
unfinished portal, to see the new refusal toast on screen.

## Out of scope (carried forward from the discarded plan, trimmed to what's still true)

Return travel, partial-threshold gating, gate art keyed to progression state, NPC cross-gating,
the corner minimap (already reveal-gated per-marker; not one of the gaps identified here),
dialogue/memory text, the art bake, the audio callback, world generation, `tile_blocked`, the
menu, the save file's layout.
