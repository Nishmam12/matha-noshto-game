---
tags: [process, handover, wayfarer]
updated: 2026-08-02
exe_size_bytes: 679424
---

# Handover — Wayfarer

**Read this first if you are picking this project up cold.** It is the single-file context dump:
what exists, how to build it, what was decided and why, what is verified, and every trap that
already cost time once.

Hub: [[Wayfarer MOC]] · Rules of engagement: [[Agent Prompt]] · Game plan: [[Overview]] ·
Build environment: [[Toolchain Setup]] · Running log: [[INDEX]]

---

## 1. What this is

**Wayfarer** — an exploration / memory-restoration game for the **2P Game Arcade "1.44MB Floppy
Disk" contest**. Deadline **4 September 2026**.

The loop: explore fog-shrouded terrain → find a memory fragment or Found Soul → restore it → that
region's colour permanently returns and a synth layer joins the mix → sometimes an ability comes
back that opens terrain you couldn't cross before → explore further.

### Hard constraints — never negotiate these away

| | |
|---|---|
| Final `.exe`, decompressed and runnable | **≤ 1,474,560 bytes** |
| Ship target (safety margin) | **≤ 1,440,000 bytes** |
| Flag-and-stop threshold | 1,200,000 bytes |
| Platform | Standalone Windows `.exe`. No installer, no runtime, no shipped DLLs beyond OS-provided |
| Assets | **Zero external files.** No PNG/WAV/TTF/OGG/MP3. Everything procedural |
| Excluded libraries | SDL_image, SDL_ttf, SDL_mixer — rendering, fonts and audio are all hand-rolled |
| Judging order | **finished → under size → fun** |

"1.44 MB" has three definitions in common use. We build against the smallest.

---

## 2. Current state

| | |
|---|---|
| **`build\wayfarer.exe`** | **679,424 bytes** — 760,576 under the ship target |
| `build\wayfarer-selftest.exe` | 696,320 bytes — **not a deliverable**, never shipped |
| `src\main.c` | 2,427 lines, single translation unit |
| Warnings | zero, under `-Wall -Wextra` |
| Plan progress | **Weeks 1–3 of 5 complete.** Week 4 (audio) is next |

**Calendar note:** the "weeks" in [[Overview]] are plan phases, not elapsed time. Weeks 1–3 were
completed in a few working sessions, so there is substantially more calendar slack than the
timeline implies. That is budget for polish and the [[QA Checklist]], not licence to add scope.

### What actually works right now

- Procedural cave world (80×45 tiles), seeded, regenerable in-game with **R**
- Region graph: 16 connected regions with terrain types and ability gates
- Continuous movement, swept AABB tile collision, fixed 60 Hz simulation
- Ability gating enforced in collision (Wade / Climb / Kindle)
- Fog-to-colour reveal: sight shows shape, restoration returns colour permanently
- 14 fragments + 5 Found Souls placed with a **proven** reachability guarantee
- Restoration loop: proximity → interact → colour returns → ability granted
- Found Soul states: Lost → Found → Remembered
- Win condition and the 4-stage world-growth read
- Restore confirm beat (audio), real-time safe
- Debug overlay, 12-seed grid view, title-bar stats

### What does NOT exist yet

- **Any music.** The layered synth is Week 4. Only the confirm beat exists
- **Any text on screen.** No bitmap font until Week 5 — including Found Soul restoration lines
- **Save/load** — Week 5
- Idle sway/breathe for Found Souls
- Audio-layer-per-restore (the hook is wired; the layers are not)

### Git

Remote: **`https://github.com/Nishmam12/matha-noshto-game`** — private, branch `main`.

```
45eeb14  v0.2.0: Implement PCG32 PRNG, cellular world gen, continuous movement & fog reveal
85b0f7b  Add project source, build system and engineering docs
0678e7d  Initial commit
```

**Weeks 2 and 3 are NOT committed yet.** Uncommitted at time of writing: `src/main.c`,
`devlog/INDEX.md`, `design/Open Decisions.md`, `design/systems/Fog and Reveal.md`, and the new
`devlog/2026-08-02-session-02.md` and `-03.md`.

**Do not add `Co-Authored-By` trailers to commits.** This was asked for explicitly and one had to
be stripped and force-pushed.

`build/` and `.obsidian/` are gitignored. `wayfarer.exe` is therefore not in the repo — attach it
to a GitHub Release if a playable download is wanted.

> There is an empty `devlog.md` at the vault root that nobody created deliberately. Probably an
> accident. Left alone rather than deleted without asking.

---

## 3. Environment — read before building

**Nothing about the toolchain is on PATH, and none of it lives in the repo.**

| What | Path | Version |
|---|---|---|
| MinGW-w64 (gcc, ld, as, make, ninja, cmake, gdb) | `G:\tools\w64devkit` | w64devkit 2.9.0, **GCC 16.1.0**, `x86_64-w64-mingw32` |
| SDL2 source | `G:\tools\sdl2-src\SDL2-2.32.10` | 2.32.10 |
| **Our minimal static SDL2** | `G:\tools\SDL2-min` | built by `build-sdl2.ps1` |
| Stock prebuilt SDL2 (reference only, not linked) | `G:\tools\SDL2` | official MinGW dev package |

Override the root with `$env:WAYFARER_TOOLS`. `G:` rather than `C:` because C: had under 10 GB
free. Full setup commands are in [`README.md`](README.md).

### Build

```powershell
.\build-sdl2.ps1          # once, ~1 min. Builds the cut-down static SDL2
.\build.ps1               # the game -> build\wayfarer.exe, prints size + delta + headroom
.\build.ps1 -SelfTest     # separate build\wayfarer-selftest.exe with the test harness
.\build.ps1 -Map          # also emit build\wayfarer.map (~1.5 MB) for size forensics
```

`build.ps1` **exits non-zero if the binary goes over budget** — the size limit is enforced by the
build, not by remembering to check. Self-test builds are excluded from budget tracking.

---

## 4. The test suite — run this before believing anything

```powershell
.\build.ps1 -SelfTest
$e = ".\build\wayfarer-selftest.exe"

& $e --rng-test    --seed 1                 # PRNG: reproducibility, stream independence, bias
& $e --move-test   --seeds 20 --seed 1      # collision, no drift, determinism, diagonal speed
& $e --region-test --seeds 30 --seed 1      # region graph structure + coverage
& $e --reach-test  --seeds 50 --seed 1      # reachability invariant + negative control
& $e --gating-test --seeds 30 --seed 1      # walk-reachable == graph-reachable, all 4 tiers
& $e --play-test   --seeds 50 --seed 1      # full headless playthroughs to completion
& $e --audio-test 3000 --sfx                # callback timing under restore-beat load
& $e --autoplay 20000 --seed 3              # windowed autopilot; watch restoration happen
& $e --input-test 4000 --seed 5             # real keyboard path, reports position delta
```

**All currently pass.** Last full run:

```
rng     : PASS (0 checks failed)
move    : PASS (0 failures across 20 seeds)
region  : PASS (0 failures across 30 seeds)
reach   : PASS (0 failures across 50 seeds)
          negative control (verifier rejects unwinnable worlds): PASS
gating  : PASS (0 failures across 30 seeds)
play    : PASS (0 seeds could not be completed)
audio   : worst case 0.156 ms   partial writes 0   out of range 0
```

### The game itself

```powershell
.\build\wayfarer.exe --seed 3
```

`WASD`/arrows move · `E`/`Space` restore · `F1` region overlay · `F2` 12-seed grid ·
`R` regenerate with next seed · `ESC` quit. Stats are in the **window title** (there is no font
yet). `--frames N` runs exactly N frames then exits 0, for scripted checks.

---

## 5. Code map — `src/main.c`, in order

| Section | What lives there |
|---|---|
| Tunables | All `#define`s. Everything designers would touch is here |
| RNG | PCG32, three independent streams (terrain / entities / audio) |
| Audio | Callback, device open, restore confirm beat |
| Regions | Region/World/Scratch structs, terrain enums |
| World gen | Cellular-automaton cave, flood fill, farthest-point sampling, multi-source BFS |
| Reachability | `regions_reachable`, `world_solvable`, entity placement, generate-then-verify |
| Movement | `tile_blocked`, `player_blocked`, `move_axis`, `sim_step` |
| Restoration | `entity_in_reach`, `try_restore`, `game_complete`, `world_stage` |
| Graphics | `fog_lerp`, `render`, `render_grid`, `camera_follow` |
| Self-test | Everything under `#if WAYFARER_SELFTEST` — compiled out of the shipping build |
| `main` | Fixed-timestep loop, input, debug keys, frame cap |

### Tunables worth knowing

| Constant | Value | Notes |
|---|---|---|
| `WORLD_W` × `WORLD_H` × `TILE` | 80 × 45 × 16 | = 1280×720 world, 640×360 view |
| `REGION_COUNT` | 16 | **Hard cap 32** — adjacency is a `Uint32` bitmask |
| `FRAGMENT_COUNT` / `SOUL_COUNT` | 14 / 5 | Combined **must stay ≤ 32** — restored-mask is `Uint32` |
| `PLAYER_SPEED` | 110 px/s | |
| `SIGHT_MAX` | 0.42 | How far walking alone reveals. Restoration goes to 1.0 |
| `REVEAL_TILES` / `REVEAL_RATE` | 5 / 2.5 | Sight radius and speed |
| `RESTORE_RATE` | 0.9 | How fast colour returns after a restore |
| `INTERACT_RADIUS` | 22 px | |
| `TICK_HZ` / `FRAME_HZ` | 60 / 60 | Simulation is fixed-step; render is capped separately |

---

## 6. Decisions already made — do not re-litigate without flagging

1. **C + static SDL2, MinGW-w64.** Rationale in [[Agent Prompt]]. Chosen deliberately for byte
   control, not by default.
2. **We compile our own SDL2.** The official prebuilt `libSDL2.a` cost **1,656,876 bytes** for a
   do-nothing window — over the hard limit before any game code existed. It is built without
   `-ffunction-sections` (so `--gc-sections` can only drop whole objects) and ships every
   subsystem. Our cut-down build took the exe from 1,714,176 → 669,696.
3. **No `SDL_Renderer`.** The entire render subsystem is compiled out — it cost more than the rest
   of SDL combined. Drawing is direct pixel writes into the window surface. This also settles the
   `SDL_LockTexture` vs baked-texture question in [[Fog and Reveal]]: neither exists for us.
4. **No `-flto`.** w64devkit's GCC is built without LTO. Costs little (single translation unit,
   and `libSDL2.a` has no GCC IR anyway). If we outgrow one `.c`, use a **unity build** rather
   than changing toolchain.
5. **Continuous movement + tile collision**, fixed 60 Hz step. Settled 2026-08-02; the core hook
   is a smooth fog reveal and tile-stepping made it read chunky.
6. **PCG32, not xorshift.** [[World Generation]] requires terrain/entity/audio streams to be
   independent. PCG32's sequence constant makes them independent *by construction*; a single
   xorshift could only offer different offsets into one sequence, which can silently overlap.
7. **Video and audio initialise separately.** `SDL_Init` fails if *any* subsystem fails, so
   `SDL_Init(VIDEO|AUDIO)` refused to launch on machines with no sound device. **Do not merge
   these calls back together.**
8. **Self-test lives in a separate binary**, not behind a runtime flag. This makes "no debug code
   in the submission" structural rather than something to remember.
9. **Fog has two contributions** — sight (shape, capped 0.42) and restoration (full colour,
   permanent). This is an *interpretation* of [[Fog and Reveal]], flagged there, because a literal
   reading leaves the world black until the first restore including the fragment you must find.

---

## 7. Traps — each of these already cost time once

**Build / toolchain**

- **Zero-initialised statics land in `.data`, not `.bss`.** On PE/COFF, `-fdata-sections` emits
  them as file-backed `.data$name` COMDATs. Four world-sized `static` arrays put **39,648 bytes of
  literal zeros** into the exe. **Never declare a world-sized buffer `static`** — use a stack local
  (peak use is ~47 KB against a 2 MB stack). Check with `objdump -h`: large `.data` + small `.bss`
  means it is happening again. Gets worse as the world grows.
- gcc shells out to `as.exe` / `ld.exe` **by bare name**, so the devkit's `bin` must be on PATH.
  `build.ps1` does this; a manual gcc invocation will fail with "cannot execute 'as'".
- SDL refuses `-DSDL_DYNAMIC_API=0` on the command line. `build-sdl2.ps1` patches
  `src/dynapi/SDL_dynapi.h` instead (SDL's own sanctioned route) and fails loudly if the guard
  text ever stops matching after an SDL upgrade.
- `SDL_VIDEO` requires `SDL_LOADSO` on Windows (`CMakeLists.txt:1902`). It is the only subsystem
  dependency that could not be cut.

**C**

- **`sizeof` on a pointer.** A refactor turned `Uint8 seen[3600]` into `Uint8 *seen`, so
  `sizeof(seen)` silently became **8**. The `memset` cleared almost nothing and spawns landed in
  tiny side pockets. Watch for this whenever an array parameter becomes a pointer.
- **`game_init` must zero the whole `Game`.** It previously left progress counters alone, so
  totals accumulated across worlds — pressing **R** in-game carried the old world's fragment count
  into the new one and the win condition fired on an untouched world.

**Testing**

- **Relative assertions are not correctness.** Every structural region test passed on a partition
  covering **5 of 1585** walkable tiles, because all of them checked internal consistency (counts
  agreeing with counts). The check that caught it was the absolute one: *every walkable tile
  belongs to a region*. Prefer absolute invariants.
- **A verifier that never rejects anything proves nothing.** The reachability check passed 50/50
  on the first attempt — identical output to a checker hardwired to return "solvable". It only
  means something because a negative control feeds it deliberately unwinnable worlds.
- **Suspect the harness.** The playthrough test reported "20 of 20 seeds unwinnable", then "5 of
  20". Both times the test walker was wrong, not the game. Reporting either would have sent
  someone hunting a generator bug that did not exist.
- **Screenshots are poor evidence of direction.** With a follow camera the player stays centred
  and only scenery moves. Use the numeric `--input-test` instead.

**Windows / PowerShell (this environment)**

- `SetForegroundWindow` is **blocked for background processes**, so synthetic keystrokes silently
  go elsewhere. Use `PostMessage(hwnd, WM_KEYDOWN, vk, lparam)` straight to the window.
- `FindWindow(null, "X")` fails from PowerShell — `$null` marshals as `""`, not NULL. Use
  `EnumWindows`, or `[NullString]::Value`.
- PowerShell 5.1: **no `&&`, no `||`, no ternary.** `2>&1` on a native exe turns stderr into
  `NativeCommandError` and can fail on a mere warning — don't redirect, stderr is captured anyway.
- Beware PowerShell function names colliding with built-in aliases (`H` shadowed `Get-History`).
- Scope: `$script:x` set inside a callback is not the same variable as a function-local `$x`.

---

## 8. Verified vs NOT verified

### Verified — measured, not assumed

- Builds clean, zero warnings; binary **stripped**, **static**, imports only OS DLLs; no
  SDL_image/ttf/mixer; no self-test code in the shipping exe
- PRNG: reproducible, streams provably independent, `rng_below` bias 1.4–3.3% (want < 5%)
- Collision: 20 seeds, zero solid-tile overlaps, no escapes, no drift with zero input, identical
  trajectories, straight and diagonal travel both exactly 110.00 px/60 ticks
- Region graph: 30 seeds — 100% tile coverage, all regions contiguous, adjacency symmetric, graph
  connected, spawn never gated
- Reachability: 50 seeds solvable first attempt, gating never relaxed, **plus** a negative control
  proving the verifier rejects sealed worlds and self-locked gates
- Gating: walk-reachable == graph-reachable at all 4 ability tiers, 30 seeds
- **Full playthroughs: 50/50 seeds completed** by autopilot through real collision and the real
  restore call. 19/19 entities, all 3 abilities, every seed
- Audio callback: **0.222 ms worst case against a 21.333 ms deadline (1.0%)** with the restore
  beat firing every 40 ms; zero partial writes, zero NaN, no clipping
- Real keyboard path: `D` produces `right=1`, `dx +81.67, dy +0.00`

### NOT verified — be honest about these

- **Whether any of it is fun.** The loop closes and is provably completable. Enjoyable is untested
- **Nobody has played it by hand.** Every playthrough was the autopilot. Interact affordance,
  reach radius, movement speed and the confirm beat have never been judged by a human
- **No audio has ever been heard**, only measured
- **Never run on another machine.** [[QA Checklist]]'s "runs clean without dev tools" is unchecked
- **Pacing:** shortest-path full clear is **30–82 s of walking** (mean ~52 s). Real play with fog
  will be longer by an unknown multiplier
- The gating-relaxation fallback in world generation **has never fired** (0/50 seeds), so that path
  is untested against real failure
- SDL's own resampler never ran — this device satisfied both 48000 and 44100 exactly
- Generation time not profiled (noticeable when the grid view builds 12 worlds)
- Render cost of the tile loop not profiled

---

## 9. Open decisions — status

From [[Open Decisions]]:

| Decision | Status |
|---|---|
| Grid vs continuous movement | **RESOLVED** — continuous, 2026-08-02 |
| Landmass size / region count | **Provisional 16.** First pacing number now exists (30–82 s). Needs a call |
| Fragment + Found Soul counts | **Provisional 14 + 5** (mid-range default). Awaiting sign-off |
| Kindle: passive radius vs active ping | **Open.** Currently a plain region gate; neither behaviour built |
| Inventory/tool icons from mockup | **Open.** Treated as pitch-art decoration, not built |

[[Cut List]] is pre-committed if time runs short. **Never cut:** the fog-reveal core feel, the
reachability guarantee, staying under the byte limit, a defined completable end state.

---

## 10. How to work on this

Follow [[Agent Prompt]]'s loop, and narrate which stage you are in:
**plan → implement → build → measure → verify → report.**

- Report the exact `.exe` byte size and delta after **every** build
- Treat warnings as defects
- Batch-test ≥20 seeds after any change to generation or placement
- State plainly what you did **not** verify. Never claim audio sounds right or that something
  feels good — those need a human
- Every new note must link to an existing one; orphans break the graph
- Log every session to `devlog/YYYY-MM-DD-session-NN.md`, and append a `## Session NN` section if
  a file for today already exists. Update [[INDEX]] every session

---

## 11. Next: Week 4 — [[Audio and Synth]]

Softsynth, pattern data, the five named layers (Base / Strings / Pad / Bells / Voice of Souls),
SFX, and callback profiling under full five-layer load.

The restoration hook that should activate a layer **already exists and is waiting** — `try_restore`
is where a layer would be switched on. Current callback headroom is ~99%, so there is room, but
[[QA Checklist]] requires profiling under the full load, not extrapolating from one sine.

**Before that, consider asking the team to actually play it.** Weeks 1–3 are verified correct but
nothing has been judged by a human, and the two provisional counts plus the pacing number are all
waiting on exactly that.
