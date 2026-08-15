# Wayfarer — Production Readiness Gap Analysis

*Generated from a full inspection of the workspace at commit `3d14508` (branch `castle-fixed`), 2026-08-15.*
*Companion to [architecture-summary.md](architecture-summary.md).*

---

## 0. Scope, threat model, and how to read this

Wayfarer is an **offline, single-process, single-player native Windows game**. It has:

- no network stack, no sockets, no HTTP, no TLS, no CORS, no security headers
- no server, no database, no ORM, no query layer
- no authentication, no sessions, no multi-tenancy, no user accounts
- no third-party services, no telemetry, no crash reporting, no analytics
- exactly **one** external input surface at runtime (the 28-byte save file), plus `argv`
- exactly **one** third-party dependency (SDL2, statically linked, rebuilt with most subsystems removed)

The requested categories are therefore mapped onto their real equivalents. "Production" here means
**a contest submission running unattended on a judge's machine that the developer will never see** —
which is a genuinely demanding deployment target, because there is no way to observe a failure, no
way to ship a patch, and one bad first launch is the whole result.

| Requested category | What it means here |
|---|---|
| Unvalidated inputs | `argv` parsing, the save file, and the compiled-in art stream |
| Sanitisation | Bounds checking on decode paths and array indices |
| AuthN / AuthZ | Progression gates, the compile-time self-test gate, and the shipped `--dev` unlock |
| Security headers / CORS | *Not applicable* — replaced by the process's actual attack surface (§1.6) |
| Unhandled exceptions | C has none; read as **unchecked return values, degenerate states, and undefined behaviour** |
| Unhandled promise rejections | *Not applicable* — replaced by **cross-thread and asynchronous hazards** (§2.5) |
| Centralised error handler | Fatal-path handling in `main` and the total absence of user-facing error reporting |
| Raw stack traces to users | Replaced by: **silent exit with no diagnostic at all**, which is strictly worse |
| Structured logging | The shipping binary is `-mwindows` and has no stdout/stderr |
| Request tracing | Replaced by frame/tick/seed traceability |
| Health checks | Replaced by startup self-verification and build provenance |
| DB indexes / unpaginated queries | Per-tick full-grid scans and uncached per-frame recomputation |

### Severity key

| | Meaning |
|---|---|
| 🔴 **Critical** | Can plausibly cost the submission: silent unwinnable state, launch failure with no diagnostic, or a correctness proof that does not ship |
| 🟠 **High** | Real defect or real risk with a concrete failure mode; should be fixed before submission |
| 🟡 **Medium** | Genuine gap; costs debuggability, performance headroom, or confidence |
| 🟢 **Low** | Hygiene, polish, or documented trade-off worth recording |
| ✅ | Existing strength worth protecting from regression |

### Summary

| Category | 🔴 | 🟠 | 🟡 | 🟢 | ✅ |
|---|---|---|---|---|---|
| 1. Security & validation | 1 | 3 | 4 | 3 | 4 |
| 2. Error handling & resilience | 2 | 3 | 4 | 3 | 4 |
| 3. Observability & logging | 1 | 2 | 4 | 2 | 2 |
| 4. Testing & quality | 1 | 4 | 5 | 3 | 5 |
| 5. Performance & scalability | 1 | 2 | 5 | 3 | 3 |
| **Total** | **6** | **14** | **22** | **14** | **18** |

---

## 1. Security & Validation

### ✅ Existing strengths

| | Strength |
|---|---|
| S1 | **`game_load` is validation-first and exemplary.** Ten distinct checks (magic, reserved bytes ×2, version, restored mask ⊆ 19 bits, ability mask ⊆ 3 bits, shard mask ⊆ 8 bits, NaN-rejecting position, in-bounds position, collision-map position) all pass **before the live `Game` is touched**. The world is regenerated into `SDL_malloc`'d scratch and only `SDL_memcpy`'d over on success ([main.c:4351-4464](../src/main.c#L4351-L4464)). Six negative controls cover it |
| S2 | **The attack surface is genuinely tiny.** No sockets, no `getenv`, no registry, no `system()`/`exec`, no dynamic library loading, no user-supplied paths, no scripting, no deserialisation of anything but 28 fixed bytes |
| S3 | **Endianness and padding are handled by hand** (`save_put32/64`, `save_get32/64`, `SDL_memcpy` for float bit patterns) — no struct is ever `fwrite`n |
| S4 | **String formatting is uniformly bounded**: every `SDL_snprintf` passes `sizeof(dst)`; there is no `sprintf`, `strcpy`, `strcat`, or `gets` anywhere in the file |

---

### 🔴 SEC-1 — The baked-art RLE decoder is unvalidated in the shipping build

**Files**: [main.c:5486-5528](../src/main.c#L5486-L5528) (validator), [main.c:5568-5643](../src/main.c#L5568-L5643) (decoder)

`art_stream_ok()` and `art_stream_ok_sp()` — which fully validate every RLE stream against its
declared length, palette size and frame area — are inside `#if WAYFARER_SELFTEST`. They are called
only from `sprite_selftest()`. **The shipping binary never validates the art stream.**

Meanwhile `draw_sprite_ex()` decodes without bounds checks on the *source*:

```c
i = sp->data_off;
n = sp->data_off + sp->data_len;
while (i < n && y < sp->h) {
    unsigned int c = ART_DATA[i++];
    if (literal) { count = (c & 0x7Fu) + 1u; }
    else         { count = c + 1u; idx = ART_DATA[i++]; }   /* ← no check that i < n */
    for (k = 0; k < count && y < sp->h; k++) {
        unsigned char v = literal ? ART_DATA[i + k] : idx;  /* ← no check that i+k < n */
        ...
    }
    if (literal) i += count;
}
```

Two out-of-bounds reads of the const `ART_DATA[]` array are reachable if a bake ever emits a
truncated final record: (a) a RUN control byte as the last byte reads `ART_DATA[n]`; (b) a LITERAL
whose declared count runs past `n` reads up to 127 bytes beyond. Palette indices are likewise
unchecked against `sp->pal_n`, so `pal[v]` can read past the 64-entry stack array `Uint32 pal[ART_PAL_MAX]`
— **a stack read overflow**, not merely a `.rdata` over-read.

**Failure scenario**: `bake.ps1` is re-run after an art change, hits an encoding edge case (an
oversized sprite, a `System.Drawing` decode quirk, a truncated write), and emits a header that
compiles fine. `--sprite-test` catches it — *in the binary nobody ships*. If the developer rebuilds
release without re-running the self-test binary first, the shipped game reads out of bounds on a
sprite draw. On Windows this most likely renders garbage pixels; it can also fault.

**Why this is Critical**: the guarantee exists, is correct, and *does not ship*. The entire
"structural guarantee" argument for the two-binary model depends on the shipping build never needing
the check — but the check is the only thing standing between a bad bake and undefined behaviour.

**Fix (cheap, ~30 shipped bytes)**: make the decoder self-limiting rather than adding a runtime
validator. Three clamps inside the existing loop:

```c
if (!literal) { if (i >= n) break; idx = ART_DATA[i++]; }
if (literal && i + count > n) count = n - i;
...
unsigned char v = literal ? ART_DATA[i + k] : idx;
if (v > sp->pal_n) v = 0;            /* treat as transparent */
```

This costs a handful of bytes, makes malformed data draw a hole instead of reading out of bounds,
and is testable by pointing `art_stream_ok`'s negative control at the decoder.

---

### 🟠 SEC-2 — `--scale` is unclamped and can overflow a signed int

**File**: [main.c:13626-13630](../src/main.c#L13626-L13630)

```c
scale = arg_int(argc, argv, "--scale", 0);
if (scale < 1) scale = pick_scale();
win = SDL_CreateWindow("Wayfarer", ..., LOGICAL_W * scale, LOGICAL_H * scale, SDL_WINDOW_SHOWN);
```

`arg_int` → `SDL_atoi`, which can return any `int`. `LOGICAL_W * scale` is `960 * scale` in `int`
arithmetic, which **overflows (undefined behaviour) for `scale > 2,236,962`**. Below that, a large
value simply fails `SDL_CreateWindow` and the process exits `2` with no message.

**Fix**: `if (scale < 1) scale = pick_scale(); else if (scale > WIN_SCALE_MAX) scale = WIN_SCALE_MAX;`
— one line, and `WIN_SCALE_MAX` already exists.

---

### 🟠 SEC-3 — The save file has no integrity check

**File**: [main.c:4299-4339](../src/main.c#L4299-L4339)

The 28-byte format has magic, version and reserved bytes but **no checksum or CRC**. Every field is
individually range-checked on load, which bounds the blast radius well — but a bit flip inside the
8-byte seed passes every check and silently loads *a completely different world*, with the player's
restored mask and position applied to terrain that never produced them. The position is validated
against the regenerated map, so this usually manifests as a rejected load (good) — but on a seed
where the position happens to be walkable, the player lands in a world where her restored fragments
do not exist.

**Fix**: a 1-byte XOR or 2-byte Fletcher checksum over bytes 0..23, stored in the currently-unused
reserved byte at offset 3 or 23, bumped to `SAVE_VERSION 3`. The version-tolerant load path
([main.c:4376-4382](../src/main.c#L4376-L4382)) already demonstrates the pattern.

---

### 🟠 SEC-4 — Save writes are not atomic

**File**: [main.c:4332-4338](../src/main.c#L4332-L4338)

```c
rw = SDL_RWFromFile(path, "wb");   /* truncates immediately */
if (!rw) return -1;
i = (SDL_RWwrite(rw, buf, 1, SAVE_SIZE) == SAVE_SIZE) ? 0 : -1;
```

Opening `"wb"` truncates the existing save to zero bytes *before* the new bytes are written. A crash,
power loss, or full disk between those two calls destroys the previous save and leaves a 0–27 byte
file. The load path correctly rejects it (short read → `-1` → `"[no save]"`), so there is no
corruption — but **the player's progress is silently gone**, and the only signal is a title-bar note
they may not read.

Also: `SAVE_FILENAME` is the bare relative path `"wayfarer.sav"`, resolved against the process CWD.
Launching via `Wayfarer-DEV.bat` (which uses `start "" "%DIR%wayfarer.exe"`) or a desktop shortcut
gives a CWD that is not necessarily the binary's directory, so saves can land somewhere the player
cannot find them — and if that directory is not writable, `SDL_RWFromFile` returns NULL and the game
shows `"[save failed]"` with no explanation.

**Fix**: write to `wayfarer.sav.tmp`, close, then rename over the target. On Windows,
`MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)` is atomic; SDL has no rename wrapper, so this needs one
Win32 call or a `remove` + `rename` pair. Optionally resolve the path against `SDL_GetBasePath()` or
`SDL_GetPrefPath()`.

---

### 🟡 SEC-5 — `arg_val` will happily consume the next flag as a value

**File**: [main.c:1145-1152](../src/main.c#L1145-L1152)

```c
static const char *arg_val(int argc, char **argv, const char *key)
{
    for (i = 1; i + 1 < argc; i++)
        if (SDL_strcmp(argv[i], key) == 0)
            return argv[i + 1];   /* no check that argv[i+1] is not itself a flag */
    return NULL;
}
```

`--shot --dev` writes a BMP to a file literally named `--dev`. `--seed --frames 90` yields
`SDL_atoi("--frames")` = 0, i.e. seed 0, silently. This is self-test-only for the path-taking flags,
but `--seed`, `--frames` and `--scale` are shipping flags and all take the same route.

**Note**: the `--castle` handling ([main.c:13761-13775](../src/main.c#L13761-L13775)) *relies* on
this behaviour — a bare `--castle` followed by another flag falls back to 0 because
`SDL_atoi("--dev")` is 0. Any fix must preserve that, or make it explicit.

---

### 🟡 SEC-6 — `--frames` accepts negative values with surprising effects

**File**: [main.c:13415](../src/main.c#L13415), [main.c:14027](../src/main.c#L14027)

`limit = arg_int(argc, argv, "--frames", 0)` is not clamped. With `--frames -1`, the terminating
condition `if (limit && frame >= limit)` is `(-1 && 0 >= -1)` → **true on frame 0**, so the game
exits after a single frame. In the self-test build the screenshot guard
`if (shot && limit && frame + 1 >= limit)` is also true, so `--shot` fires on every frame.

**Fix**: `if (limit < 0) limit = 0;`

---

### 🟡 SEC-7 — The shipping binary contains a full progression-unlock switch

**Files**: [main.c:13672-13678](../src/main.c#L13672-L13678) (`--dev`/`--developer`/`--dev-mode`),
[main.c:13834-13841](../src/main.c#L13834-L13841) (F12), [main.c:3431-3449](../src/main.c#L3431-L3449) (`dev_unlock_all`)

`dev_unlock_all()` grants all three abilities, opens the Aetherhold causeway, satisfies the shard
requirement, sets every region's restoration to 1.0, and reveals all 25,748 tiles. It is reachable
in the **shipping** build from both the command line and a single keypress, with a HUD toast
announcing it.

This is almost certainly intentional (the repo ships `Wayfarer-DEV.bat` and a `.lnk` for exactly
this), and for a contest judged on "finished → under size → fun" it is arguably a feature. It is
recorded here only so the decision is explicit rather than incidental: a judge who presses F12 while
hunting for fullscreen skips the entire game.

**Options**: leave as-is (document it in the submission notes), require a modifier (`Ctrl+Shift+F12`),
or gate `--dev` behind a build flag while keeping F12.

---

### 🟡 SEC-8 — Save seed field is 64-bit but the CLI can only express 32

**Files**: [main.c:13425](../src/main.c#L13425), [main.c:4321](../src/main.c#L4321)

`Game.seed` is `Uint64` throughout and the save format stores all 8 bytes, but `--seed N` goes
through `SDL_atoi` (an `int`). A save produced by pressing `R` many times past `INT_MAX`, or a
hand-edited save, holds a seed that cannot be reproduced from the command line. The comment at
[main.c:13422-13424](../src/main.c#L13422-L13424) acknowledges this. Consequence: the "shareable
seed" shown in the HUD is not always shareable.

---

### 🟢 SEC-9 — No build provenance in the binary

There is no `--version`, no embedded build hash, and no Windows VERSIONINFO resource. A judge
reporting a problem cannot say which build they ran, and neither can the developer. A single
`#define WAYFARER_BUILD "3d14508"` in the title bar costs ~20 bytes.

### 🟢 SEC-10 — `.gitignore` correctly excludes a leaked-credential file

[.gitignore:33-36](../.gitignore) explicitly excludes `opencode.json`, noted as having contained
plaintext API keys from a different agent tool run against this working tree on 2026-08-06. Worth
confirming it never entered history:

```powershell
git log --all --full-history -- opencode.json
```

If it did, the keys must be rotated regardless of subsequent deletion.

### 🟢 SEC-11 — Repository visibility is an open submission blocker

The README lists "Repo visibility unconfirmed against the contest's requirement (currently private)"
as blocker #3. Not a code issue, but it is a submission-blocking item that belongs on the same list.

---

## 2. Error Handling & Resilience

### ✅ Existing strengths

| | Strength |
|---|---|
| R1 | **Audio failure is a designed degraded mode.** `SDL_Init(SDL_INIT_VIDEO)` and `SDL_InitSubSystem(SDL_INIT_AUDIO)` are deliberately separate calls, because a combined request fails if *any* subsystem fails. Verified with a bogus `SDL_AUDIODRIVER` ([main.c:13618-13664](../src/main.c#L13618-L13664)). Silence is acceptable; failing to launch is not |
| R2 | **Backbuffer failure is a designed degraded mode.** `backbuffer_new` returning NULL makes the renderer draw straight into the window surface at native resolution ([main.c:7841-7863](../src/main.c#L7841-L7863)) |
| R3 | **The window surface is re-fetched every frame** because it is invalidated on resize ([main.c:13911](../src/main.c#L13911)) |
| R4 | **The fixed-step accumulator is clamped** to 0.25 s, so a breakpoint or window drag cannot spiral the catch-up loop ([main.c:13901-13902](../src/main.c#L13901-L13902)) |

---

### 🔴 ERR-1 — The pathological-seed path silently produces an unwinnable game

**File**: [main.c:4159-4168](../src/main.c#L4159-L4168)

```c
if (biggest_first < 0) { /* pathological seed: carve rather than trap */
    g->w.solid[WORLD_H / 2][WORLD_W / 2] = 0;
    g->p.x = ...; g->p.y = ...;
    g->w.region_count = 0;
    g->w.spawn_region = -1;
    return 1;
}
```

If no open overworld component exists, `game_init` carves a single tile, sets `region_count = 0`,
and **returns**. It never runs `regions_build`, `world_place_and_verify`, `place_entities` or
`world_heights_all`. The resulting game has:

- zero regions, so `world_stage()` returns 0 ("Unexplored") forever
- zero placed entities (`ents[]` is all-zero from `SDL_zero`, so every `tile` is **0**, not `-1`)
- `game_complete()` = `frags_restored + souls_restored >= 19` → **permanently false**

The player stands on one carved tile inside solid rock, in a world that cannot be completed, with no
indication anything is wrong. Worse, because `SDL_zero(*g)` leaves `ents[i].tile == 0` rather than
`-1`, `entity_in_reach` and `mm_draw` treat all 19 entities as *placed at tile 0* — the top-left
corner of the map — rather than as unplaced.

**Every call site discards the return value**: `(void)game_init(&game, &rngs)` appears at
[main.c:13666](../src/main.c#L13666), [main.c:13853](../src/main.c#L13853),
[main.c:4407](../src/main.c#L4407) (inside `game_load`), and [main.c:7713](../src/main.c#L7713)
(`render_grid`). Nothing anywhere checks it.

**Failure scenario**: a judge presses `R` a few times, lands on a pathological seed, and plays a
world that can never be won. There is no test that forces this branch, so nobody knows how likely it
is.

**Fixes** (in order of cost):
1. Initialise `ents[i].tile = -1` in the early-return path (2 lines) — removes the phantom-entity
   half of the bug immediately.
2. Have `game_init` retry with `seed + 1` (up to a small bound) instead of returning a degenerate
   world. Generation is already 17 ms, and `R` already reseeds; this is the same mechanism.
3. Add a self-test that constructs the condition directly (force `world_gen` to produce an all-solid
   overworld) and asserts the recovery behaviour — this branch currently has **zero** coverage.

---

### 🟢 ERR-2 — RESOLVED — Every fatal startup path exits silently with no user-visible diagnostic

> **Resolved** on branch `castle-fixed`. A `fatal()` helper
> ([main.c:13433](../src/main.c#L13433)) now sits on all four paths: it reads `SDL_GetError()` *before*
> any teardown call, shows an `SDL_ShowSimpleMessageBox` in the shipping build — falling back to Win32
> `MessageBoxA` when SDL cannot — writes `FATAL n:` to stderr in the self-test build, which runs
> unattended, and returns its `code` argument unchanged so the 1/2/3/4 exit-code contract is
> untouched. **Measured cost: +1,024 bytes total** — the estimate below feared 4–10 KB of
> newly-retained SDL message-box objects; `--gc-sections` had evidently been keeping them already, and
> the `MessageBoxA` fallback added 0 further bytes. The three *degraded-mode* failures (audio
> subsystem, `backbuffer_new`, `game_save`) were deliberately left silent — see the note at the end of
> this entry.
>
> This also closes **ERR-12** and partially closes **OBS-1**.

**File** (original): [main.c:13623-13634](../src/main.c#L13623-L13634), [main.c:13912-13918](../src/main.c#L13912-L13918)

```c
if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
win = SDL_CreateWindow(...);
if (!win) { SDL_Quit(); return 2; }
...
if (!fb || fb->format->BytesPerPixel != 4) { ...; return fb ? 4 : 3; }
```

The shipping binary is linked `-mwindows`. It has **no console, no stdout, no stderr**. On any of
these four paths the process simply vanishes: double-click, nothing happens, no window, no message,
no log. `SDL_GetError()` is never read, anywhere in the file.

This intersects directly with the README's own submission blocker #2 — *"Never smoke-tested on a
second machine"*. If the binary fails to start on a judge's machine, the failure is completely
undiagnosable, by them or by anyone.

**Fix (~200–400 shipped bytes, the single highest-value spend in this document)**:

```c
static int fatal(const char *what) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wayfarer", what, NULL);
    return 1;
}
...
if (SDL_Init(SDL_INIT_VIDEO) != 0) return fatal(SDL_GetError());
```

`SDL_ShowSimpleMessageBox` works with a NULL window and is already linked (it is part of the video
subsystem that is compiled in). With 516 KB of headroom, this is affordable several hundred times
over.

**As shipped**, the helper differs from the sketch above in three ways that turned out to matter:

1. It takes the **exit code as a parameter and returns it unchanged**, rather than hardcoding `1`.
   The 1/2/3/4 contract is published in architecture-summary §8.4; the dialog is added *alongside* it.
2. It calls `SDL_GetError()` **before** `SDL_Quit` / `SDL_CloseAudioDevice`, which can clear or
   overwrite the per-thread error string. Reading it after teardown yields a blank message.
3. It writes to **stderr under `#if WAYFARER_SELFTEST`** instead of showing a modal. That build is
   `-mconsole` and now runs unattended under `tools/run-tests.ps1`; a modal box there would not fail
   the suite, it would hang it indefinitely.

**Verified**: a probe compiled against the same `SDL2-min` static lib, calling
`SDL_ShowSimpleMessageBox` with a `NULL` parent and **no** prior `SDL_Init`, produces a real Win32
dialog (window class `#32770`, title `Wayfarer`) and returns `0`. The message-box path is present and
working in the cut-down SDL2, which was the one way this change could have failed silently.

As shipped, `fatal()` uses **two** channels in the shipping build — `SDL_ShowSimpleMessageBox` first,
Win32 `MessageBoxA` if that returns non-zero. The reason is in the box below.

> **Exit path 1 needed a second channel, and now has one.** SDL routes `SDL_ShowSimpleMessageBox`
> through the video subsystem, initialising it on demand. When that initialisation is what failed,
> the box cannot be shown: with `SDL_VIDEODRIVER=nonexistent` the call returns `-1` and displays
> **nothing**. The path *most* likely to fire on a judge's machine was the path least able to report
> itself.
>
> `fatal()` now falls back to a raw Win32 `MessageBoxA` whenever `SDL_ShowSimpleMessageBox` returns
> non-zero. `MessageBoxA` is pure `user32` — already linked — and depends on no part of SDL, so it
> survives exactly the case SDL cannot. **Cost: 0 bytes**; it fit inside existing alignment padding,
> leaving the shipping binary at 924,672. `MessageBoxA` rather than `MessageBoxW` because the
> formatted buffer is `char[256]`; the prototype is declared by hand rather than pulling
> `<windows.h>` and its several hundred macros into a 14,000-line translation unit.
>
> **Verified**: `SDL_VIDEODRIVER=nonexistent` on the shipping binary now yields a native dialog
> (window class `#32770`, title `Wayfarer Fatal Error`) and exit code `1`. All four exit paths are
> now covered under a broken video driver.

**Deliberately still silent** — these are designed degraded modes, and a dialog on any of them would
convert a resilience feature into a launch-blocking popup:

| Path | Why it stays silent |
|---|---|
| `SDL_InitSubSystem(SDL_INIT_AUDIO)` fails | Audio is split from video *specifically* so a machine with no sound device still launches (R1). Silence is the feature |
| `backbuffer_new()` returns NULL | The renderer falls back to the window surface at native resolution (R2). The game is fully playable |
| `SDL_RWFromFile` fails in `game_save` | Already reported in the title bar as `[save failed]`. That path's real fix is SEC-4 |

---

### 🟠 ERR-3 — `game_init`'s return value is meaningful and universally discarded

Covered under ERR-1, but worth stating separately: `game_init` returns the count of open tiles
reachable from spawn — a genuinely useful health signal — and all four call sites cast it to `void`.
A world with, say, 40 reachable tiles is technically completable but unplayable; nothing notices.

Compare `--land-test`, which *does* check this (a 50% open-tile reach bar) and currently fails seeds
85, 417 and 430 at 22–23%. **Those three seeds are reachable in real play via `R` or `--seed`, and
the shipping game will happily run them.**

**Fix**: check the return in `main` and `render_grid`; on a low value, regenerate with the next seed.

---

### 🟠 ERR-4 — `world_place_and_verify`'s total-ungate fallback ships an unverified world shape

**File**: [main.c:3097-3101](../src/main.c#L3097-L3101)

```c
for (i = 0; i < w->region_count; i++)
    w->regions[i].terrain = TERRAIN_NORMAL;
place_entities(...); place_shards(...);
return -100;
```

The final fallback strips **all** terrain gating and returns `-100` **without calling
`world_solvable` at all**. Every other path in that function verifies before returning. This path is
reached only after 64 gated attempts and 16 progressive ungating passes have all failed, so it is
rare — but "rare and unverified" is exactly the shape of a submission-day surprise, and the ungated
world it produces is one where the three abilities gate nothing (the state `--gating-test`'s own
negative control exists to reject).

`Game.gen_attempts` records `-100` faithfully, and nothing reads `gen_attempts` anywhere.

**Fix**: assert (or at minimum record and surface) solvability on this path too. With everything
ungated, `world_solvable` should be trivially satisfiable — so if it *isn't*, that is worth knowing.

---

### 🟠 ERR-5 — No runtime diagnostics exist in the shipping build at all

`art_stream_ok` (SEC-1), the `Perf` counters, `perf_report`, the F3 tuning overlay, and every
`printf` in the file are inside `#if WAYFARER_SELFTEST` / `#if WAYFARER_PERF`. The shipping binary
carries **zero** assertions, zero instrumentation, and zero error reporting.

This is a deliberate and well-argued trade (see the two-binary rationale in the README), and for byte
budget it was the right call at 1.09 MB. At **923,648 bytes with 516 KB of headroom**, the balance
has changed: the cost of a message box on four fatal paths is ~0.05% of remaining headroom, and it
converts "the game didn't start" from an unanswerable question into a one-line bug report.

---

### 🟡 ERR-6 — Unchecked SDL return values

| Call | Site | Consequence if it fails |
|---|---|---|
| `SDL_SetWindowFullscreen` | [main.c:13831](../src/main.c#L13831) | `fullscreen` flag desynchronises from actual state; the next F11 toggles the wrong way |
| `SDL_UpdateWindowSurface` | [main.c:7878](../src/main.c#L7878) | Frame silently not presented |
| `SDL_BlitSurface` (minimap) | [main.c:4798](../src/main.c#L4798) | Minimap silently absent |
| `SDL_SaveBMP` | [main.c:13963](../src/main.c#L13963), 8631, 8643, 11914, 12090 | **Self-test only** — a capture script "succeeds" with no file written |
| `fopen` (`--dump`) | [main.c:13099](../src/main.c#L13099) | Self-test only |

None are fatal. The `SDL_SaveBMP` cases are the most annoying in practice, because a screenshot
workflow that silently writes nothing wastes a whole verification pass.

### 🟡 ERR-7 — `hud` is file-scope mutable state with a format-bound cached surface

**File**: [main.c:4727-4734](../src/main.c#L4727-L4734), [main.c:4787-4792](../src/main.c#L4787-L4792)

```c
static struct { char toast[64]; int toast_left, win_left, win_shown, mm_dirty, mm_tick;
                SDL_Surface *mm; } hud;
```

`hud.mm` is created lazily against **the first framebuffer format it ever sees** and never recreated:

```c
if (!hud.mm)
    hud.mm = SDL_CreateRGBSurface(0, MM_W, MM_H, fb->format->BitsPerPixel, ...Rmask...);
```

F11 fullscreen replaces the window surface, and the backbuffer is allocated once at startup from the
*pre-fullscreen* window format. In practice these match on Windows; if they ever do not, the minimap
blits with wrong channel masks (colour-swapped) and nothing detects it. It is also the one piece of
global mutable state shared between the game loop and the self-test capture paths.

### 🟡 ERR-8 — `game_load` calls `castle_apply_layout` redundantly

**File**: [main.c:4417-4428](../src/main.c#L4417-L4428)

`apply_restore(tmp, i)` already calls `castle_apply_layout(&g->w, 1)` + `castle_apply_heights` when
`i == WELL_SOUL_IDX` ([main.c:3385-3396](../src/main.c#L3385-L3396)), and `game_load` then calls
`castle_apply_layout` again eleven lines later. Idempotent, so harmless — but it is exactly the kind
of duplicated call ordering that produced defects #1 and #4 in the Aetherhold rebuild (the wrong-order
`world_heights`/`castle_apply_heights` pair, and the `apply_restore` path that never recomputed
heights). Worth collapsing while the history is fresh.

### 🟡 ERR-9 — The compile-time stack guard has only 6.5% margin, and covers less than the real frame

**File**: [main.c:1410-1411](../src/main.c#L1410-L1411)

```c
typedef char wayfarer_stack_guard[(sizeof(World) + sizeof(Scratch) < 700 * 1024) ? 1 : -1];
```

Measured: `World` ≈ 309,800 B + `Scratch` ≈ 360,472 B = **670,272 B** against a 716,800 B limit —
**46,528 B (6.5%) of margin**. Growing `WORLD_H` by ~7 rows fails the build, which is the intended
behaviour and a genuine strength.

Two caveats: (a) the guard bounds those two structs, not the *deepest actual frame* — `world_heights`
adds its own `Uint8 dist[157][164]` (25,748 B) on top, and several self-tests declare `World`,
`Scratch` and further locals together; (b) MinGW's 2 MB default stack is a link-time property that
nothing in the build asserts.

**Fix**: raise the guard's coverage to include the largest known additional frame, or pass
`-Wl,--stack,4194304` to buy explicit headroom (costs zero file bytes — it is a PE header field).

### 🟢 ERR-10 — `render_grid` leaves a partially-drawn grid on allocation failure

[main.c:7702-7703](../src/main.c#L7702-L7703): `if (!g) return;` mid-loop leaves some thumbnails
drawn and the rest as background. Cosmetic, self-recovering on the next dirty repaint.

### 🟢 ERR-11 — `camera_follow`'s ease is per-frame, not per-tick

[main.c:7782-7786](../src/main.c#L7782-L7786) documents this: the ease constant is applied once per
*frame*, so the effective time constant drifts on a machine that cannot hold `FRAME_HZ`. Explicitly
acknowledged and accepted; recorded here so it is not rediscovered as a bug.

### 🟢 ERR-12 — RESOLVED — No `SDL_GetError()` call exists anywhere in the codebase

A corollary of ERR-2, listed separately because it is a one-word grep and a permanent blind spot:
SDL reports *why* every one of its failures happened, and Wayfarer never asks.

> **Resolved** by the same change. `fatal()` ([main.c:13433](../src/main.c#L13433)) reads
> `SDL_GetError()` and puts the result in front of the user. The grep is no longer empty — though it
> still returns exactly one hit, so the blind spot is *narrowed*, not eliminated: every non-fatal SDL
> call still discards its reason.

---

## 3. Observability & Logging

### ✅ Existing strengths

| | Strength |
|---|---|
| O1 | **Seed-based traceability is excellent.** Every world is a pure function of `--seed N`; the seed is shown in the window title *and* in the HUD, so any visual report can be reproduced exactly |
| O2 | **The self-test binary's instrumentation is thorough**: `Perf` tracks render/present/sleep/frame separately (the README notes this exists precisely because a single "~56 fps" number conflated all four and proved nothing), and `audio_selftest` reports worst-case callback duration against the deadline |

---

### 🟠 OBS-1 — PARTIALLY RESOLVED — The shipping binary has no output channel of any kind

> **Partially resolved.** The shipping binary now has exactly one output channel: the `fatal()`
> message box (ERR-2), on the four fatal paths only. The "if the game fails before the window exists,
> there is nothing" case below is closed. Everything else in this entry still stands — there is no
> log, and a game that *misbehaves* rather than dying still reports only through the title bar. The
> `--log <file>` proposal remains open as Tier 3 item 14.

Linked `-mwindows`: no stdout, no stderr, no console, no log file, no event-log entry, no message
box. `printf` appears **only** under `#if WAYFARER_SELFTEST`.

The complete set of runtime signals available to a player or judge is:

| Channel | Content |
|---|---|
| Window title | seed, `fragments N/14`, `souls N/5`, world stage, `[F1 overlay]`, `[F2 grid]`, and a save/load note |
| HUD | counters, minimap, restore toasts (3 s), win banner (6 s) |

If the game fails before the window exists, there is nothing. If it misbehaves after, there is a
title bar. This is the observability posture of the artefact that will be judged.

*(The first sentence no longer holds: a fatal pre-window failure now raises a message box naming the
subsystem and quoting `SDL_GetError()`. The second still does.)*

**Minimum viable fix** — *done*: the four `fatal()` message boxes from ERR-2. **Better**: an opt-in
`--log <file>` that appends one line per session (build id, seed, resolution, scale, audio device
rate, exit reason) — perhaps 300 bytes, and it makes a second-machine smoke test *reportable* rather
than merely *attempted*.

---

### 🟠 OBS-2 — No health check or startup self-verification in the shipping build

There is no equivalent of a readiness probe: nothing verifies at startup that the art table is
intact (SEC-1), that the audio device opened, that the backbuffer allocated, or that generation
produced a sane world (ERR-1, ERR-3). All four of those have degraded modes; none is reported.

**Fix**: a `--selfcheck` flag on the shipping binary that runs the handful of cheap invariants
(art stream validity across all 138 sprites, one world generation, `world_solvable`) and reports via
message box + exit code. This is the one piece of test machinery worth shipping, because it is the
only thing that can be run *on the machine where the problem is*.

### 🟠 OBS-3 — Performance is unmeasurable on any machine but the developer's

`WAYFARER_PERF` defaults to `WAYFARER_SELFTEST` ([main.c:33-35](../src/main.c#L33-L35)), so the
shipping binary carries no frame timing. The README's frame-time evidence ("900-frame captures at
the courtyard, the keep terrace and the spawn point all sit on the 60 fps cap") was measured with the
self-test binary on one machine. Both binaries run the identical render path, which makes the numbers
*transferable* — but only across builds, not across hardware. A judge on an older integrated GPU
cannot report a frame rate, and neither can the developer ask for one.

### 🟡 OBS-4 — No structured logging concept exists at all

Even the self-test output is unstructured `printf` prose (`"land : 30 seeds, 33-38% open, both
negative controls fire"`). It is highly readable by a human and completely unparseable by a script,
which is a large part of why there is no CI (§4). A `--format=tsv` or one-line-per-seed mode would
make the suite machine-checkable without changing a single assertion.

### 🟡 OBS-5 — No frame/tick correlation identifier

`frame` is a local in `main` and `Game.clock` is render-only. Neither is exposed in the title bar or
HUD, so "it glitched about ten seconds in" cannot be tied to a tick number or a `--frames N` value
for reproduction. Adding `frame` to the `--perf` title string (self-test) and to a future log line
costs nothing.

### 🟡 OBS-6 — Save files carry no provenance

No timestamp, no playtime, no build id, no world-stage summary. `game_load` cannot distinguish "a
save from this build" from "a save from three commits ago that happens to share a version byte". The
`SAVE_VERSION` bump is the only guard, and it only moves when the *format* changes, not when
generation does — so a generation change silently invalidates every existing save while the version
byte still matches. **This is a live hazard**: any change to `world_gen`, `place_entities`, or the
RNG stream ordering makes old saves load a different world at the same seed, passing every check.

**Fix**: include a `GEN_VERSION` byte in the save, bumped whenever generation changes, separate from
`SAVE_VERSION`.

### 🟢 OBS-7 — No crash handler

No `SetUnhandledExceptionFilter`, no minidump. For a submission this is defensible; recorded for
completeness.

### 🟢 OBS-8 — `build/.last_size` is the only build-history artefact

It holds one integer and is gitignored along with the rest of `build/`, so the size trajectory that
`build.ps1` prints (`delta +0 bytes (prev 923,648)`) is lost on any clean checkout. A committed
`docs/size-history.tsv` appended by `build.ps1` would preserve the one metric this project cares
about most.

---

## 4. Testing & Quality

### ✅ Existing strengths — this is the strongest area of the project

| | Strength |
|---|---|
| T1 | **25 self-test entry points**, each with a **negative control** — a deliberately broken case the check must reject. The stated rule ("a checker that has never rejected anything is assumed to prove nothing") is followed consistently, including structural ones like `--gating-test`'s "gating is decorative" check and `--aether-test`'s use of the old hardcoded rectangles as a control |
| T2 | **Headless full playthroughs**: `--play-test --seeds 50` runs 50 complete games to completion through the real `sim_step`/`try_interact` code, not a model of it |
| T3 | **Batch seeded testing** is the norm — 20 to 200 seeds per invariant, with the stated rule "batch-test at least 20 seeds after any change to generation or placement" |
| T4 | **Tests drive real code, not copies.** `input_poll` is called the single source of truth for key mapping specifically so tests exercise what the game runs; `--rebuild-test` was strengthened to assert through the actual render path rather than a pure predicate |
| T5 | **Determinism is tested, not assumed**: `--rng-test` (reproducibility, stream independence, modulo bias), `--audio-test` (two fresh states produce bit-identical 96,000-sample streams), `move_determinism_test` |

---

### 🟠 QA-1 — RESOLVED (runner), STILL OPEN (CI service) — the suite was unenforced

> **Resolved** by [tools/run-tests.ps1](../tools/run-tests.ps1). One command builds whatever is stale,
> runs all 25 self-tests plus a 26th size-budget assertion, times each one, prints a slowest-first
> summary, and exits with the **number of failing tests** (`0` = all green, `99` = the build failed so
> nothing ran). Costs zero shipped bytes — every test already returned a correct exit code, so
> `src/main.c` was not touched.
>
> Beyond the sketch below it also auto-rebuilds on staleness against **both** `src/main.c` and
> `src/art_data.h` (QA-12 — a re-bake changes only the header, and `build/` is never cleaned), times
> the suite (QA-7), and asserts the size budget (QA-13). `-Quick` skips the two long batch tests and
> says so in the banner; `-Filter` runs a subset; `-NoBuild` runs whatever binaries exist.
>
> **Still open**: there is no CI *service*. Nothing runs on push — invoking the runner is a manual
> act, so a commit with a failing test can still be pushed. That is the residual half of this entry.
> Also note the runner is green while **QA-2** (seeds 85/417/430) is unfixed, because those seeds sit
> past the documented `--seeds 30` window; the script carries a comment naming them so the gap stays
> visible in the file that would otherwise imply full coverage.

There is no `.github/`, no `.gitlab-ci.yml`, no `Makefile`, and **no script that runs the test
suite**. All 25 invocations are hand-typed from the README, one at a time, in whatever subset the
developer remembers.

Consequences:

- "25/25 self-tests green" in the README is a hand-recorded claim about a past moment, not a checked
  property of the current commit. It cannot be verified without ~5 minutes of manual invocation.
- A commit can be pushed with a failing test and nothing notices.
- `build.ps1 -SelfTest` produces the test binary but never runs anything.
- The gap between "the tests exist and are excellent" and "the tests are enforced" is one PowerShell
  script.

**Fix (highest value-per-hour item in this document)** — `tools/run-tests.ps1`:

```powershell
$e = ".\build\wayfarer-selftest.exe"
$tests = @(
  @('--iso-test'), @('--font-test'), @('--fog-test'), @('--sprite-test'),
  @('--fade-test'), @('--rebuild-test'), @('--ground-test'), @('--motion-test'),
  @('--rng-test','--seed','1'), @('--save-test'), @('--hud-test'),
  @('--land-test','--seeds','30','--seed','1'), @('--village-test','--seeds','30','--seed','1'),
  @('--move-test','--seeds','20','--seed','1'), @('--region-test','--seeds','30','--seed','1'),
  @('--reach-test','--seeds','50','--seed','1'), @('--bridge-test','--seeds','200','--seed','1'),
  @('--sector-test','--seeds','30','--seed','1'), @('--portal-test','--seeds','30','--seed','1'),
  @('--shard-test','--seeds','30','--seed','1'), @('--gating-test','--seeds','30','--seed','1'),
  @('--aether-test','--seeds','20'), @('--path-test','--seeds','20'),
  @('--play-test','--seeds','50','--seed','1'), @('--audio-test','3000','--sfx')
)
$fail = 0
foreach ($t in $tests) {
  & $e @t | Out-Host
  if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: $($t -join ' ')" -ForegroundColor Red; $fail++ }
}
exit $fail
```

Every test already returns a correct exit code, so this works today with no changes to `main.c`.
Even without a CI service, one command that returns a single pass/fail makes the claim checkable.

---

### 🟠 QA-2 — Three known-failing seeds are outside the range the suite normally runs

`--land-test --seeds 500` fails seeds **85, 417, 430** — the player reaches only 22–23% of open
tiles against a 50% bar. The README documents this as pre-existing and not a regression.

The problem is not the defect, it is the **detection geometry**: the documented invocation is
`--land-test --seeds 30 --seed 1`, which covers seeds 1–30. All three failures are past seed 30, so
the suite as normally run is green while a known generation-quality defect exists. As the README
itself notes, this is "worth fixing before trusting the suite to catch a fresh regression".

These seeds are reachable in real play: `--seed 85` or ~85 presses of `R`.

**Fix**: either fix the generator, or add the three known-bad seeds to the default `--land-test`
batch as explicit regression cases (`--seeds 30` plus a hardcoded `{85, 417, 430}` tail), so the
count of failures is *asserted* rather than remembered.

### 🟠 QA-3 — Uncovered critical paths

| Path | Site | Why it matters |
|---|---|---|
| `game_init` pathological-seed early return | [main.c:4159-4168](../src/main.c#L4159-L4168) | **Entire failure branch, zero coverage.** Produces a silently unwinnable game (ERR-1) |
| `world_place_and_verify` total-ungate (`return -100`) | [main.c:3097-3101](../src/main.c#L3097-L3101) | Returns without verifying solvability (ERR-4); no test asserts what this world looks like |
| `arg_val` / `arg_int` / `arg_flag` | [main.c:1143-1167](../src/main.c#L1143-L1167) | No test at all. SEC-2, SEC-5 and SEC-6 are all in untested code that every launch runs |
| `blit_scale` at non-integer window ratios; F11 fullscreen | [main.c:4931](../src/main.c#L4931), [main.c:7870](../src/main.c#L7870) | `--font-test`/`--hud-test` render at logical size; the *upscale and present* path is exercised only by hand |
| `backbuffer_new` returning NULL (degraded mode) | [main.c:7848](../src/main.c#L7848) | A documented degraded mode that no test forces |
| `mm_draw` surface-format mismatch | [main.c:4787](../src/main.c#L4787) | ERR-7; untested |
| `draw_sprite_ex` against a malformed stream | [main.c:5568](../src/main.c#L5568) | `art_stream_ok` validates the *data*; nothing tests that the *decoder* survives bad data (SEC-1) |
| Save/load across a generation change | [main.c:4351](../src/main.c#L4351) | OBS-6: a v2 save from an older generator loads silently into a different world |
| v1→v2 save upgrade with `has_castle_key` implied by the restored mask | [main.c:4392, 4427](../src/main.c#L4392) | The upgrade path exists (`if (restored & (1u << WELL_SOUL_IDX)) has_castle_key = 1`) and has no dedicated control |

### 🟠 QA-4 — No sanitiser or memory-tooling pass

The build is `-Os -Wall -Wextra` with zero warnings — genuinely good, and worth protecting. But there
is no documented ASan/UBSan/Valgrind run anywhere in the repo or the archived devlogs.

For a codebase this pointer-dense (raw framebuffer writes, hand-rolled RLE decode, 2D array indexing
through computed tile indices, `SDL_memcpy` of 300 KB structs), a sanitiser build is the single
cheapest way to find the class of defect that `-Wextra` structurally cannot see — including SEC-1's
out-of-bounds reads.

**Fix**: a `-Sanitize` switch in `build.ps1` adding
`-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g`, then run `--play-test --seeds 20`
under it. This build is never shipped, so it costs zero submission bytes.

*Note*: MinGW-w64's ASan support is limited; if it does not link, UBSan alone (`-fsanitize=undefined`)
still catches the integer overflow in SEC-2 and any out-of-bounds array indexing UB.

### 🟠 QA-5 — Several substantial features are tested by construction but have never been observed

The README is admirably honest about this, and it belongs in a gap analysis because "tested by
construction" and "verified" are different claims:

| Feature | State |
|---|---|
| Five of six character facings | Row-distinctness is tested; **never seen on screen** |
| `WALK_FPS` (12.0) and the static standing pose | Both unjudged guesses; nobody has watched the walk cycle run |
| Audio | Measured deterministic and sub-millisecond; **never listened to** |
| Aetherhold traversal | Screenshotted from 4 camera positions; **nobody has walked it** |
| Pacing (30–82 s shortest-path clear) | Last measured before two tile-size changes and a much larger grid |
| Shard pickups vs `PROP_CRYSTAL` | Read as ambient decoration in stills; unjudged palette/size pass |

The project's own stated rule — *"Look at the screen. Every visual bug of consequence in this
project's history was found by a screenshot, never by a passing test suite"* — is exactly the
methodology these six items are still waiting on.

### 🟡 QA-6 — Edge cases with no explicit coverage

| Case | Current behaviour |
|---|---|
| `region_count == 0` | Guarded in `world_stage` ([main.c:3465](../src/main.c#L3465)) and `tile_blocked` (via `REGION_NONE`), but reachable only through the untested ERR-1 branch |
| `REGION_NONE` (0xFF) as an array index | Correctly guarded at every site checked (`tile_blocked`, `mm_col`, `render_grid`, `building_restoration` via `region >= REGION_COUNT`). ✅ consistent |
| `ents[i].tile < 0` (unplaced) | Guarded in `entity_in_reach`, `mm_draw`, `render_grid`, `entities_split_ok`. ✅ consistent — but see ERR-1, where the value is `0` rather than `-1` |
| `w->well < 0` | Guarded in `place_entities` ([main.c:2899](../src/main.c#L2899)) |
| Player exactly on a world boundary | `solid_at` returns 1 outside the grid, so "outside the world is wall". ✅ |
| Audio device with a non-48 kHz native rate | Handled — `a->rate = have->freq`, and only `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` is permitted. `--audio-test --rate N` covers it ✅ |
| Zero-length `argv` (`argc == 1`) | All three `arg_*` helpers loop from `i = 1`, so this is safe ✅ |
| Save file larger than 28 bytes | Read is exactly `SAVE_SIZE`; trailing bytes are ignored, not rejected. Benign but undocumented |

### 🟡 QA-7 — Test suite runtime is significant and untracked

`--play-test --seeds 50` takes ~2 minutes, and world generation is **+30%** (13 → 17 ms per world)
since the Aetherhold rework, which the self-test suite pays per seed. A full 25-test pass is roughly
5+ minutes. Nothing tracks this; a future generation change that doubles it again would go unnoticed
until someone waits.

### 🟡 QA-8 — `--land-test`'s 50% reach bar is the only quantitative quality gate

Most tests assert structural invariants (this exists, that never exceeds this). `--land-test` is
close to the only one asserting a *quality* threshold — and it is the one currently failing. There is
no test asserting, for example, minimum entity spread, minimum village count, or a pacing bound,
despite `ENTITY_SPACING`, `VILLAGE_SITES` and `BUILDING_TARGET` all being tuned constants.

### 🟡 QA-9 — Self-test code is 38% of the translation unit

Lines 7955–13394 ≈ 5,440 of 14,049 lines. Entirely compiled out of shipping, so it costs zero bytes
— but it does mean the single file is harder to navigate, and the `#if WAYFARER_SELFTEST` gate now
guards test helpers, capture helpers, the tuning HUD, perf counters, *and* the art validator (SEC-1),
which are four different concerns behind one switch.

### 🟡 QA-10 — Screenshot-based verification has no baseline comparison

`--shot` writes a BMP; nothing compares it to a stored reference. Every visual verification is a
human looking at an image once. `build/` currently holds 14 stale BMPs (`v1.bmp` … `v7.bmp`,
`seed1_grid.bmp`, `rev_test.bmp`, `char_gate_test.bmp`, …) with no manifest saying what any of them
was supposed to show. A tiny perceptual-hash or exact-pixel comparison against committed references
for 3–4 canonical cameras would turn "look at the screen" into a repeatable check.

### 🟢 QA-11 — Test invocations live only in prose

The 25 commands exist as a fenced block in the README. QA-1's script would make them executable
rather than copy-pasteable.

### 🟢 QA-12 — `build/` holds two binaries and 14 BMPs, gitignored and unmanaged

Includes `wayfarer-selftest.old.exe`. Harmless, but `build.ps1` never cleans, so stale artefacts
accumulate and a stale `.old.exe` is exactly the thing someone runs by accident at 2 a.m.

### 🟢 QA-13 — No test asserts the size budget

`build.ps1` gates it, but no *test* does, so `run-tests.ps1` (QA-1) should assert
`(Get-Item build\wayfarer.exe).Length -le 1440000` as test 26.

---

## 5. Performance & Scalability

### ✅ Existing strengths

| | Strength |
|---|---|
| P1 | **Frame time is measured, not assumed.** 900-frame captures at three camera positions (courtyard, keep terrace, spawn) all sit on the 60 fps cap (≈16.8 ms/frame) |
| P2 | **Audio meets its deadline with two orders of magnitude to spare**: 0.079–0.325 ms worst case against 21.333 ms |
| P3 | **Render culling is structurally sound**: whole diagonal bands are rejected with one vertical test (`ay` is constant across a band), the horizontal test rejects per column, side-face colours are computed only when there is a face to paint, and the framebuffer clear uses `SDL_memset4` per row |

---

### 🔴 PERF-1 — `sim_step` performs unbounded full-grid scans every tick, permanently

**File**: [main.c:3564-3626](../src/main.c#L3564-L3626)

This is the single largest avoidable cost in the program, and it has three distinct parts.

**(a) The completion count — a full 25,748-tile scan per revealing region, per tick**
([main.c:3604-3613](../src/main.c#L3604-L3613)):

```c
int cnt = 0, tot = 0;
for (y = 0; y < WORLD_H; y++)
    for (x = 0; x < WORLD_W; x++)
        if (g->w.region[y][x] == i) { tot++; if (g->w.reveal[y][x] >= 1.0f) cnt++; }
```

`Region.tiles` **already holds `tot`** ([main.c:1210](../src/main.c#L1210)), and `cnt` could be
maintained incrementally by the loop directly above that does the revealing. Instead both are
recounted from scratch, every tick, for every region currently animating. At the win moment all 16
regions are queued: 16 × 25,748 = **411,968 tile reads per tick**.

**(b) The Chebyshev ring expansion re-walks the entire covered area every tick**
([main.c:3586-3603](../src/main.c#L3586-L3603)):

```c
for (maxR = 0; maxR < WORLD_W + WORLD_H; maxR++) {          /* up to 321 */
    for (y = cy - maxR; y <= cy + maxR; y++)
        for (x = cx - maxR; x <= cx + maxR; x++) {
            if (x != cx-maxR && x != cx+maxR && y != cy-maxR && y != cy+maxR) continue;  /* interior skipped AFTER iterating it */
            ...
            if (g->w.reveal[y][x] >= 1.0f) { revealed++; continue; }   /* re-counts everything already done */
```

The inner double loop iterates the **full (2r+1)² box** and then discards the interior with a
`continue`, so reaching radius R costs Σ(2r+1)² ≈ (4/3)R³ iterations. For a region ~100 tiles across
that is **≈ 1.3 million iterations per revealing region per tick**, and the already-revealed prefix
is re-counted from scratch on every one of those ticks.

**(c) The fallback loop scans the whole grid per region, per tick, forever**
([main.c:3616-3626](../src/main.c#L3616-L3626)):

```c
for (i = 0; i < g->w.region_count; i++) {
    if (g->w.regions[i].restoration >= 1.0f && !g->region_revealing[i]) {
        int any = 0;
        for (y = 0; y < WORLD_H && !any; y++)
            for (x = 0; x < WORLD_W && !any; x++)
                if (g->w.region[y][x] == i && g->w.reveal[y][x] < 1.0f) any = 1;
        if (any) { g->region_revealing[i] = 1; g->region_reveal_prog[i] = 0.0f; }
    }
}
```

The `&& !any` short-circuit only fires when it **finds** an unrevealed tile. Once a region has
finished revealing — the steady state after every single restore — `any` stays 0 and the loop scans
all 25,748 tiles to conclude there is nothing to do. **Every tick. For the rest of the session.**

Worked example: after the win (or immediately after `--dev` / F12, which sets
`restoration = 1.0` and `reveal = 1.0` everywhere without ever setting `region_revealing`
— [main.c:3439-3448](../src/main.c#L3439-L3448)), all 16 regions qualify:

> 16 regions × 25,748 tiles × 60 ticks/s ≈ **24.7 million tile reads per second**, producing nothing.

**Fix** — all three parts collapse into one change. Add two fields to `Region` (or a parallel array
in `Game`): `revealed_count`, and reuse the existing `tiles`. Then:

- increment `revealed_count` in the one place that writes `reveal[y][x] = 1.0f`;
- replace part (a) with `if (revealed_count >= tiles) region_revealing[i] = 0;`
- replace part (c) with `if (restoration >= 1.0f && revealed_count < tiles)` — an O(1) test;
- for part (b), precompute a per-region tile list once at generation (`regions_build` already walks
  every tile), sorted by Chebyshev distance from `seed_tile`, and reveal a prefix of it. The whole
  ring machinery disappears.

Expected result: `sim_step`'s reveal cost drops from ~10⁵–10⁶ operations per tick to ~10².

---

### 🟠 PERF-2 — `world_heights` recomputes three full-grid passes every tick

**File**: [main.c:3642](../src/main.c#L3642) (call), [main.c:3764-3937](../src/main.c#L3764-L3937) (body)

`sim_step` calls `world_heights` unconditionally on every tick. Each call:

- declares `Uint8 dist[157][164]` — **25,748 bytes of stack**, re-zeroed each time
- runs an initialisation pass, a forward chamfer pass and a backward chamfer pass over all 25,748 tiles
- runs a fourth pass assigning heights

That is **≈ 103,000 tile operations per tick ≈ 6.2 M/s**, plus 1.5 MB/s of stack traffic.

The comment ([main.c:3628-3642](../src/main.c#L3628-L3642)) argues correctly that re-deriving once
per tick is *provably* right for every path that can move `restoration`, and explicitly prefers that
over "hunting down each call site and hoping none are missed" — the mistake that caused the
"levitating houses" regression. **That reasoning is sound and should be preserved.** The observation
here is narrower: the *answer* only changes when some region crosses a `bld_phase` boundary
([main.c:3717-3724](../src/main.c#L3717-L3724)), which is a 16-element comparison.

**Fix that keeps the safety property**:

```c
/* Same "ask, don't remember" discipline — just ask cheaply. */
{
    static /* or Game field */ Uint8 last_phase[REGION_COUNT];
    int changed = 0;
    for (i = 0; i < g->w.region_count; i++) {
        Uint8 p = (Uint8)bld_phase(g->w.regions[i].restoration);
        if (p != last_phase[i]) { last_phase[i] = p; changed = 1; }
    }
    if (changed) world_heights(&g->w);
}
```

This is still derived from the same single source of truth (`bld_phase`), still cannot drift, and
still runs on *every* path that moves `restoration` — including dev unlock and save load, both of
which flow back through `sim_step`. Cost: 16 comparisons per tick instead of 103,000 tile
operations. (Put `last_phase` in `Game`, not at file scope — the `.data` rule.)

---

### 🟠 PERF-3 — `--dev` and F12 immediately enter PERF-1's worst case

**File**: [main.c:3431-3449](../src/main.c#L3431-L3449)

`dev_unlock_all` sets every region's `restoration` and `restore_to` to 1.0 and every tile's `reveal`
to 1.0, but never touches `region_revealing`. On the very next tick, all 16 regions satisfy
`restoration >= 1.0f && !region_revealing[i]` and every one of them triggers a full-grid scan that
finds nothing — the permanent 24.7 M/s state described in PERF-1(c), from the first frame.

Since `Wayfarer-DEV.bat`, the `.lnk` shortcut, and every `--castle` screenshot capture all run with
`--dev`, **most development and capture sessions have been running in this state**. This is likely
part of why it has not been noticed: the frame budget absorbs it at 60 fps on the dev machine.

**Fix**: PERF-1's `revealed_count` fix resolves this automatically. As a one-line stopgap,
`dev_unlock_all` can set `region_revealing[i] = 0` explicitly (it already implicitly means "nothing
left to reveal").

### 🟡 PERF-4 — World generation is 30% slower since the Aetherhold rework, and the suite pays it per seed

13 → **17 ms per world**, because `castle_tier` derives a tier by walking a ring scan
(`castle_inset`) where four hardcoded rectangles were four comparisons. This is a startup and
`R`-key cost, invisible in play — but every seeded self-test regenerates a world, so a 50-seed
`--play-test` and a 200-seed `--bridge-test` both pay it 50 and 200 times.

The README names the fix and the reason it was declined: memoising `castle_inset` into a ~3 KB table
would be 3 KB of *shipped* bytes (PE/COFF with `-fdata-sections`) to speed up a proof nobody ships.
That trade was correct at 1.09 MB. At 923,648 bytes with 516 KB of headroom, 3 KB is **0.6% of
remaining headroom** for a ~25% generation speedup and a materially faster test suite. Worth
revisiting explicitly rather than leaving as a settled decision.

### 🟡 PERF-5 — No per-frame colour cache in the tile loop

**File**: [main.c:7189-7205](../src/main.c#L7189-L7205)

Per visible tile, per frame: `tile_colour`, `tile_reveal`, `tile_hash`, then **two to four
`fog_lerp` calls**, each of which performs an `SDL_MapRGB` (a function call into SDL that recomputes
shifts and losses from the pixel format every time).

At 960×540 with 36×18 diamonds, roughly 1,500–2,000 tiles are visible per frame → up to ~8,000
`SDL_MapRGB` calls per frame, ~480,000/s. Currently affordable (frame time is at the cap), but it is
the first thing to look at if headroom ever shrinks. The format is fixed for the session, so
`Rshift/Rloss` etc. could be hoisted once into locals and the pack done inline — the pattern
`draw_sprite_ex` already uses for its fade blend ([main.c:5622-5631](../src/main.c#L5622-L5631)).

### 🟡 PERF-6 — `render_grid` (F2) regenerates 12 complete worlds

**File**: [main.c:7694-7749](../src/main.c#L7694-L7749)

12 × `game_init` at ~17 ms ≈ **200 ms hitch** whenever the grid view is toggled or the seed changes.
Mitigated by the `dirty` flag (regenerate only on change, always re-present), and it is a debug view
— but it is a visible stall on a keypress a judge might hit.

### 🟡 PERF-7 — `mm_redraw` repaints all 25,748 minimap pixels 4× per second

**File**: [main.c:4767-4775](../src/main.c#L4767-L4775), [main.c:4794](../src/main.c#L4794)

```c
if (hud.mm_dirty || (hud.mm_tick++ % 15) == 0) mm_redraw(g);
```

25,748 `fill_rect` calls (each a 1×1 rect, so each pays the full clip-and-loop preamble) every 15
frames, plus on every restore. ≈ 103,000 `fill_rect` calls/s. Small next to the render loop, but
`mm_col` also reads `reveal`, `solid`, `region` and `regions[].terrain` per pixel. A direct pixel
write instead of `fill_rect` would be a ~10× improvement for a two-line change.

### 🟡 PERF-8 — Structural scalability ceilings are close on two axes

| Axis | Current | Ceiling | Headroom |
|---|---|---|---|
| Byte budget | 923,648 | 1,440,000 (ship) | **516,352 B — comfortable** |
| `World + Scratch` stack | 670,272 B | 716,800 B (compile guard) | **46,528 B — 6.5%, tight** |
| Entities | 19 | 32 (`Uint32` save mask) | 13 |
| Regions | 16 | 32 (`Uint32 adj`) | 16 |
| Buildings | 40 | 255 (`Uint8 bld_at`) | 215 |
| Shards | 8 | 8 (`Uint8` save mask) | **0 — at the limit** |
| Tile height | ±127 px (`Sint8`) | — | ample |

The stack guard is the binding constraint on world size. `sizeof(World) + sizeof(Scratch)` grows at
**26 bytes per tile** — 8 `Uint8` grids + a 4-byte `reveal` float in `World`, plus `seen`(1) +
`owner`(1) + `stack`(4) + `queue`(4) + `dist`(4) in `Scratch` — so one added row costs
26 × 164 = **4,264 bytes**. With 46,528 bytes of margin, **fewer than 11 additional rows** would fail
the build. Shards are at their format ceiling exactly.

### 🟢 PERF-9 — Interaction scans are linear but trivially small

`entity_in_reach` is O(19), `shard_in_reach` O(8), `portal_in_reach` O(2), all only on keypress. No
change warranted; noted so it is not mistaken for a gap.

### 🟢 PERF-10 — `place_paths` is deliberately RNG-free

Adding paths never shifts the world RNG stream, so path work cannot perturb any seeded test result.
An architectural property worth protecting, not a gap.

### 🟢 PERF-11 — No caching of `castle_tier` within a single generation pass

`castle_tier` is called from `castle_apply_layout`, `castle_apply_heights`, `castle_wall_art`,
`castle_stair_art`, `castle_decor_at` and the render dispatch, each recomputing the same ring scan.
A ~1,400-entry `Uint8` scratch table (1.4 KB, a generation *local*, so **zero shipped bytes** —
unlike the memoisation table PERF-4 discusses) would serve every generation-time caller. This is
strictly better than the shipped-table option the README considered and rejected.

---

## 6. Prioritised remediation plan

Ordered by (risk to the submission) ÷ (effort), not by category.

### Tier 1 — before submission

| # | Item | Effort | Why |
|---|---|---|---|
| ~~1~~ | ✅ **ERR-2** — *done*: `fatal()` with `SDL_ShowSimpleMessageBox` + `SDL_GetError()` on the four fatal paths | **measured +1,024 B** | Converts an undiagnosable silent failure into a bug report. Directly de-risks README blocker #2 (never smoke-tested on a second machine). Also closed ERR-12, partially closed OBS-1 |
| ~~2~~ | ✅ **QA-1** — *done*: `tools/run-tests.ps1` runs all 25 tests + a size assertion under one exit code | **0 B**, 388.5 s/run | Makes "25/25 green" a checkable property. Verified 26/26 green. CI *service* still absent |
| 3 | **ERR-1**: set `ents[i].tile = -1` in the pathological-seed path; retry with `seed+1` instead of returning a degenerate world | ~20 min, ~40 B | Removes a silently unwinnable game state |
| 4 | **SEC-1**: three bounds clamps inside `draw_sprite_ex` | ~15 min, ~30 B | Makes the decoder self-limiting so the correctness proof no longer lives only in the unshipped binary |
| 5 | **SEC-2 / SEC-6**: clamp `--scale` to `WIN_SCALE_MAX`, clamp `--frames` to ≥ 0 | ~5 min, ~20 B | Removes signed-overflow UB on a shipping flag |
| 6 | **PERF-1**: `revealed_count` per region; delete both full-grid scans and the ring walk | ~1–2 h, ~0 B net | Removes ~24.7 M wasted tile reads/second in the steady state |

### Tier 2 — before trusting the suite again

| # | Item | Effort |
|---|---|---|
| 7 | **QA-2**: fix seeds 85/417/430, or add them as explicit regression cases | hours–days |
| 8 | **PERF-2**: gate `world_heights` on a `bld_phase` change (preserving the ask-don't-remember discipline) | ~30 min |
| 9 | **SEC-4**: atomic save via temp-file + rename | ~30 min |
| 10 | **SEC-3 / OBS-6**: save checksum + a separate `GEN_VERSION` byte | ~1 h |
| 11 | **QA-4**: `-Sanitize` build switch; run `--play-test --seeds 20` under UBSan | ~1 h |
| 12 | **ERR-4**: verify solvability on the `-100` ungate path | ~15 min |

### Tier 3 — quality of life

| # | Item |
|---|---|
| 13 | **OBS-2**: `--selfcheck` on the shipping binary (art validity + one world + solvability) |
| 14 | **OBS-1**: optional `--log <file>` with one line per session |
| 15 | **QA-5**: actually walk Aetherhold, watch the walk cycle, listen to the audio, view all six facings |
| 16 | **PERF-11**: generation-local `castle_tier` scratch table (zero shipped bytes) |
| 17 | **PERF-4**: revisit the `castle_inset` memoisation decision against the new 516 KB headroom |
| 18 | **SEC-9**: embed a build hash in the title bar |
| 19 | **QA-10**: committed reference BMPs + pixel comparison for 3–4 canonical cameras |
| 20 | **§10 of the architecture doc**: fix the 8 documentation discrepancies (F5/F9 swap first — it is user-facing) |

---

## 7. What is genuinely strong here

A gap analysis that reads as uniformly negative misrepresents this codebase. The following are
better than typical, and the remediation above should not disturb them:

1. **The render-only / collision-truth partition.** A single, consistently enforced rule
   (`tile_blocked` reads only `solid` and `regions[].terrain`) that makes every completability proof
   a re-run rather than a re-argument. This is the load-bearing design decision of the whole project.
2. **Generate-then-verify with progressive fallback.** Reachability is enforced structurally, at
   every ability tier, with a documented and deliberate degradation order.
3. **Negative controls on every test.** The stated rule — a checker that has never rejected anything
   proves nothing — is followed with unusual discipline, including structural controls
   (`--gating-test`'s "gating is decorative", `--aether-test`'s legacy-rectangles control).
4. **`game_load`'s validation-first design.** Ten checks, scratch regeneration, commit-on-success. It
   is the best-written function in the file.
5. **Real-time audio discipline.** Atomics only, no allocation, no locks, payload-before-flag
   ordering, and a measured worst case two orders of magnitude inside the deadline.
6. **Single sources of truth, defended by name.** `castle_tier`, `fog_lerp`, `building_sprite_id`,
   `dream_shift`, `input_poll`, `tile_hash` — each is one function that several callers must agree
   with, and the comments explain what drifted before the consolidation.
7. **The comments are the design record.** They document what was *measured*, what was *tried and
   rejected*, and what the failure mode was — including uncomfortable ones (the causeway that was
   fifteen copies of one arch, the levitating houses, the spawn that landed in the dream realm).
   That is rarer and more valuable than clean code.
8. **Honest status reporting.** The README distinguishes verified claims from unverified ones and
   lists what has never been looked at. Most of §QA-5 exists in this document only because the
   project already said so.

---

*End of gap analysis. See [architecture-summary.md](architecture-summary.md) for the system reference.*
