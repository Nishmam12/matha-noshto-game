# Tier 1 Remediation — Items 3 & 4

**Item 3 — ERR-1**: the pathological-seed path stops producing a silently unwinnable world
**Item 4 — SEC-1**: `draw_sprite_ex()` becomes self-limiting, so the decoder's correctness no longer
lives only in the binary nobody ships

*Working tree on branch `castle-fixed`, `src/main.c` at **14,114 lines** with Tier 1 items 1–2
applied. Line numbers below are from that tree; every edit is also anchored on exact source text so
it survives drift. Companion to [production-gap-analysis.md](production-gap-analysis.md) §2 (ERR-1,
ERR-3) and §1 (SEC-1).*

> **Tree state, read this first.** `git status` on this working tree reports `M src/main.c`,
> `?? docs/`, `?? tools/run-tests.ps1`, `?? CLAUDE.md`, and `HEAD` is still `3d14508`. Items 1–2 are
> **in the working tree but not in a commit**. Commit them before starting this change, or the byte
> delta measured below will be the delta for items 1+2+3+4 together and unattributable — the exact
> failure mode §A.7 of the previous spec warned about.

---

## Contents

- [Part A — ERR-1: pathological-seed handling](#part-a--err-1-pathological-seed-handling)
  - [A.0 Design constraints](#a0-design-constraints)
  - [A.1 Step 1 — the retry loop in `game_init`](#a1-step-1--the-retry-loop-in-game_init)
  - [A.2 Step 2 — the degenerate fallback, made honest](#a2-step-2--the-degenerate-fallback-made-honest)
  - [A.3 Step 3 — resync the caller's seed](#a3-step-3--resync-the-callers-seed)
  - [A.4 What the retry does *not* touch](#a4-what-the-retry-does-not-touch)
  - [A.5 Step 4 — `--genfail-test` with a three-sided negative control](#a5-step-4--genfail-test-with-a-three-sided-negative-control)
  - [A.6 Byte cost and stack/memory invariants](#a6-byte-cost-and-stackmemory-invariants)
  - [A.7 Verification](#a7-verification)
- [Part B — SEC-1: in-decoder RLE bounds clamps](#part-b--sec-1-in-decoder-rle-bounds-clamps)
  - [B.0 Two corrections to the gap analysis's sketch](#b0-two-corrections-to-the-gap-analysiss-sketch)
  - [B.1 Step 1 — clamp 0: bound `n` against `ART_DATA_BYTES`](#b1-step-1--clamp-0-bound-n-against-art_data_bytes)
  - [B.2 Step 2 — clamps 1 and 2: the source buffer](#b2-step-2--clamps-1-and-2-the-source-buffer)
  - [B.3 Step 3 — clamp 3: the palette index](#b3-step-3--clamp-3-the-palette-index)
  - [B.4 Step 4 — the `_sp` split, so the decoder is testable at all](#b4-step-4--the-_sp-split-so-the-decoder-is-testable-at-all)
  - [B.5 Step 5 — `--decode-test` with a three-sided negative control](#b5-step-5--decode-test-with-a-three-sided-negative-control)
  - [B.6 Byte cost and stack/memory invariants](#b6-byte-cost-and-stackmemory-invariants)
  - [B.7 Verification](#b7-verification)
- [Part C — Doc updates these two changes require](#part-c--doc-updates-these-two-changes-require)
- [Part D — Execution checklist for Claude Code](#part-d--execution-checklist-for-claude-code)

---

# Part A — ERR-1: pathological-seed handling

## A.0 Design constraints

Six constraints shape every line below. The first is the one that decides the whole shape of the fix.

| # | Constraint | Why |
|---|---|---|
| 1 | **The retry is a `for` loop inside `game_init`, never recursion** | `Scratch sc` is a **~360 KB stack local** ([main.c:4109](../src/main.c#L4109)). A recursive `game_init` would put a second `Scratch` *and* the caller's `World` on one frame — roughly 1.03 MB against MinGW's 2 MB default, before `world_heights`'s own `Uint8 dist[157][164]` and before any self-test that already declares `World` + `Scratch` together. Invariant 2 is about `sizeof(World) + sizeof(Scratch)`; recursion doubles the term the guard was written to bound. A loop reuses the one frame and costs nothing |
| 2 | **No new file-scope state, and no new `Game` field** | Invariant 3. The retry needs no bookkeeping: it is fully observable through `rngs->seed` and `g->seed`, both of which already exist. `gen_attempts` is deliberately **not** overloaded — it has a published encoding (`>0` attempts used, `<0` gating relaxed, `-100` total ungate) that [main.c:8905-8912](../src/main.c#L8905-L8912) reads |
| 3 | **Determinism is preserved exactly** | Invariant 5. `seed → world` stays a pure function: seed *S* deterministically yields *S*'s world, or deterministically yields *S+1*'s if *S* is pathological. No wall clock, no state carried between calls. `rngs_init` reseeds all three PCG32 streams from the new master seed, so stream independence is untouched |
| 4 | **The seed the caller displays must be the seed that produced the world** | This is the constraint that is easy to miss and expensive to get wrong. `main` keeps its **own** `seed` local and passes it to `hud_draw` ([main.c:14004](../src/main.c#L14004)) and to the title bar ([main.c:14040](../src/main.c#L14040)). If `game_init` silently advances the seed and `main` does not resync, the HUD shows a seed that **does not reproduce the world on screen** — which destroys strength O1, the single best debugging property this project has. See [§A.3](#a3-step-3--resync-the-callers-seed) |
| 5 | **Collision truth is untouched** | Invariant 1. This change is pure generation control flow. `tile_blocked` is not read, not written, not reasoned about |
| 6 | **The self-test gets a negative control** | Invariant 6, and this branch currently has **zero** coverage (QA-3). See [§A.5](#a5-step-4--genfail-test-with-a-three-sided-negative-control) |

### What is actually broken, restated precisely

The gap analysis names two of the four defects on this path. All four are here:

| # | Defect | Evidence |
|---|---|---|
| 1 | `ents[i].tile == 0`, not `-1` | `SDL_zero(*g)` at [4119](../src/main.c#L4119) zeroes the array; the early return at [4159-4168](../src/main.c#L4159-L4168) never calls `place_entities`. Every consumer guards on `tile < 0` — [`entity_in_reach`](../src/main.c#L3320), [`mm_draw`](../src/main.c#L4812), and `render_grid`'s marker loop (`if (t < 0) continue;`) — so all 19 entities are treated as **placed at tile 0**, the top-left world corner |
| 2 | `shards[i] == 0`, not `-1` — **not in the gap analysis** | Identical mechanism. `shards[i] < 0` is the universal guard ([3327](../src/main.c#L3327), [4314](../src/main.c#L4314), [4812](../src/main.c#L4812), [7539](../src/main.c#L7539)), and the field comment at [1472](../src/main.c#L1472) states the `-1`-means-resolved idiom outright. Eight phantom shards stack on tile 0, drawn on the minimap and collectable |
| 3 | `g->seed` is never set — **not in the gap analysis** | `g->seed = rngs->seed;` is at [4240](../src/main.c#L4240), **after** the early return at 4167. On this path `g->seed` keeps the `0` that `SDL_zero` left, so `tile_hash` decorates with seed 0 and a save written from this state records seed 0 |
| 4 | The world is unwinnable and nothing says so | `region_count = 0` → `world_stage()` is 0 forever; `game_complete()` needs 19 restores that can never happen |

Defects 1–3 are all fixed by the same four lines. Defect 4 is what the retry loop removes.

---

## A.1 Step 1 — the retry loop in `game_init`

**Location**: `src/main.c`, module 20 (`game_init`), lines **4113–4168**.

### A.1.1 Add the bound

Insert immediately **before** `static int game_init(` (line **4105**), above the existing
`/* Spawn in the LARGEST open region… */` comment block at 4101:

```c
/* ERR-1. How many times game_init will walk to the next seed before it accepts a world with no
 * open overworld component at all.
 *
 * 8 rather than 1: the condition is a property of the seed, not a transient, so a single retry
 * either fixes it or does not. 8 rather than 64: generation is ~17 ms, and render_grid runs
 * game_init twelve times on one keypress (main.c:7712), so the worst-case F2 hitch is
 * 12 x 8 x 17 ms = 1.6 s. That is the number to raise if this bound is ever hit in practice —
 * and if it is, the generator is what needs fixing, not the bound.
 *
 * The walk is `seed + 1`, deliberately the same step the R key takes (main.c:13909), so a player
 * pressing R past a pathological seed and game_init stepping past it internally land on the same
 * world. One rule, not two. */
#define GEN_RETRY_MAX 8
```

### A.1.2 Restructure the body

**Anchor** — line **4113**:

```c
    int x, y, biggest = 0, biggest_first = -1;
```

**Replace with**:

```c
    int x, y, biggest = 0, biggest_first = -1;
    int attempt;
```

**Anchor** — the block from line **4115** (`/* Wipe everything first.`) through line **4168**
(the closing `}` of the pathological branch). Replace the whole span with:

```c
    /* ERR-1: generate, and if the seed produced no open overworld component at all, step to the
     * next seed and generate again. A LOOP, not recursion: `sc` above is ~360 KB of frame, and a
     * recursive call would put a second Scratch beside it — the exact term wayfarer_stack_guard
     * (main.c:1410) exists to bound. Reusing this one frame costs nothing.
     *
     * Before this, the no-component case carved a single tile, set region_count = 0 and returned
     * a world with zero regions, zero placed entities and zero placed shards — permanently
     * unwinnable, permanently reporting "Unexplored", and indistinguishable from a normal world
     * to a player. There is no test that forces the branch, so nobody knows how likely it is;
     * making it recoverable is cheaper than measuring it. */
    for (attempt = 0; attempt < GEN_RETRY_MAX; attempt++) {

        /* Wipe everything first. Leaving progress counters alone made restoration
         * totals accumulate across regenerations — pressing R would have carried
         * the previous world's fragment count into the new one, and
         * game_complete() would fire on a world nobody had touched.
         *
         * Also the per-attempt reset: every attempt starts from a clean Game, so attempt 2 cannot
         * inherit anything attempt 1 wrote. */
        SDL_zero(*g);
        biggest = 0;
        biggest_first = -1;

        world_gen(&g->w, &rngs->terrain);
#if WAYFARER_SELFTEST
        /* --genfail-test only: force the no-component condition so the recovery path has
         * coverage. Always 0 outside that test. See the flag's declaration. */
        if (g_force_pathological > 0) {
            g_force_pathological--;
            SDL_memset(g->w.solid, 1, sizeof(g->w.solid));
        }
#endif
        castle_apply_layout(&g->w, g->has_castle_key);
        /* Rivers before buildings, so a house is never stamped across a channel,
         * and both before the flood fill so the verifier gets to reject a layout
         * either of them walls off. */
        place_rivers(&g->w, &rngs->terrain, sc.dist, sc.queue);
        place_buildings(&g->w, &rngs->terrain);
#if WAYFARER_SELFTEST
        if (!g_suppress_paths)
#endif
            place_paths(&g->w);
        /* And the portal, for the same reason and in the same window: before the flood fill and the
         * verifier, so a layout it cannot serve is rejected and regenerated by machinery that already
         * exists (decision 13). From here on, tile_neighbours reports the portal edge to every
         * traversal, so the spawn fill, the region graph and the completability proof all see it. */
        place_portal(&g->w, &rngs->terrain, sc.seen, sc.stack, rngs->seed);
        SDL_memset(seen, 0, sizeof(sc.seen));

        /* Pass 1: find the largest open region. */
        /* IN THE OVERWORLD. The row bound is not cosmetic: this swept the whole grid, which was
         * correct while the grid held one island — but the dream sector is a second landmass, and on
         * a seed where its largest component beats the overworld's, the player would have spawned in
         * the dream realm, before any portal, in a sector with no entities and no way home.
         *
         * Nothing caught it. --land-test measures `home` from wherever the spawn lands, so a dream
         * spawn looks perfectly healthy, and --sector-test had no opinion about where the player
         * starts. It does now. */
        for (y = 1; y < OVERWORLD_H - 1; y++) {
            for (x = 1; x < WORLD_W - 1; x++) {
                int first = -1, sx = 0, sy = 0;
                int n = flood_open(&g->w, seen, stack, x, y, &first, &sx, &sy);
                if (n > biggest) {
                    biggest = n;
                    biggest_first = first;
                }
            }
        }

        if (biggest_first >= 0)
            break;

        /* Pathological seed. Walk to the next one and regenerate — every stream, from one master
         * seed, exactly as rngs_init is called everywhere else. The caller's Rngs is updated in
         * place on purpose: g->seed is taken from rngs->seed at the end of this function, so the
         * world and the seed it reports stay the same fact. */
        if (attempt + 1 < GEN_RETRY_MAX)
            rngs_init(rngs, rngs->seed + 1u);
    }
```

Note that the block is otherwise **byte-for-byte the original**, re-indented one level. The only
substantive additions are the `for`, the two resets, the `#if WAYFARER_SELFTEST` forcing hook, the
`break`, and the `rngs_init` step.

---

## A.2 Step 2 — the degenerate fallback, made honest

`GEN_RETRY_MAX` consecutive pathological seeds is not a case anyone expects to see. It is also not a
case to leave undefined, and the reachability guarantee (`design/Cut List.md`: *a world with a thin
dream realm still ships; an unwinnable one does not*) says what to do — ship the degraded world, but
do not let it lie about its own state.

Insert immediately **after** the `for` loop closes and **before** the `/* Pass 2: … */` comment
(originally line 4170):

```c
    if (biggest_first < 0) {
        /* GEN_RETRY_MAX seeds in a row with no open overworld component. Carve rather than trap —
         * but unlike the original version of this branch, say so in the values every consumer
         * already reads, rather than leaving SDL_zero's zeros to be misread as data.
         *
         * `0` is a VALID tile index — the world's top-left corner. Leaving ents[] and shards[] at
         * zero told entity_in_reach (main.c:3320), shard_in_reach (main.c:3327), mm_draw
         * (main.c:4812) and render_grid's marker loop that all 19 memories and all 8 shards were
         * sitting in that corner. The whole codebase agrees that -1 means "not placed"
         * (Entity.tile, Game.shards, World.portal, World.well all use it); this path was the one
         * place that did not say it. */
        int i;

        g->w.solid[WORLD_H / 2][WORLD_W / 2] = 0;
        g->p.x = (float)(WORLD_W / 2) * TILE + TILE * 0.5f;
        g->p.y = (float)(WORLD_H / 2) * TILE + TILE * 0.5f;
        g->cam_x = 0;
        g->cam_y = 0;
        g->w.region_count = 0;
        g->w.spawn_region = -1;

        for (i = 0; i < ENTITY_COUNT; i++)
            g->ents[i].tile = -1;
        for (i = 0; i < SHARD_COUNT; i++)
            g->shards[i] = -1;

        /* And the seed. The original early return sat ABOVE `g->seed = rngs->seed;`, so this path
         * left g->seed at the 0 SDL_zero wrote — which tile_hash then used to decorate the world,
         * and which game_save would have recorded as the seed that produced it. */
        g->seed = rngs->seed;
        return 1;
    }
```

> **Deliberately unchanged**: `world_heights_all` is still skipped on this path. The existing
> comment at [4235-4237](../src/main.c#L4235-L4237) is correct — `SDL_zero` leaves `height` all
> zeros, which draws flat, which is right for a world that is one carved tile in solid rock.

---

## A.3 Step 3 — resync the caller's seed

This is the half of ERR-1 that has nothing to do with `game_init` and everything to do with not
breaking O1.

`main` holds its own `Uint64 seed` and hands it to the HUD and the title bar. `game_init` may now
advance `rngs.seed` past it. Two call sites need one line each.

### A.3.1 Startup

**Anchor** — line **13724**:

```c
    (void)game_init(&game, &rngs);
```

**Replace with**:

```c
    (void)game_init(&game, &rngs);
    /* ERR-1: game_init may have walked to the next seed. `seed` is what the HUD (main.c:14004)
     * and the window title (main.c:14040) show, and the entire value of showing it is that
     * --seed N reproduces the world on screen. Resync or the number becomes a lie. */
    seed = rngs.seed;
```

### A.3.2 The `R` key

**Anchor** — lines **13908–13911**:

```c
                case SDLK_r: /* regenerate with the next seed */
                    seed++;
                    rngs_init(&rngs, seed);
                    (void)game_init(&game, &rngs);
```

**Replace with**:

```c
                case SDLK_r: /* regenerate with the next seed */
                    seed++;
                    rngs_init(&rngs, seed);
                    (void)game_init(&game, &rngs);
                    seed = rngs.seed;   /* ERR-1: game_init may have walked further */
```

The `audio.rng_seed_req = seed;` three lines below then picks up the resynced value automatically,
so the music seeds from the same master seed as the world — which is what
[main.c:954](../src/main.c#L954) says the audio stream's single source of truth is supposed to be.

> **Why this cannot be done inside `game_init` instead.** `game_init` takes `Rngs *`, not the
> caller's display variable. Passing an extra out-parameter would change the signature of a function
> with **34 call sites**, 30 of them in the self-test harness. Two lines in `main` is the smaller
> change by a wide margin.

---

## A.4 What the retry does *not* touch

Three call sites see the retry and must be reasoned about rather than edited. All three are fine,
and one is materially safer than before.

| Call site | Behaviour after this change |
|---|---|
| **`game_load`** — [main.c:4406-4407](../src/main.c#L4406-L4407) | A saved seed is by construction a seed that already produced a world, so the retry does not fire on a normal load. It *can* fire if generation changed since the save was written (OBS-6, the live hazard). In that case `game_init` regenerates from `saved_seed + 1` and the position check at [4409](../src/main.c#L4409) runs against a world the save never described — which almost always **rejects the load** (`"[no save]"`). That is strictly better than the old behaviour, which was to load a degenerate world and let the player walk around in it. **No edit needed.** `*seed_out` already reports the real seed and `main` already assigns it |
| **`render_grid`** — [main.c:7712-7713](../src/main.c#L7712-L7713) | Cell `c` is generated from `base_seed + c`; if that seed is pathological the cell now shows `base_seed + c + 1`'s world instead. The grid draws no per-cell seed label, so nothing on screen becomes wrong — and the alternative (a cell full of 19 phantom entity markers stacked in its top-left corner, which is exactly what the `if (t < 0) continue;` guard produces today) is worse. **No edit needed** |
| **The 30 self-test call sites** | Every one of them passes a `Rngs` it seeded itself and then reads the resulting `Game`. If a batch test hits a pathological seed it now measures seed *N+1*'s world under the label *N*. This is a real (if almost certainly unexercised) reporting inaccuracy. It is **not** worth 30 edits; `--genfail-test` (§A.5) is where the behaviour is pinned, and any batch test that wants the true seed can read `g->seed` |

---

## A.5 Step 4 — `--genfail-test` with a three-sided negative control

Invariant 6, and QA-3 lists this branch as having **zero** coverage. Costs **zero shipped bytes**.

### A.5.1 The forcing flag

The project already has this pattern: `g_suppress_bridges` (1977), `g_suppress_paths` (1986) and
`g_suppress_marks` (1990) are self-test-only file statics that force a branch a test cannot
otherwise reach. They sit inside a `#if WAYFARER_SELFTEST` block that opens at line **1977** and
closes at line **1991**. Follow the pattern exactly.

**Anchor** — line **1990**, the last declaration in that block:

```c
static int g_suppress_marks = 0;
#endif
```

**Insert between them**, so the new flag is inside the same gate:

```c
/* --genfail-test only: makes the next N calls to world_gen produce an all-solid grid, which is the
 * one condition game_init's retry loop exists for and which no natural seed is known to hit. A
 * COUNTER rather than a flag, because the two things worth testing are opposite: a small N proves
 * the retry recovers, and an N past GEN_RETRY_MAX proves the degenerate fallback is reached and
 * labels itself honestly. Always 0 outside that test. */
static int g_force_pathological = 0;
```

> These are `int` file statics, not large structs, so they do not touch invariant 3 (the `.data`
> trap is about `World`/`Scratch`-sized objects), and being inside the `WAYFARER_SELFTEST` gate they
> are compiled out of the shipping build entirely — which is also why the forcing hook in §A.1.2 is
> itself wrapped in `#if WAYFARER_SELFTEST`. An ungated reference to a gated symbol is the one way
> this part fails to build; if the shipping link errors on `g_force_pathological`, that is the cause.

### A.5.2 The test

Add inside the `#if WAYFARER_SELFTEST` block, near the other `*_selftest` functions:

```c
/* ERR-1: game_init must recover from a seed with no open overworld component, and when it cannot,
 * it must produce a world that says so in the values every consumer reads.
 *
 * Three-sided, deliberately — the failure this fix is guarding against has three distinct shapes
 * and pinning only one of them would leave the other two free to regress:
 *
 *   (1) a NORMAL seed must not be disturbed: same seed in, same seed out, entities placed.
 *   (2) ONE forced failure must be RECOVERED: seed advances by exactly 1, world is fully built.
 *   (3) failure past the bound must be DEGRADED HONESTLY: region_count 0, and every entity and
 *       shard reporting -1 rather than the tile-0 lie SDL_zero leaves behind.
 *
 * The negative control is (3) read against (1): the same probe that must fire on the degenerate
 * world must NOT fire on the normal one. A probe that always says "degenerate" proves nothing, and
 * that is precisely the mistake the tile-0 bug made possible for two years. */
static int genfail_selftest(Uint64 seed)
{
    Game *g = (Game *)SDL_malloc(sizeof(Game));
    Rngs rngs;
    int fails = 0, i;
    int norm_phantom, degen_phantom;

    if (!g) {
        printf("FAIL  genfail: out of memory\n");
        return 1;
    }

    /* ---- (1) a normal seed is untouched ---------------------------------- */
    g_force_pathological = 0;
    rngs_init(&rngs, seed);
    (void)game_init(g, &rngs);

    norm_phantom = 0;
    for (i = 0; i < ENTITY_COUNT; i++)
        if (g->ents[i].tile == 0) norm_phantom++;

    if (rngs.seed != seed || g->seed != seed) {
        printf("FAIL  genfail: normal seed %.0f became %.0f (game %.0f)\n",
               (double)seed, (double)rngs.seed, (double)g->seed);
        fails++;
    }
    if (g->w.region_count == 0) {
        printf("FAIL  genfail: normal seed %.0f produced zero regions\n", (double)seed);
        fails++;
    }
    printf("normal:    seed %.0f -> %.0f, %d regions, %d entities at tile 0\n",
           (double)seed, (double)g->seed, g->w.region_count, norm_phantom);

    /* ---- (2) one forced failure is recovered ------------------------------ */
    g_force_pathological = 1;
    rngs_init(&rngs, seed);
    (void)game_init(g, &rngs);
    g_force_pathological = 0;

    if (g->seed != seed + 1u) {
        printf("FAIL  genfail: one forced failure landed on %.0f, expected %.0f\n",
               (double)g->seed, (double)(seed + 1u));
        fails++;
    }
    if (g->w.region_count == 0 || g->w.spawn_region < 0) {
        printf("FAIL  genfail: recovered world is still degenerate "
               "(%d regions, spawn_region %d)\n", g->w.region_count, g->w.spawn_region);
        fails++;
    }
    printf("recovery:  1 forced failure -> seed %.0f, %d regions, spawn_region %d\n",
           (double)g->seed, g->w.region_count, g->w.spawn_region);

    /* ---- (3) past the bound, degraded honestly ---------------------------- */
    g_force_pathological = GEN_RETRY_MAX + 4;   /* every attempt fails */
    rngs_init(&rngs, seed);
    (void)game_init(g, &rngs);
    g_force_pathological = 0;

    degen_phantom = 0;
    for (i = 0; i < ENTITY_COUNT; i++)
        if (g->ents[i].tile != -1) degen_phantom++;
    for (i = 0; i < SHARD_COUNT; i++)
        if (g->shards[i] != -1) degen_phantom++;

    if (g->w.region_count != 0 || g->w.spawn_region != -1) {
        printf("FAIL  genfail: exhausted retries did not take the degenerate path\n");
        fails++;
    }
    if (degen_phantom) {
        printf("FAIL  genfail: %d of %d entities/shards not marked unplaced (-1)\n",
               degen_phantom, ENTITY_COUNT + SHARD_COUNT);
        fails++;
    }
    /* The seed must still be reported. This was the third silent defect: the old early return sat
     * above `g->seed = rngs->seed`, so a degenerate world claimed seed 0. */
    if (g->seed != rngs.seed) {
        printf("FAIL  genfail: degenerate world reports seed %.0f, generated from %.0f\n",
               (double)g->seed, (double)rngs.seed);
        fails++;
    }
    printf("degraded:  %d attempts exhausted -> %d regions, %d phantom placements, seed %.0f\n",
           GEN_RETRY_MAX, g->w.region_count, degen_phantom, (double)g->seed);

    /* ---- negative control: the probe must discriminate --------------------- */
    /* (3)'s probe counts placements that are not -1. On the normal world of (1) that count is
     * ENTITY_COUNT + SHARD_COUNT minus whatever went unplaced — i.e. large and non-zero. If it
     * were not, the check above would pass on every world including healthy ones and would be
     * proving nothing at all. */
    {
        int discriminates = (norm_phantom < ENTITY_COUNT);   /* normal world: not all at tile 0 */
        printf("negative control (probe distinguishes healthy from degenerate): %s  "
               "[normal %d at tile 0 of %d, degenerate %d not -1]\n",
               discriminates ? "PASS" : "FAIL", norm_phantom, ENTITY_COUNT, degen_phantom);
        if (!discriminates) fails++;
    }

    SDL_free(g);
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
```

> **`SDL_malloc`, not a stack local.** `sizeof(Game)` is ~310 KB and this test holds one across
> three generations. Same reason `game_load` allocates its scratch `Game` rather than declaring one
> ([main.c:4404](../src/main.c#L4404)). Invariant 2.

### A.5.3 Dispatch

**Anchor** — line **13514**, in the test-flag block:

```c
        if (arg_flag(argc, argv, "--sprite-test"))
            return sprite_selftest();
```

**Insert after it**:

```c
        if (arg_flag(argc, argv, "--genfail-test"))
            return genfail_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
```

Then add to `tools/run-tests.ps1`'s `$tests` table, after the `sprite` row:

```powershell
    @{ n = 'genfail';  a = @('--genfail-test', '--seed', '1') }
```

and update the counts in that script's header comment and in `README.md` (25 → 26 tests, 27 with
the size assertion; 28 if Part B's `--decode-test` is also added).

---

## A.6 Byte cost and stack/memory invariants

### Bytes

| Component | Estimate |
|---|---|
| `for` loop scaffolding + two resets + `break` | ~25 B |
| `rngs_init(rngs, rngs->seed + 1u)` call | ~15 B |
| Two `-1` fill loops (19 + 8 iterations) | ~40 B |
| `g->seed = rngs->seed;` on the fallback path | ~8 B |
| Two `seed = rngs.seed;` resyncs in `main` | ~10 B |
| `GEN_RETRY_MAX` | 0 B (`#define`) |
| `g_force_pathological`, forcing hook, `genfail_selftest`, dispatch | **0 B** (self-test only) |
| **Total, budget for** | **≤ 150 B** |

### Invariants

| Invariant | Status after this change |
|---|---|
| **1 — collision truth** | Untouched. `tile_blocked` is not read or written; `solid` is written only by the pre-existing single-tile carve |
| **2 — `sizeof(World) + sizeof(Scratch)` < 700 KB** | **Unchanged at 670,272 B.** No struct gains a field. `game_init`'s frame gains one `int` (`attempt`) and one `int` (`i`, in the fallback block scope) — 8 bytes, against 46,528 B of guard margin. The loop reuses the single `Scratch sc`; **this is the reason the fix is a loop and not recursion** |
| **3 — no `.data` trap** | Respected. `g_force_pathological` is one `int` and is self-test-only. `genfail_selftest` allocates its `Game` with `SDL_malloc` |
| **4 — thread discipline** | Untouched. Nothing here runs on or signals the audio thread. The `R` handler's existing `audio.rng_seed_req` / `SDL_AtomicSet` payload-before-flag ordering is preserved, and now carries a *more* correct seed |
| **5 — determinism** | Preserved. `rngs_init` reseeds all three streams from one master seed exactly as every other caller does; stream ids are untouched. `seed → world` remains a pure function |
| **6 — test parity** | Satisfied: `--genfail-test` is three-sided with an explicit discrimination control |

---

## A.7 Verification

```powershell
# 1. Both binaries build clean, and the delta is attributable
.\build.ps1 -SelfTest
.\build.ps1                      # record the `delta` line verbatim

# 2. The new test, on its own
.\build\wayfarer-selftest.exe --genfail-test --seed 1
$LASTEXITCODE                    # expect 0

# 3. Nothing else moved. These four are the ones that touch generation hardest.
.\tools\run-tests.ps1 -Filter 'land,reach,play,sector'
$LASTEXITCODE                    # expect 0

# 4. Determinism did not regress
.\build\wayfarer-selftest.exe --rng-test --seed 1
.\build\wayfarer-selftest.exe --save-test
$LASTEXITCODE                    # expect 0

# 5. Full suite
.\tools\run-tests.ps1
$LASTEXITCODE                    # expect 0
```

**By hand, and this is the one that matters**: launch `.\build\wayfarer.exe --seed 7`, read the seed
in the title bar and in the HUD, press `R` a dozen times, and confirm the two never disagree and
that the number always increments by exactly 1. Then quit, relaunch with the seed the title last
showed, and confirm you get the same world. That is O1, and §A.3 is the only thing standing between
this change and quietly breaking it.

---

# Part B — SEC-1: in-decoder RLE bounds clamps

## B.0 Two corrections to the gap analysis's sketch

The gap analysis's three-clamp sketch is close, and its conclusion (make the decoder self-limiting
rather than shipping the validator) is right. Two things in it are wrong, and one of them matters a
great deal.

### Correction 1 — `pal[v]` is **not** an out-of-bounds read

The gap analysis says the palette index is *"unchecked against `sp->pal_n`, so `pal[v]` can read
past the 64-entry stack array `Uint32 pal[ART_PAL_MAX]` — a stack read overflow"*.

`ART_PAL_MAX` is **256**, not 64 ([main.c:5468](../src/main.c#L5468)), and `v` is declared
`unsigned char` ([main.c:5612](../src/main.c#L5612)). `v` therefore cannot exceed 255, and
`pal[255]` is the last valid element. **`pal[v]` is always in bounds.**

What actually goes wrong is narrower and still worth fixing: `art_palette` fills only
`out[1 .. pal_n]` ([main.c:5533-5540](../src/main.c#L5533-L5540)), so entries `pal_n+1 .. 255` hold
whatever was on the stack. A bad index reads an **indeterminate value** and paints a garbage colour
— not a crash, not a memory-safety violation, but a sprite with random pixels whose cause is
invisible. The clamp is still correct; the justification is "no indeterminate reads and a
predictable failure mode", not "prevents a stack overflow". Fixing the claim matters because a
security note that overstates its own severity gets discounted the next time it is read.

### Correction 2 — there is a **fourth** clamp, and it is the one that closes the real hole

All three of the sketched clamps are expressed relative to `n`:

```c
    i = sp->data_off;
    n = sp->data_off + sp->data_len;        /* main.c:5593-5594 — never checked */
```

`art_stream_ok_sp` checks this — `if (n > ART_DATA_BYTES) return 0;` at
[main.c:5496-5497](../src/main.c#L5496-L5497) — and `draw_sprite_ex` **does not**. If a bad bake
emits a descriptor whose `data_off + data_len` runs past the end of the 190,377-byte `ART_DATA`
array, every one of the three clamps is measured against a bogus `n` and the decoder reads out of
bounds anyway, all three clamps notwithstanding.

This is also the only genuinely out-of-bounds read on this path. The sketch's cases (a) and (b) —
a trailing RUN control byte and an over-long LITERAL — read past `n`, but `n` is normally an offset
*inside* `ART_DATA`, so those reads land in the **next sprite's data**: wrong pixels, not a fault.
They become true out-of-bounds reads only for the sprite whose slice ends at `ART_DATA_BYTES`.

So: four clamps, in the order they must appear. Clamp 0 is the load-bearing one.

### Design constraints

| # | Constraint | Why |
|---|---|---|
| 1 | **The clamps ship.** No `#if` gate of any kind | The entire point of SEC-1 is that the guarantee currently lives only in the unshipped binary |
| 2 | **Malformed data draws a hole, never a fault, never a hang** | The failure mode has to be *visible and harmless*: a missing sprite is a bug report; a garbage-pixel sprite is a mystery; a fault is a lost submission |
| 3 | **`i <= n` is an invariant at every clamp site** | It is what makes `n - i` safe in unsigned arithmetic. Proven in §B.2 |
| 4 | **No new state, no new allocation, no signature change to the shipping call path** | Invariant 3. `draw_sprite`, `draw_sprite_fade`, `draw_sprite_flip` keep their signatures exactly |
| 5 | **The decoder gets a negative control** | Invariant 6. This requires the `_sp` split in §B.4 — the same move `art_stream_ok_sp` already made, for the same reason, documented in its own comment at [main.c:5482-5485](../src/main.c#L5482-L5485) |

---

## B.1 Step 1 — clamp 0: bound `n` against `ART_DATA_BYTES`

**Anchor** — lines **5593–5596**:

```c
    i = sp->data_off;
    n = sp->data_off + sp->data_len;
    x = 0;
    y = 0;
```

**Replace with**:

```c
    /* SEC-1 clamp 0, and the load-bearing one: every clamp below is expressed relative to `n`, so
     * an `n` that is itself out of bounds makes the other three worthless. art_stream_ok_sp has
     * checked this since it was written (main.c:5496) — it just never shipped.
     *
     * A truncated or mis-sized bake is the whole failure scenario here: bake.ps1 emits a header
     * that compiles fine, --sprite-test catches it in the binary nobody ships, and release draws
     * from a descriptor pointing past the end of a 190,377-byte const array. Clamping rather than
     * returning, so a merely over-long slice still draws the part of itself that is real. */
    i = sp->data_off;
    n = sp->data_off + sp->data_len;
    if (n > ART_DATA_BYTES || n < sp->data_off)   /* second test catches unsigned wraparound */
        n = ART_DATA_BYTES;
    x = 0;
    y = 0;
```

`i > n` needs no separate guard: the `while (i < n && …)` head at 5598 is already false, the loop
body never runs, and the function falls through to `PERF_COUNT` and returns having drawn nothing.

---

## B.2 Step 2 — clamps 1 and 2: the source buffer

**Anchor** — lines **5604–5609**:

```c
        if (literal) {
            count = (c & 0x7Fu) + 1u;
        } else {
            count = c + 1u;
            idx = ART_DATA[i++];
        }
```

**Replace with**:

```c
        if (literal) {
            count = (c & 0x7Fu) + 1u;
            /* SEC-1 clamp 2: a LITERAL whose declared count runs past the end of this sprite's
             * slice would read up to 127 bytes of the next sprite's stream — or past the array
             * entirely, for the last sprite in ART_DATA. Draw the bytes that exist and stop. */
            if (i + count > n)
                count = n - i;
        } else {
            count = c + 1u;
            /* SEC-1 clamp 1: a RUN control byte as the final byte of the slice leaves no index
             * byte to read. Reading it anyway is the (a) case in the gap analysis. */
            if (i >= n)
                break;
            idx = ART_DATA[i++];
        }
```

### Why `n - i` cannot underflow

Both clamps sit between the loop head and the next iteration, and the loop head guarantees `i < n`.
The only mutation in between is the single `i++` on line 5599 (`c = ART_DATA[i++]`), so at both
clamp sites:

> `i < n` at the head, one increment, therefore **`i <= n`**, therefore `n - i >= 0`.

`i` and `n` are `unsigned int`, so this matters: at `i == n`, clamp 2 sets `count = 0`, the inner
`for` runs zero times, `i += 0` leaves `i == n`, and the `while` head rejects on the next test. No
underflow, no infinite loop. Clamp 1 catches the same `i == n` case for RUN before the read.

`i + count` cannot overflow either: `i <= 190,377` and `count <= 128`.

---

## B.3 Step 3 — clamp 3: the palette index

**Anchor** — line **5612**:

```c
            unsigned char v = literal ? ART_DATA[i + k] : idx;
```

**Replace with**:

```c
            unsigned char v = literal ? ART_DATA[i + k] : idx;
            /* SEC-1 clamp 3. art_palette fills pal[1 .. pal_n] only (main.c:5536), so a larger
             * index reads an indeterminate stack value and paints a colour nobody chose. Index 0
             * is the bake's guaranteed-transparent entry, so folding a bad index to 0 makes
             * malformed data draw a HOLE — visible, harmless, and reportable — rather than
             * confetti. Same test art_stream_ok_sp uses (main.c:5504), so the decoder and the
             * validator agree on what "in palette" means. */
            if (v > sp->pal_n)
                v = 0;
```

The existing `if (v)` guard on the next line then skips the pixel entirely, which is exactly the
transparent path. No further change is needed inside the blend.

---

## B.4 Step 4 — the `_sp` split, so the decoder is testable at all

Invariant 6 requires a negative control, and a negative control here requires handing the decoder a
**deliberately malformed sprite record**. `draw_sprite_ex` takes an `int id` and indexes
`ART_SPRITES`, which is genuinely `const` in `.rodata` — writing through it is undefined behaviour
and would fault on a read-only page. `art_stream_ok_sp` hit this exact wall and solved it by taking
a pointer; its comment at [main.c:5482-5485](../src/main.c#L5482-L5485) records the earlier version
that cast the const away.

Make the same move.

**Anchor** — lines **5568–5579**:

```c
static void draw_sprite_ex(SDL_Surface *fb, int id, int cx, int by, float rev,
                           int fade, int flip)
{
    const ArtSprite *sp;
    Uint32 pal[ART_PAL_MAX];
    Uint32 rmask, gmask, bmask;
    unsigned int i, n;
    int x0, y0, x, y;

    if (id < 0 || id >= ART_SPRITE_COUNT)
        return;
    sp = &ART_SPRITES[id];
```

**Replace with**:

```c
/* Takes the record by POINTER rather than by id, for the same reason art_stream_ok_sp does
 * (main.c:5482): --decode-test's negative control has to hand the decoder a record that lies about
 * its own stream, and ART_SPRITES is const in .rodata. A local copy is legal where casting the
 * const away is not.
 *
 * The id-taking wrapper below is what every caller uses; this is not a second code path. */
static void draw_sprite_sp(SDL_Surface *fb, const ArtSprite *sp, int cx, int by, float rev,
                           int fade, int flip)
{
    Uint32 pal[ART_PAL_MAX];
    Uint32 rmask, gmask, bmask;
    unsigned int i, n;
    int x0, y0, x, y;

    if (!sp)
        return;
```

Then **append immediately after** the closing brace of the function (currently line **5643**, after
`PERF_COUNT(sp->w * sp->h);`):

```c
static void draw_sprite_ex(SDL_Surface *fb, int id, int cx, int by, float rev,
                           int fade, int flip)
{
    if (id < 0 || id >= ART_SPRITE_COUNT)
        return;
    draw_sprite_sp(fb, &ART_SPRITES[id], cx, by, rev, fade, flip);
}
```

`draw_sprite`, `draw_sprite_fade` and `draw_sprite_flip` (lines 5646–5670) are **unchanged** — they
still call `draw_sprite_ex`, still with the same signatures.

> **Byte cost of the split: expected 0.** `draw_sprite_ex` becomes a two-line static with a handful
> of callers; under `-Os` GCC inlines it into them, leaving one real function where there was one
> real function. **Measure it** — `build.ps1` prints the delta. If it costs more than ~30 B, mark
> `draw_sprite_ex` as the wrapper it is and check `-Map` output before accepting.

---

## B.5 Step 5 — `--decode-test` with a three-sided negative control

Add inside the `#if WAYFARER_SELFTEST` block, next to `sprite_selftest` (line **11022**).

```c
/* SEC-1: the shipping decoder must survive a malformed stream.
 *
 * sprite_selftest already proves the baked DATA is well-formed (art_stream_ok over all 138
 * sprites). This proves the DECODER is well-behaved when the data is not — which is a different
 * claim, and the one the shipping binary actually depends on, because art_stream_ok does not ship.
 *
 * Three malformed records, each shaped like a real bake failure:
 *   (a) a slice running past the end of ART_DATA      — a truncated or mis-sized emit
 *   (b) a slice ending on a RUN control byte          — the classic dropped final byte
 *   (c) a stream whose indices exceed the palette     — a palette/stream mismatch
 *
 * The negative control is that all three must be REJECTED by art_stream_ok_sp. Without it this
 * test could be feeding the decoder perfectly valid data and reporting "survived" forever. */
static int decode_selftest(void)
{
    /* A framebuffer with a margin all round, pre-filled with a sentinel. Anything the decoder
     * writes outside the sprite's own box lands on a sentinel pixel and is detectable. */
    enum { FBW = 128, FBH = 128, MARGIN = 32 };
    SDL_Surface *fb = SDL_CreateRGBSurface(0, FBW, FBH, 32, 0x00FF0000u, 0x0000FF00u,
                                           0x000000FFu, 0u);
    const Uint32 SENTINEL = 0x00ABCDEFu;
    ArtSprite bad[3];
    const char *name[3] = { "slice past ART_DATA_BYTES",
                            "slice ends on a RUN control byte",
                            "palette index past pal_n" };
    int fails = 0, t, escaped_total = 0, rejected = 0;

    if (!fb) {
        printf("FAIL  decode: could not create surface\n");
        return 1;
    }

    /* (a) real sprite 0, but the slice claims to run past the end of the array. */
    bad[0] = ART_SPRITES[0];
    bad[0].data_len = ART_DATA_BYTES;            /* off + len is now well past the end */

    /* (b) real sprite 0, truncated so the final byte is a control byte with no operand.
     *     Length 1 guarantees it: byte 0 is a control byte and nothing follows it. */
    bad[1] = ART_SPRITES[0];
    bad[1].data_len = 1;

    /* (c) real sprite 0's stream, but the record claims a 1-entry palette, so almost every index
     *     in it is out of range. */
    bad[2] = ART_SPRITES[0];
    bad[2].pal_n = 1;

    for (t = 0; t < 3; t++) {
        int x, y, escaped = 0;
        int bx0, by0, bx1, by1;

        /* Fill with the sentinel, then draw with the anchor at the centre. */
        SDL_FillRect(fb, NULL, SENTINEL);
        draw_sprite_sp(fb, &bad[t], FBW / 2, FBH / 2, 1.0f, 0, 0);

        /* The box the sprite is contractually allowed to touch: anchor is bottom-centre. */
        bx0 = FBW / 2 - bad[t].anchor_x;
        by0 = FBH / 2 - bad[t].anchor_y;
        bx1 = bx0 + bad[t].w;
        by1 = by0 + bad[t].h;

        for (y = 0; y < FBH; y++) {
            for (x = 0; x < FBW; x++) {
                Uint32 p = *(Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch + x * 4);
                if (p == SENTINEL)
                    continue;
                if (x < bx0 || x >= bx1 || y < by0 || y >= by1)
                    escaped++;
            }
        }
        escaped_total += escaped;
        printf("decode: %-34s  %s  (%d px outside the sprite box)\n",
               name[t], escaped ? "FAIL" : "PASS", escaped);
        if (escaped) fails++;
    }

    /* ---- negative control: the three records are genuinely malformed ------- */
    /* If art_stream_ok_sp accepted any of them, this test would have been proving that the decoder
     * survives VALID data — true, useless, and indistinguishable from a real pass. */
    for (t = 0; t < 3; t++)
        if (!art_stream_ok_sp(&bad[t]))
            rejected++;
    printf("negative control (all 3 records rejected by the validator): %s  [%d of 3]\n",
           rejected == 3 ? "PASS" : "FAIL", rejected);
    if (rejected != 3) fails++;

    /* ---- and the positive half: a REAL sprite must still draw --------------- */
    /* A decoder clamped into drawing nothing at all would pass everything above. */
    {
        int x, y, drawn = 0;
        SDL_FillRect(fb, NULL, SENTINEL);
        draw_sprite_sp(fb, &ART_SPRITES[0], FBW / 2, FBH / 2, 1.0f, 0, 0);
        for (y = 0; y < FBH; y++)
            for (x = 0; x < FBW; x++)
                if (*(Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch + x * 4) != SENTINEL)
                    drawn++;
        printf("control (a valid sprite still draws): %s  [%d px]\n",
               drawn > 0 ? "PASS" : "FAIL", drawn);
        if (drawn <= 0) fails++;
    }

    SDL_FreeSurface(fb);
    printf("%s  (%d escaped writes across 3 malformed streams)\n",
           fails ? "FAIL" : "PASS", escaped_total);
    return fails ? 1 : 0;
}
```

**Dispatch** — insert after the `--genfail-test` line added in §A.5.3:

```c
        if (arg_flag(argc, argv, "--decode-test"))
            return decode_selftest();
```

and in `tools/run-tests.ps1`:

```powershell
    @{ n = 'decode';   a = @('--decode-test') }
```

> **What this test does and does not prove.** It proves the decoder confines its *writes* and
> terminates on malformed input. It does **not** prove the absence of out-of-bounds *reads* — no
> pure-C test can, because an out-of-bounds read of a const array has no observable effect. That
> proof is QA-4's job: `-fsanitize=address,undefined` over `--decode-test` is the only thing that
> can assert it, and it is Tier 2 item 11. Note the dependency here so the gap does not get
> forgotten: **the clamps close the hole, this test proves the surrounding behaviour, and UBSan is
> what will eventually prove the clamps themselves.**

---

## B.6 Byte cost and stack/memory invariants

### Bytes

| Component | Estimate |
|---|---|
| Clamp 0 (two compares, one store) | ~14 B |
| Clamp 1 (compare + branch to loop exit) | ~8 B |
| Clamp 2 (add, compare, conditional subtract) | ~14 B |
| Clamp 3 (compare against `sp->pal_n`, conditional zero) | ~10 B |
| `draw_sprite_sp` / `draw_sprite_ex` split | **0 B expected** (inlined at `-Os`); budget 30 B |
| `decode_selftest` + dispatch | **0 B** (self-test only) |
| **Total, budget for** | **≤ 80 B** |

Clamps 1–3 sit inside the sprite decode loop, which runs per RLE record and per pixel-run. Three
integer compares against values already in registers is not measurable against the existing per-pixel
blend, and `PERF_COUNT` is already tracking this path if you want to confirm — run
`--perf` before and after and compare `rnd` ms at the same camera position.

### Combined with Part A

| | Bytes |
|---|---|
| Shipping size after items 1–2 | 924,672 |
| Headroom under the 1,440,000 ship target | 515,328 |
| Part A worst case | ~150 |
| Part B worst case | ~80 |
| **Headroom consumed** | **≤ 0.045%** |
| Projected size | ~924,900 (≈ 515,100 B under target) |

Measure, do not trust the estimate. `build.ps1` prints `delta` against `build/.last_size` on every
build, and the two parts should be built and measured **separately** so each delta is attributable.

### Invariants

| Invariant | Status after this change |
|---|---|
| **1 — collision truth** | Untouched. `draw_sprite_ex` is render-only and reads no collision input |
| **2 — `sizeof(World) + sizeof(Scratch)` < 700 KB** | **Unchanged at 670,272 B.** No struct is modified. `draw_sprite_sp`'s frame is the same as `draw_sprite_ex`'s was — `Uint32 pal[256]` (1,024 B) plus a handful of scalars — and the wrapper adds at most one call frame that `-Os` is expected to inline away. `decode_selftest`'s `SDL_Surface` is heap, not stack |
| **3 — no `.data` trap** | Respected. No new file-scope object of any kind |
| **4 — thread discipline** | Untouched. The decoder runs on the game thread only |
| **5 — determinism** | Untouched. `draw_sprite_ex` reads no RNG stream; decoration is `tile_hash`'s job and this path does not touch it. On well-formed art the clamps are **never taken**, so the rendered output is bit-identical — which `--sprite-test`, `--rebuild-test` and `--fade-test` will confirm |
| **6 — test parity** | Satisfied: `--decode-test` has a three-sided negative control plus a positive control against drawing nothing |

---

## B.7 Verification

```powershell
# 1. Build, read the delta (built separately from Part A)
.\build.ps1 -SelfTest
.\build.ps1

# 2. The new test
.\build\wayfarer-selftest.exe --decode-test
$LASTEXITCODE                    # expect 0

# 3. Well-formed art must render EXACTLY as before — the clamps are never taken on good data
.\tools\run-tests.ps1 -Filter 'sprite,fade,rebuild,iso,hud,font'
$LASTEXITCODE                    # expect 0

# 4. Pixel-identical proof, stronger than the above: same shot before and after
.\build\wayfarer-selftest.exe --castle 3 --dev --frames 2 --shot build\keep_after.bmp
#   compare against a build\keep_before.bmp captured BEFORE applying Part B
Compare-Object (Get-FileHash build\keep_before.bmp).Hash (Get-FileHash build\keep_after.bmp).Hash

# 5. Full suite
.\tools\run-tests.ps1
$LASTEXITCODE                    # expect 0
```

Step 4 is the one worth the extra minute. The clamps are only correct if they are **inert on
well-formed data**, and an identical file hash across the change is the cheapest possible proof of
that. Capture `keep_before.bmp` before you start editing.

---

# Part C — Doc updates these two changes require

| Doc | Change |
|---|---|
| `docs/production-gap-analysis.md` §2 ERR-1 | Mark resolved. Record the four defects, not two: the entity `-1`, the **shard `-1`** and the **unset `g->seed`** were all on the same path, and the retry loop is what closes the fourth |
| `docs/production-gap-analysis.md` §2 ERR-3 | **Partially** closed. `game_init`'s return is still discarded at all 34 sites — but the *degenerate* case it was most needed for now cannot occur without `GEN_RETRY_MAX` consecutive failures. The "low reachable-tile count" half (the 40-reachable-tiles world) is untouched and stays open |
| `docs/production-gap-analysis.md` §1 SEC-1 | Mark resolved, and **correct the two errors in the entry itself**: `ART_PAL_MAX` is 256 not 64, so `pal[v]` was never an out-of-bounds read; and the fix is **four** clamps, not three, because `n` was itself unchecked |
| `docs/production-gap-analysis.md` §4 QA-3 | Remove two rows: "`game_init` pathological-seed early return" and "`draw_sprite_ex` against a malformed stream" now have `--genfail-test` and `--decode-test` |
| `docs/production-gap-analysis.md` §4 QA-4 | Add a note: `--decode-test` is the natural first target for the `-Sanitize` build, because it is the one test that deliberately feeds malformed data to a pointer-dense decode loop |
| `docs/production-gap-analysis.md` §6 Tier 1 | Strike items 3 and 4, with measured byte costs |
| `docs/architecture-summary.md` §2.4 | `tools/run-tests.ps1` now runs 27 self-tests + the size assertion |
| `docs/architecture-summary.md` §6.1 | The generation flowchart gains the retry: `flood_open x2` → *no component?* → `seed + 1`, up to `GEN_RETRY_MAX` |
| `docs/architecture-summary.md` §8.2 | Add `--genfail-test` and `--decode-test` to the test entry-point table |
| `docs/architecture-summary.md` §7, module 29 | Add `draw_sprite_sp` to the sprites module row |
| `docs/architecture-summary.md` §9 | Invariant 2 gains a sentence: `--seed N` reproduces a world, and where *N* is pathological it reproduces *N+1*'s world **deterministically** — the mapping is still a pure function |
| `README.md` | 25 → 27 tests in the invocation list and in the "all green" claim |
| `tools/run-tests.ps1` | Two `$tests` rows; update the header comment's count and the `size` test's position in the numbering |

---

# Part D — Execution checklist for Claude Code

Run these as ordered tasks in VS Code. **1–2 are pre-flight, 3–9 are Part A, 10–16 are Part B,
17–19 close out.** Do not interleave Part A and Part B: the byte deltas must stay attributable.

1. **Commit items 1–2 first.** `git status` should currently show `M src/main.c` with `HEAD` at
   `3d14508`. Commit that plus `docs/`, `tools/run-tests.ps1` and `CLAUDE.md` before touching
   anything below, or every measurement in this document becomes a measurement of four changes at
   once. Report the resulting commit hash.
2. **Capture the pixel baseline** for §B.7 step 4, from the *current* binary:
   `.\build.ps1` then
   `.\build\wayfarer-selftest.exe --castle 3 --dev --frames 2 --shot build\keep_before.bmp`
   (build the self-test binary too). Record the file hash.

### Part A — ERR-1

3. **Verify the anchors.** Grep `src/main.c` and confirm **exactly one** match apiece:
   `biggest_first < 0`, `int x, y, biggest = 0, biggest_first = -1;`,
   `g->w.spawn_region = -1;`, `static int g_suppress_paths = 0;`,
   `case SDLK_r: /* regenerate with the next seed */`. Also confirm `(void)game_init(&game, &rngs);`
   matches **exactly twice** (13724 and 13911). If any count differs, stop and report.
4. **Confirm the self-test gate.** Lines **1977–1991** should be a `#if WAYFARER_SELFTEST` block
   containing `g_suppress_bridges`, `g_suppress_paths` and `g_suppress_marks`. Confirm that, then
   put `g_force_pathological` inside it. If the block is not there, stop and report — the forcing
   hook in `game_init` depends on the symbol being gated the same way.
5. **Add `GEN_RETRY_MAX`** (§A.1.1) above `game_init`.
6. **Restructure `game_init`** (§A.1.2 and §A.2). The re-indented block must be otherwise
   byte-identical to the original; diff it against `git show HEAD:src/main.c` to confirm the only
   changes are the loop, the resets, the forcing hook, the `break` and the `rngs_init` step.
7. **Add the two `seed = rngs.seed;` resyncs** in `main` (§A.3). This is the step whose omission is
   invisible at build time and breaks the seed-reproducibility property at runtime.
8. **Add `g_force_pathological`, `genfail_selftest` and its dispatch** (§A.5).
9. **Build both binaries and run §A.7.** Zero new warnings under `-Wall -Wextra`. Report the `delta`
   line verbatim and the `--genfail-test` output in full.

### Part B — SEC-1

10. **Verify the anchors.** Confirm **exactly one** match apiece for `idx = ART_DATA[i++];`,
    `unsigned char v = literal ? ART_DATA[i + k] : idx;`, and
    `static void draw_sprite_ex(SDL_Surface *fb, int id, int cx, int by, float rev,`.
    `i = sp->data_off;` and `n = sp->data_off + sp->data_len;` each match **twice** — once in
    `art_stream_ok_sp` (~5494) and once in `draw_sprite_ex` (~5593). **Edit only the second.**
    Touching the validator's copy would be harmless but pointless; touching it *instead* would
    leave the shipping decoder unfixed and every test still green.
    Confirm `#define ART_PAL_MAX 256` and `#define ART_DATA_BYTES 190377` — if either number
    differs from this document, the tree has drifted and §B.0's analysis needs re-checking before
    you edit.
11. **Apply clamp 0** (§B.1).
12. **Apply clamps 1 and 2** (§B.2).
13. **Apply clamp 3** (§B.3).
14. **Split `draw_sprite_sp` / `draw_sprite_ex`** (§B.4). Confirm `draw_sprite`, `draw_sprite_fade`
    and `draw_sprite_flip` are untouched.
15. **Add `decode_selftest` and its dispatch** (§B.5).
16. **Build and run §B.7**, including the `keep_before.bmp` / `keep_after.bmp` hash comparison from
    step 2. **The hashes must be identical.** If they are not, a clamp is firing on well-formed art
    — stop and report which of the four, before doing anything else.

### Close out

17. **Update `tools/run-tests.ps1`** with both new rows and the corrected counts, then run the full
    suite. Report the summary table and `$LASTEXITCODE`.
18. **Apply the Part C doc updates**, including the two factual corrections to the SEC-1 entry.
19. **Report**: the two measured byte deltas separately, both against the 515,328 B headroom; the
    suite pass/fail count and runtime; the `keep_before`/`keep_after` hash result; and any anchor
    that did not match cleanly.

Do **not** bundle Tier 1 items 5 (SEC-2/SEC-6) or 6 (PERF-1) into this change. Item 5 touches
argument parsing in `main`, which Part A also edits, and item 6 rewrites `sim_step`'s reveal loop —
mixing either one makes the deltas above unattributable and the pixel-identity check in step 16
meaningless.

---

---

# Part E — Execution status (2026-08-15, branch `castle-fixed`)

All source edits applied, built and verified. One substantive correction to §A.1.2 was required —
see [§E.6](#e6-correction-to-a12--the-forcing-hook-was-in-the-wrong-place).

## E.1 Toolchain interruption (resolved)

Midway through, `G:\tools` — the `$TOOLS` root holding `w64devkit` and `SDL2-min` — **vanished**.
The drive stayed mounted and healthy but held only game installs; no `w64devkit`, `SDL2-min`,
`libSDL2.a` or `SDL.h` existed on any mounted volume, and `gcc` was not on `PATH`. It had built
cleanly at `bbbc94a` an hour earlier. The Recycle Bin was empty.

Rebuilt from scratch **inside the project root**, which `.gitignore` already excluded defensively
(`w64devkit/`, `SDL2*/`, `sdl2-src/`, `sdl2-build/`) — so `$env:WAYFARER_TOOLS` is now the project
root itself and none of it can be committed by accident:

| Component | Version | Note |
|---|---|---|
| w64devkit | 2.9.1 (GCC **16.2.0**) | previous version unknown |
| CMake | **3.31.12** | *not* 4.x — SDL2 2.32 declares `cmake_minimum_required(3.0.0...3.10)` and CMake 4 removed compatibility below 3.5, so 4.x cannot configure it |
| Ninja | 1.13.2 | w64devkit ships neither cmake nor ninja; both are merged into its `bin\` because `build-sdl2.ps1` hardcodes `$DEVKIT\bin\cmake.exe` and configures with `-G Ninja` |
| SDL2 source | 2.32.10 | the version `build-sdl2.ps1` pins |

Rebuilt `libSDL2.a` is 2,007,674 B (stock is 15,679,488).

**The compiler change moved the byte count**, so the old 924,672 B figure is not a valid baseline:
GCC 16.2.0 builds the *identical* commit `bbbc94a` at **923,648 B**, 1,024 bytes smaller — one PE
alignment block. Every measurement below is therefore against a fresh same-toolchain baseline.

**Render output is unaffected**: `--castle 3 --dev --frames 2` captured from `bbbc94a` on the *old*
toolchain and on the *new* one hashes identically (`DAA163CD…4E9994B0`). That also means the
`keep_before.bmp` baseline captured before any of this work remained valid.

## E.2 Checklist outcome

| Checklist step | Status |
|---|---|
| 1 — commit items 1–2 | ✅ `bbbc94a` |
| 2 — `keep_before.bmp` baseline | ✅ SHA-256 `DAA163CD…4E9994B0`, 1,555,254 B |
| 3–4 — verify anchors + gate | ✅ every count exactly as specified |
| 5–8 — Part A edits | ✅ applied |
| 9 — build + run §A.7 | ✅ zero warnings; `--genfail-test` green **after §E.6's fix** |
| 10 — verify Part B anchors | ✅ every count exactly as specified |
| 11–15 — Part B edits | ✅ applied |
| 16 — build + §B.7 + hash compare | ✅ **`keep_before` == `keep_after`, byte-identical** |
| 17 — runner rows + full suite | ✅ 28 checks |
| 18 — Part C doc updates | ✅ applied |

## E.2.1 Measured cost

| | Bytes |
|---|---|
| Baseline, `bbbc94a`, this toolchain | 923,648 |
| With items 3 + 4 | **924,160** |
| **Delta** | **+512** |
| Budget (§A.6 ≤150 + §B.6 ≤80) | ≤230 |
| Headroom remaining under the 1,440,000 target | 515,840 |

+512 B is **exactly one PE file-alignment block**, so the true code growth is anywhere in
(0, 512] and cannot be resolved more finely without `-Map`. It is consistent with the ≤230 B
estimate. Items 3 and 4 could not be measured separately — both were already in the tree when the
toolchain came back — so this is the combined figure, obtained by stashing both and rebuilding.

Self-test binary: 1,002,496 B → 1,006,080 B (not budget-tracked).

## E.3 Anchor verification (steps 3, 4, 10)

Every anchor matched its specified count exactly — no drift:

| Anchor | Expected | Found |
|---|---|---|
| `biggest_first < 0` | 1 | 1 (4159) |
| `int x, y, biggest = 0, biggest_first = -1;` | 1 | 1 (4113) |
| `g->w.spawn_region = -1;` | 1 | 1 (4166) |
| `static int g_suppress_paths = 0;` | 1 | 1 (1986) |
| `case SDLK_r: /* regenerate with the next seed */` | 1 | 1 (13908) |
| `(void)game_init(&game, &rngs);` | 2 | 2 (13724, 13911) |
| self-test gate block 1977–1991 | 3 flags | ✅ exactly `g_suppress_bridges`, `g_suppress_paths`, `g_suppress_marks` |
| `idx = ART_DATA[i++];` | 1 | 1 (5686) |
| `unsigned char v = literal ? ART_DATA[i + k] : idx;` | 1 | 1 (5690) |
| `static void draw_sprite_ex(SDL_Surface *fb, int id, …` | 1 | 1 (5646) |
| `i = sp->data_off;` | 2 | 2 (5572 validator, 5671 decoder) — **edited the second only** |
| `n = sp->data_off + sp->data_len;` | 2 | 2 (5573, 5672) — **edited the second only** |
| `ART_PAL_MAX` | 256 | ✅ 256, confirming §B.0 Correction 1 |
| `ART_DATA_BYTES` | 190377 | ✅ 190377 (in `art_data.h`, not `main.c`) |

## E.4 Static verification performed while the toolchain was missing

These were stand-ins, not substitutes — recorded because they turned out to be accurate: the code
compiled with **zero warnings on the first attempt** once a compiler was available, and the only
defect they could not have caught was a semantic one (§E.6).

- **Delimiter balance** over the whole 14,462-line file, after stripping comments and string/char
  literals: balanced, and every top-level function begins at brace depth 0.
- **`git diff -w`** over the `game_init` restructure confirms the re-indented block is otherwise
  byte-identical — the only changes are the `for`, the two resets, the `#if WAYFARER_SELFTEST`
  forcing hook, the `break` and the `rngs_init` step, exactly as §A.1.2 requires.
- **No dangling `id`** inside `draw_sprite_sp`'s body; the identifier survives only in the comment
  and in the new wrapper.
- **Gate placement**: `decode_selftest` (11152), `genfail_selftest` (11259) and `sprite_selftest`
  (11360) all fall inside the harness block (8073–13732); `art_stream_ok_sp`, which
  `decode_selftest` calls, is inside its own gate at 5559–5607.
- **Field widths checked by hand** because the compiler cannot: `ArtSprite.data_len` is
  `unsigned int`, so `bad[0].data_len = ART_DATA_BYTES` (190,377) does not truncate; `pal_n` is
  `unsigned short`, so `bad[2].pal_n = 1` is fine. Clamp 3's `v > sp->pal_n` compares
  `unsigned char` against `unsigned short`, both promoted to `int` — the same shape as the existing
  line 5582 in `art_stream_ok_sp`, which already compiles warning-free under `-Wextra`.
- **`tools/run-tests.ps1`** parses clean via the PowerShell AST parser; 27 self-test rows + the size
  gate.

## E.5 Verification results

| Check | Result |
|---|---|
| `-Wall -Wextra`, both builds | **zero warnings, zero errors** |
| `--decode-test` | **PASS** — 0 px escaped on all 3 malformed records; validator rejected 3 of 3; valid sprite still drew 844 px |
| `--genfail-test` | **PASS** *(after §E.6)* — normal seed 1→1 with 16 regions; 1 forced failure → seed **2**; 8 exhausted → seed **8**, 0 regions, 0 phantom placements |
| `keep_before.bmp` vs `keep_after.bmp` | **byte-identical** — no clamp fires on well-formed art |
| Full suite | **28 passed, 0 failed, 0 skipped in 346.0 s**, `$LASTEXITCODE` 0 |

`--genfail-test`'s degraded line reading `seed 8` is the arithmetic working: attempt 0 uses seed 1
and steps to 2, …, attempt 7 uses seed 8, and the final `attempt + 1 < GEN_RETRY_MAX` guard
correctly declines to step past it.

## E.6 Correction to §A.1.2 — the forcing hook was in the wrong place

**§A.1.2 places the `g_force_pathological` hook immediately after `world_gen`. That does not work,
and it fails silently in the worst possible way — as a green test.**

`castle_apply_layout`, `place_rivers` and `place_portal` all run *after* `world_gen` and all carve
open tiles. An all-solid grid written next to `world_gen` is therefore partly undone before the
flood scan ever sees it, so the scan finds a healthy component and the retry never fires. Measured:
with the hook where §A.1.2 puts it, all three cases ran to completion against a normal 16-region
world —

```
normal:    seed 1 -> 1, 16 regions, 0 entities at tile 0
FAIL  genfail: one forced failure landed on 1, expected 2
FAIL  genfail: exhausted retries did not take the degenerate path
degraded:  8 attempts exhausted -> 16 regions, 0 phantom placements, seed 1
```

The condition being simulated is "**the flood scan finds no open overworld component**", so the hook
has to make that true *at the scan*, not at generation. Moved to sit immediately after
`SDL_memset(seen, 0, sizeof(sc.seen))` and immediately before the Pass 1 comment — the last point
before the scan, where `solid[][]` is final for the attempt.

It is worth noting *why* this is worth this much prose: the misplaced hook produced a test that
printed `PASS` on its negative control and exercised none of the code it existed to cover. Had the
three positive assertions been any weaker, it would have shipped as coverage that proved nothing —
the exact failure mode invariant 6 exists to prevent.

The move is inside `#if WAYFARER_SELFTEST`, so it costs zero shipped bytes: the shipping binary
measured 924,160 B before and after it.

---

*Sources: [production-gap-analysis.md](production-gap-analysis.md) §1 SEC-1, §2 ERR-1, §2 ERR-3,
§4 QA-3, §4 QA-4, §6 Tier 1 items 3–4;
[architecture-summary.md](architecture-summary.md) §4.4, §4.5, §4.6, §5.3, §6.1, §7, §8.2, §9;
[tier1-items-1-2-implementation.md](tier1-items-1-2-implementation.md) §A.0, §B.2;
and `src/main.c` at the current working tree (14,114 lines), read directly for every anchor.*
