---
tags: [design, wayfarer, bugfix]
---

# Bug Fix Plan — codebase sweep, 2026-08-11

See [[Wayfarer MOC]] for the project hub and [[Agent Prompt]] for the verify-before-done loop this
plan follows. Written after a full read-only audit of `src/main.c` (split into three regions —
world-gen/data model, simulation+save+render, audio+selftest+main+build-scripts — audited in
parallel, then every finding re-verified line-by-line against the live file). **8 confirmed
issues**, from a real heap buffer overflow in the renderer to three places where the project's own
verification harness can't catch the fault it claims to check for, to two world-generation bugs
that quietly violate invariants the code's own comments assert.

Everything below lives in `src/main.c` (single translation unit). Rebuild/verify via `build.ps1` /
`build.ps1 -SelfTest` per `README.md`. **This file is a punch list for a future implementation
session** — hand it directly to whoever (human or agent) picks this up next.

## How to use this file

Pick a group, read its entries, make the changes, run the listed verification, log the session per
[[Agent Prompt]]'s devlog convention. Groups are independent of each other; entries *within* a
group should land together since they share one verification pass. Don't reorder within a group —
the sequencing notes explain why.

- **Group A** (do first — memory safety): #1
- **Group B** (independent, trivial to isolate, any order): #6, #7, #8
- **Group C** (do together, in order #2 → #3): audio self-test + the race it would otherwise mask
- **Group D** (do together, in order #4 → #5, one combined seed-sweep at the end): both touch
  world generation and need the same wide multi-seed regression pass

---

## #1 [HIGH — memory safety] Off-by-one overflow in `iso_diamond` / `iso_diamond_lr`

**Where:** `iso_diamond` (`src/main.c:4448-4459`), `iso_diamond_lr` (`4473-4485`), contrasted with
the correct `iso_tile` (`4163+`) and `vspan`'s documented contract (`4121-4142`, comment: "x is
clipped by the caller... only y is tested here"). See [[Isometric Rendering]].

**The bug:** both functions clip the right edge with `if (cx + i1 > fb->w) i1 = fb->w - cx;` then
loop `for (i = i0; i <= i1; i++)` — inclusive. `iso_tile` computes the *identical* clamp expression
but loops with strict `<` (exclusive) and is correct. Two consequences: (a) once clamped,
`cx+i1 == fb->w` is one past the last valid column (`fb->w-1`) but the inclusive loop still visits
it; (b) the guard is strict `>`, not `>=`, so a diamond whose right edge lands exactly on
`cx+rw == fb->w` isn't clamped at all. `vspan` performs no x-bounds check of its own, so this is a
real out-of-bounds write: `pixels + y*pitch + fb->w*4`. On any row but the last this corrupts pixel
`(0, y+1)` (silent scanline bleed); on the **last row** it writes past the end of the backbuffer's
`SDL_malloc`'d pixel buffer (`backbuffer_new`, `~6611`) — a genuine heap overflow.

**Trigger:** every tree/bush/prop contact shadow (`draw_tree` `~4915`, `draw_bush` `~4945`,
`draw_prop` `~5628`, all via `iso_diamond`) and every building roof slice (`draw_building`
`~5382`, `~5391`, via `iso_diamond_lr`) whose screen position lands at/near the right edge of the
960 px logical framebuffer — i.e. routine camera scrolling, not a crafted input.

**Fix:** match `iso_diamond`'s own sibling `iso_tile`'s convention exactly — widen the unclamped
default to `i1 = rw + 1` and change the loop to strict `<`:
```c
int i, i0 = -rw, i1 = rw + 1;      /* was: i1 = rw */
if (cx + i0 < 0)      i0 = -cx;
if (cx + i1 > fb->w)  i1 = fb->w - cx;
for (i = i0; i < i1; i++) {        /* was: i <= i1 */
```
Same two-line change in both `iso_diamond` and `iso_diamond_lr`. Unclamped column count is
unchanged (still `2*rw+1` columns), and the boundary case (`cx+rw==fb->w`) now clamps correctly.

**Verify:** no existing `--iso-test` case exercises `iso_diamond`/`iso_diamond_lr` (only `iso_tile`
is covered). Add a case to `iso_selftest` (`~7977`) in its existing canary-guard style: build a
small `fake_surface` with sentinel bytes past the end of its pixel buffer, call `iso_diamond` with
`cx` chosen so the diamond's right edge lands exactly at `fb->w` on the last row, assert the
sentinel is untouched. Confirm it fails pre-fix and passes post-fix, then run `--iso-test` and the
full suite for regression. Zero determinism/byte-budget impact — pure rendering bounds fix.

---

## #6 [LOW-MEDIUM — test harness gap] `gating_selftest` computes but never asserts `gated_seen`

**Where:** `gating_selftest` (`src/main.c:6987-7026`). See [[Abilities]]. Confirmed: `gated_seen`
(set at line 7018-7019 when tier-0 walk differs from full-ability reach) is only ever printed
(`7022-7024`), never folded into the `fails` this function returns, and the `--gating-test` CLI
dispatch never aggregates it either. The function's own comment states the missing bar directly:
*"at least one region must be out of reach on some seed, or gating is decorative and the test
proves nothing."* A regression that made ability gating a complete no-op would still print PASS.

**Fix:** thread it out via an out-parameter, mirroring the exact pattern `reach_selftest` already
uses for its `relaxed` counter:
```c
static int gating_selftest(Uint64 seed, int verbose, int *out_gated)
{
    ...
    if (gated_seen) (*out_gated)++;
    ...
    return fails;
}
```
At the `--gating-test` dispatch, thread a `gated` counter through the seed loop, print how many of
N seeds actually exercised gating, and add `if (!gated) bad++;` as the negative-control assertion
the comment already promised. Single call site — safe to change the signature.

**Verify:** temporarily stub the ability check to a no-op in a throwaway build, confirm
`--gating-test` now FAILs (proves the check has teeth), revert, then run `--gating-test --seeds 200`
normally and confirm PASS with `gated > 0`. Test-only change (selftest binary only), zero risk to
shipping code.

---

## #7 [LOW — input validation gap] `game_load` doesn't validate `abilities` against its legal bitmask

**Where:** `game_load` (`src/main.c:3478-3541`). See [[Save and UI]]. Confirmed: `restored`
(`3510-3512`) and `shards` (`3514-3516`) are both checked against their masks and rejected if any
out-of-range bit is set — matching the function's documented "every malformed input is rejected
before the live game is touched" contract. `abilities = buf[20];` (line 3513) has **no equivalent
check**, even though `shards` right below it does. Legal bits are
`ABIL_WADE|ABIL_CLIMB|ABIL_KINDLE = 0x07` (`~859-862`). Not a crash risk (collision code only ever
masks with `&`), but a hand-edited save can silently grant abilities never earned in play,
contradicting the function's own stated validation contract.

**Fix:** add the identical-style check immediately alongside the shards check:
```c
abilities = buf[20];
if (abilities & ~(Uint8)(ABIL_WADE | ABIL_CLIMB | ABIL_KINDLE))
    return -1;
shards = buf[21];
if ((Uint32)shards & ~shard_mask)
    return -1;
```

**Verify:** `save_selftest` (`~11268-11461`) has 4 negative controls (truncated, wrong version, bad
magic, out-of-bounds position) and none for a bad abilities/shards mask. Add a 5th control (bad
save with `buf[20] |= 0xF8`), confirm it's rejected with the game untouched. Run `--save-test`.
Zero risk to well-formed saves.

---

## #8 [LOW — cosmetic] Seed display truncates a loaded 64-bit seed to `int` (two sites)

**Where:** `hud_draw`'s `int seed` parameter formats via `"seed %d"` (`3987`); `main()`'s F9 (load)
handler does `seed = (int)ls;` (`11903`) where `ls` is the full `Uint64` `game_load` returns.
**Second site found during verification, not in the original audit:** the window title bar
(`12007`) has the identical bug from the same root cause (`main()`'s local `int seed`). `Game.seed`
itself stays full-fidelity internally (used correctly by every `tile_hash` call) — this is
display-only. Confirmed unreachable via the CLI (`--seed` is parsed with `SDL_atoi` into an `int`,
so it can't express an out-of-range value) — only reachable via a hand-edited save file's raw seed
bytes, same threat model as #7.

**Fix (optional relative to #1-#7, but cheap):** widen `main()`'s local `seed` to `Uint64` (root
cause), widen `hud_draw`'s parameter to `Uint64` and format via the codebase's existing
`%.0f`/`(double)` idiom already used elsewhere for `Uint64` seeds (avoids pulling in `%llu`/PRIu64
per [[Agent Prompt]]'s printf-bloat warning), and apply the same format change to the title-bar
string at line 12007. F9 becomes `seed = ls;` with no cast.

**Verify:** extend `hud_selftest` with a case asserting two seeds differing only above bit 31
render different seed-line pixels (fails pre-fix, passes post-fix). Run `--hud-test`. Zero
determinism impact.

---

## #2 [HIGH — false negative] `audio_selftest` never gates on its own printed invariants outside `--layers`

**Where:** `audio_selftest` (`src/main.c:11019-11191`). See [[Audio and Synth]]. Confirmed:
`partial writes`, `NaN`, and `out of range` (clipped) counts are computed and printed
`"(must be 0)"` (lines 11111, 11139-11140) but the only pass/fail gate in the whole function is
inside `if (layers) { ... if (!ok) return 1; }` (11143-11174). Every other invocation — including
the plain 440 Hz tone and `--noise`, both of which `README.md:95-96` documents as real usage
(`--audio-test 600 --noise --seed 42 ...`, `--audio-test 3000 --rate 44100`) — falls through to an
unconditional `return 0;` at line 11190 regardless of these values. [[Agent Prompt]] calls
audio-callback faults (crashes, NaN, clipping, partial writes) release blockers; this test can't
catch any of them outside `--layers`.

**Fix:** aggregate a `fails` counter across *every* invocation, matching the `fails`-counter idiom
other selftests already use:
```c
int fails = 0;
...
if (a.partial_len) fails++;
...
if (nan) fails++;
if (clipped) fails++;
if (layers) {
    ... /* keep existing ok/det checks */
    if (!ok) fails++;
    if (!det) fails++;
}
...
printf("\n%s (%d checks failed)\n", fails ? "FAIL" : "PASS", fails);
SDL_free(a.cap);
SDL_Quit();
return fails ? 1 : 0;
```
Keep the `--dump` write happening before the final return regardless of `fails`, so a failing
capture can still be inspected offline.

**Verify:** run `--audio-test 3000`, `--audio-test 3000 --noise`, `--audio-test 3000 --rate 44100`,
`--audio-test 3000 --layers`, `--audio-test 3000 --sfx` and confirm exit 0 with everything at 0
(no regression). To prove the gate has teeth, temporarily push an `SFX_CFG`/`TONE_AMP` amplitude
above 1.0 in a throwaway local build, confirm `--audio-test` now reports a nonzero clipped count
**and** a nonzero exit code, then revert. Selftest-only change, no shipping-code impact.

---

## #3 [MEDIUM — data race / UB] `audio.rng` written from the main thread without synchronization

**Where:** confirmed three write sites at `src/main.c:11710` (init, before
`SDL_PauseAudioDevice(dev, 0)` — safe, still single-threaded), `11890` (`R` key handler) and `11904`
(`F9` load handler) — both **after** the device is unpaused and the callback thread is live. The
callback thread concurrently reads/mutates the same `Rng` via `rng_bipolar(&a->rng)` in
`wave_sample`'s `W_NOISE` case (`~596`, used by the in-game `SFX_PORTAL`) and the `--noise` test
branch in `audio_cb` (`~727`). The `Audio` struct's own doc comment states the invariant this
violates: the main thread may only bump atomics; every other byte is callback-owned.
`layer_fire`/`voice_fire`/`reset_req` already follow that rule correctly via `SDL_Atomic*` — this
is the one place `audio.rng` doesn't.

**Fix:** extend the existing `reset_req` request/latch pattern rather than inventing new
synchronization:
1. Add `SDL_atomic_t rng_req;` and `Uint64 rng_seed_req;` to `Audio`, near `reset_req`.
2. In `synth_latch` (`~608-635`), alongside the existing `reset_req` handling, add:
   ```c
   if (SDL_AtomicGet(&a->rng_req)) {
       SDL_AtomicSet(&a->rng_req, 0);
       rng_seed(&a->rng, a->rng_seed_req, STREAM_AUDIO);
   }
   ```
3. Replace both unsafe main-thread writes (`11890`, `11904`) with a payload-then-flag handoff:
   ```c
   audio.rng_seed_req = seed;   /* or `ls` at the F9 site */
   SDL_AtomicSet(&audio.rng_req, 1);
   ```
   This reuses `rng_seed(..., STREAM_AUDIO)` — the same derivation `rngs_init` already uses — so
   there's one source of truth for the audio RNG stream, no duplicated logic. Leave the `11710`
   init-time write untouched (predates the callback thread's existence).

**Verify:** primarily regression — the full `--audio-test` sweep (all flag combinations) must
still pass unchanged, since a fixed seed's audio output is unaffected by *when* the reseed lands
relative to a callback boundary. As a best-effort stress check, open a real audio device with
`--sfx`/`--noise` and hammer the R/F9 reseed path from the main thread for several seconds while
watching the (now-real, per #2) `partial_len`/NaN/clipped counters and confirming no crash/hang. If
ThreadSanitizer is available in the MinGW toolchain, a `-fsanitize=thread` run is the strongest
available proof — note explicitly if it isn't available rather than claiming a check that wasn't run.

**Sequencing:** land #2 before #3 — #2's fix is what makes a real fault from this race actually
visible in `--audio-test`'s exit code, so verifying #3 without #2 already in place proves nothing.
One combined verification pass after both land.

---

## #4 [MEDIUM — generation defect] Aetherhold watchtower at wrong coordinates, unprotected, and duplicated in two places

**Where:** `castle_apply_layout` (`src/main.c:1388-1409`, confirmed). See
[[Phase 13 - Aetherhold Castle]] and [[World Generation]]. The gate is at
`gx = CASTLE_CAUSEWAY_X0 - 2 = 80`; the watchtower comment says "6 tiles west of gate" but the code
hardcodes `wx = 88, wy = 30` — 8 tiles *east*, landing inside the causeway's own x-span, not 6 tiles
west on the approach shelf. Traced `is_castle_reserved(88,30)` by hand through
`castle_island_tile`/`castle_approach_tile`/`castle_causeway_tile` (`115-188`): it evaluates
**false** (`castle_approach_tile` requires `tx<=82`). `place_rivers`'s BFS/carve step only skips
`is_castle_reserved` tiles (never checks `w->solid`), so a river can legally route through and
silently overwrite the hand-placed tower on some seeds — contradicting `castle_apply_layout`'s own
comment that hand-crafted features must never be overwritten by procedural generation.

**Second site, confirmed, not in the original audit:** the render dispatch at `~6067` and `~6073`
has an *independent* hardcoded copy of the same coordinate (`tx == 88 && ty == 30`, and
`(tx == 88) ? ART_AETHER_BLD_TOWER_ROUND_RUINED : ...`). Fixing only the generation site would leave
the sprite drawn at the old, now-unprotected tile while the actual structure moves — a visibly
broken landmark. **Both sites must move together.**

**Fix:** introduce named constants near the other `CASTLE_*` `#define`s (`83-100`) as the single
source of truth — the root cause is two independently hand-copied literals that fell out of sync:
```c
#define CASTLE_WATCHTOWER_X (CASTLE_CAUSEWAY_X0 - 2 - 6)   /* 6 tiles west of the gate: 74 */
#define CASTLE_WATCHTOWER_Y (43 - (CASTLE_WATCHTOWER_X - 58) / 3 + CASTLE_Y_SHIFT)  /* 33 */
```
(Confirmed by hand: evaluates to `(74, 33)`, and `castle_approach_tile(74, 33)` returns true via
its path-shelf branch, so the corrected location falls inside `is_castle_reserved` without touching
that predicate.) Then:
- `1399`: `int wx = CASTLE_WATCHTOWER_X, wy = CASTLE_WATCHTOWER_Y, dx, dy;`
- `~6067`, `~6073`: replace both `tx == 88 && ty == 30` / `tx == 88` literals with
  `tx == CASTLE_WATCHTOWER_X && ty == CASTLE_WATCHTOWER_Y` / `tx == CASTLE_WATCHTOWER_X`.
- Update the now-accurate comment at `1397`.

**Verify:** no existing test checks watchtower integrity. Add a check to `land_selftest`
(`~9956-9970`, which already runs full generation per seed) asserting the 3×3 block centered on
`(CASTLE_WATCHTOWER_X, CASTLE_WATCHTOWER_Y)` is solid/`SURF_ROCK` except the hollow `SURF_LAND`
center, and never `SURF_RIVER`. Run `--land-test --seeds 500` (above the default 20, to raise the
odds a river actually threatens the old coordinate) before and after — should be able to find a
failing seed pre-fix, 0/500 post-fix.

**Risk — flag explicitly:** this moves the watchtower for **every** seed (it's hand-placed, not
seed-derived) — `(88,30)` reverts to ordinary procedural terrain, `(74,33)` now carries the tower.
Intended and correct, but a visible, seed-independent world content change worth being aware of
going in, not just a silent bytes-in/bytes-out patch.

---

## #5 [MEDIUM — generation defect] Ability-grant entities not restricted to the overworld sector

**Where:** `place_entities` (`src/main.c:2281-2361`, confirmed exactly). See [[Abilities]] and
[[World Generation]]. The grant loop (`2295-2308`) carries a comment stating the three ability
grants "stay ... in the OVERWORLD," and a second, independent comment at line 893 says the same
thing (`DREAM_FRAGMENTS ... never the 3 ability grants`). But unlike the entity loop right below it
(`2310-2360`), which explicitly computes `over_mask`/`dream_mask` via `regions_by_sector` and
intersects `reach & (want_dream ? dream_mask : over_mask)`, the grant loop uses `reach` unfiltered
and calls `pick_tile_in_region(w, r, rng, -1)` — sector `-1`, "anywhere." `regions_by_sector` isn't
even called until after the grant loop finishes. Since some regions are exempt from ability gating
(depth ≤1, and the dream "arrival" region, always) and the portal is an ordinary region-graph edge,
a grant can legally land in the dream sector on some seeds — and neither `world_solvable` nor
`entities_split_ok` checks grant placement by sector, so nothing in the generate-then-verify retry
loop rejects it. A mis-placed grant would also silently count toward the `DREAM_FRAGMENTS` quota in
`entities_split_ok` (it's an ordinary `ents[i]` with `is_soul==0`), potentially masking a seed that
actually needed a real fragment placed in the dream sector.

**Fix:** hoist the `regions_by_sector` call above both loops, and filter the grant loop the same
way the loop below it already does, using the identical two-tier fallback:
```c
static void place_entities(World *w, Rng *rng, Entity *ents)
{
    static const Uint8 grant_order[3] = { ABIL_WADE, ABIL_CLIMB, ABIL_KINDLE };
    Uint8 held = 0;
    Uint32 over_mask, dream_mask;
    int i;

    regions_by_sector(w, &over_mask, &dream_mask);   /* moved up */
    ...
    for (i = 0; i < 3; i++) {
        Uint32 reach = regions_reachable(w, held);
        int r = pick_region(reach & over_mask, w->region_count, rng);
        if (r < 0)
            r = pick_region(reach, w->region_count, rng);   /* same fallback tier as below */
        if (r < 0)
            break;
        ents[i].region = (Uint8)r;
        ents[i].tile = pick_tile_in_region(w, r, rng, 0);   /* 0 = overworld */
        if (ents[i].tile < 0)
            ents[i].tile = pick_tile_in_region(w, r, rng, -1); /* same straddling-region fallback */
        ents[i].grants = grant_order[i];
        held |= grant_order[i];
    }
    {
        Uint32 reach = regions_reachable(w, held);
        /* regions_by_sector call removed here — already computed above */
        for (i = 3; i < ENTITY_COUNT; i++) { ... unchanged ... }
    }
}
```
This can't introduce a new hard-failure mode: the spawn region is always in `over_mask` and always
reachable at `held==0` (asserted separately by `sector_selftest`), so `reach & over_mask` is
non-empty at `i==0` on every world. If a future degenerate world ever made it empty anyway, the
existing `r<0 → break` path leaves that grant unplaced, which `world_solvable` already correctly
rejects, routing into `world_place_and_verify`'s existing 64-attempt retry — no new mechanism.

**Verify:** extend `reach_selftest` (`~7504+`, which already iterates `g.ents[]` and already has
`x`/`y` computed per entity) with, inside that loop:
```c
if (g.ents[i].grants && dream_sector(y)) {
    fails++;   /* + a printed diagnostic, matching this function's existing style */
}
```
Run `--reach-test --seeds 500` before the fix (should be able to find a failing seed — proves the
check has teeth) and after (0/500 fail).

**Risk / sequencing — flag explicitly:** this changes `place_entities`'s RNG draw sequence for any
seed where the old code would have rolled a grant into the dream sector, which can shift
`gen_attempts` and interacts with the `entities_split_ok` dream-quota count noted above. **Land #4
before #5** (it's the more mechanical, narrowly-scoped fix), then #5, then run the full
generation-related battery once: `--land-test`, `--reach-test`, `--region-test`, `--sector-test`,
all at `--seeds 200-500`. Don't sweep seeds twice for what is effectively one "world-gen changed"
verification event.

---

## Verification summary (after all groups land)

1. `.\build.ps1 -SelfTest` — must build with zero warnings (`-Wall -Wextra`, per [[Agent Prompt]]).
2. Full existing suite (`README.md`, `Handover.md` §4's command list) — every `--*-test` flag, to
   confirm nothing regresses.
3. Wide seed sweeps specifically for Group D: `--land-test --seeds 500`, `--reach-test --seeds 500`,
   `--region-test --seeds 200`, `--sector-test --seeds 200`.
4. Group C: `--audio-test` across `{plain, --noise, --rate 44100, --layers, --sfx}`, plus the
   throwaway-amplitude-spike manual check described under #2 to prove the new gate has teeth.
5. `.\build.ps1` (shipping build) — confirm final byte size stays under the 1,440,000 target and
   note the delta (none of these fixes are expected to cost meaningfully more than a handful of
   bytes; #1/#6/#7/#8's new self-test cases live in the selftest-only binary and cost the shipping
   build nothing).
6. Log the session per [[Agent Prompt]]'s devlog convention (`devlog/YYYY-MM-DD-session-NN.md`,
   update `devlog/INDEX.md`), since that's this project's standing convention for any verified
   change — plan → implement → build → measure → verify → report.
