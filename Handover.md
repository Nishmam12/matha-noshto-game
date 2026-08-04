---
tags: [process, handover, wayfarer]
updated: 2026-08-04
exe_size_bytes: 689152
---

# Handover — Wayfarer

**Read this first if you are picking this project up cold.** It is the single-file context dump:
what exists, how to build it, what was decided and why, what is verified, and every trap that
already cost time once.

Hub: [[Wayfarer MOC]] · Rules of engagement: [[Agent Prompt]] · Game plan: [[Overview]] ·
Build environment: [[Toolchain Setup]] · Renderer: [[Isometric Rendering]] · Running log: [[INDEX]]

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
| Assets | **Zero external files.** No PNG/WAV/TTF/OGG/MP3/GLB. Everything procedural |
| Excluded libraries | SDL_image, SDL_ttf, SDL_mixer — rendering, fonts and audio are all hand-rolled |
| Judging order | **finished → under size → fun** |

"1.44 MB" has three definitions in common use. We build against the smallest.

> **The size constraint is not the binding one, and has never been.** See §9. Plan against
> *authoring effort*, which is the real limit, not bytes.

---

## 2. Current state

| | |
|---|---|
| **`build\wayfarer.exe`** | **689,152 bytes** — 750,848 under the ship target |
| `build\wayfarer-selftest.exe` | 715,776 bytes — **not a deliverable**, never shipped |
| `src\main.c` | 3,934 lines, single translation unit |
| Warnings | zero, under `-Wall -Wextra` |
| Plan progress | Weeks 1–3 complete. **An isometric renderer pivot was inserted before Week 4.** Audio and UI are still untouched |

### What actually works right now

- Procedural world (80×45 tiles at 32 px), seeded, regenerable in-game with **R**
- **Isometric 2.5D renderer**: 2:1 diamonds, elevation with cliff faces, band-sweep depth sort
- **Procedural scenery**: layered trees (8,192 variants), bushes, rocks, reeds, flowers, crystals,
  stumps — all from a per-tile hash, none stored
- **Procedural buildings**: 22 per world on average, 640,000 mix-and-match combinations
- 960×540 logical framebuffer, integer-scaled into the window; F11 borderless fullscreen
- Region graph: 16 connected regions with terrain types and ability gates
- Continuous movement, swept AABB tile collision, fixed 60 Hz simulation
- Ability gating enforced in collision (Wade / Climb / Kindle)
- Fog-to-colour reveal: sight shows shape, restoration returns colour permanently
- 14 fragments + 5 Found Souls placed with a **proven** reachability guarantee
- Restoration loop, Found Soul states, win condition, 4-stage world-growth read
- Restore confirm beat (audio), real-time safe
- Debug overlay, 12-seed grid view, title-bar stats, render instrumentation

### What does NOT exist yet

- **Any music.** The layered synth is Week 4. Only the confirm beat exists
- **Any text on screen.** No bitmap font until Week 5 — including Found Soul restoration lines
- **Save/load** — Week 5
- **Any animation at all.** Nothing sways, shimmers, bobs or smokes. The world is static
- **The player is still a 24×24 orange square.** No layered character, no walk cycle, no facing
- **Input is still world-aligned.** `W` travels up-*right* on screen, not up
- **Buildings do not respond to restoration.** The ruin→whole rebuild is designed but not built,
  and it is the single highest-value item remaining (see §11)
- Idle sway/breathe for Found Souls
- Audio-layer-per-restore (the hook is wired; the layers are not)

### Git

Remote: **`https://github.com/Nishmam12/matha-noshto-game`** — private, branch `main`.

```
d622ed0  docs: record the isometric decision and supersede the flat-geometry notes
f96de1b  Buildings: mix-and-match houses, verified by the existing reachability proof
297b390  Remaining props, and an isometric interact ring
b63a4ec  devlog: session 01 of the isometric pivot, and INDEX size history
5f664c3  Layered procedural trees and bushes, and a real depth sort
1df01f7  Surface detail: per-tile hash, ground grain, terrain marks
cf84716  Slice 3: 960x540 logical, integer-scaled into the window
6f187e8  Slice 2: elevation -- cliff faces, terraces, sunken water
af1f928  Slice 1: 32px tiles and the isometric projection
18c7c49  Slice 0: render instrumentation, and the baseline it produced
22a4108  v0.3.0: Region graph, reachability invariant, restoration loop
```

**Everything is committed.** Nothing is pushed to the remote yet — check before assuming.

**Do not add `Co-Authored-By` trailers to commits.** This was asked for explicitly and one had to
be stripped and force-pushed.

`build/` and `.obsidian/` are gitignored. `wayfarer.exe` is therefore not in the repo — attach it
to a GitHub Release if a playable download is wanted.

> Two files at the vault root that nobody in the build created: an empty `devlog.md`, and an
> untracked **`tree.glb`** (a binary glTF 3D model). The `.glb` in particular is worth raising —
> **no external asset file can ship**, so if someone is planning to load it, that plan needs to
> change. Both left alone rather than deleted without asking.

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

& $e --iso-test                             # NEW: rasteriser exactness, seams, depth, upscale
& $e --village-test --seeds 30 --seed 1     # NEW: building placement invariants
& $e --rng-test    --seed 1                 # PRNG: reproducibility, stream independence, bias
& $e --move-test   --seeds 20 --seed 1      # collision, no drift, determinism, diagonal speed
& $e --region-test --seeds 30 --seed 1      # region graph structure + coverage
& $e --reach-test  --seeds 50 --seed 1      # reachability invariant + negative control
& $e --gating-test --seeds 30 --seed 1      # walk-reachable == graph-reachable, all 4 tiers
& $e --play-test   --seeds 50 --seed 1      # full headless playthroughs to completion
& $e --audio-test 3000 --sfx                # callback timing under restore-beat load
& $e --autoplay 20000 --seed 3              # windowed autopilot; watch restoration happen
& $e --input-test 4000 --seed 5             # real keyboard path, reports position delta
& $e --frames 400 --perf --seed 4           # NEW: render/present/sleep ms, px and calls per frame
& $e --frames 60 --seed 4 --overlay --shot out.bmp   # NEW: scripted screenshot
```

**All currently pass.** Last full run, 2026-08-04:

```
iso     : PASS  exact tiling (65536 == 65536), 0 seam px, 0 mis-owned px,
                upscale x1/x2/x3 exact with margins cleared
          negative control (1px tile offset rejected): PASS  [63800 vs 65536]
village : PASS (0 failures across 30 seeds), mean 22 buildings per world
          negative controls (non-solid footprint, walled-in building): both PASS
rng     : PASS (0 checks failed)
move    : PASS (0 failures across 20 seeds); straight and diagonal both 220.00 px/60 ticks
region  : PASS (0 failures across 30 seeds)
reach   : PASS (0 failures across 50 seeds)
          negative control (verifier rejects unwinnable worlds): PASS
          gating relaxed on 0 of 50 seeds
gating  : PASS (0 failures across 30 seeds)
play    : PASS (0 seeds could not be completed)
audio   : worst case 0.141 ms   partial writes 0   NaN 0   out of range 0
perf    : render 0.859 ms  present 1.259 ms  3.27 M px/frame  74,835 calls/frame
```

### The game itself

```powershell
.\build\wayfarer.exe --seed 3
```

`WASD`/arrows move · `E`/`Space` restore · `F1` region overlay · `F2` 12-seed grid ·
**`F11` borderless fullscreen** · `R` regenerate with next seed · `ESC` quit. Stats are in the
**window title** (there is no font yet). `--frames N` runs exactly N frames then exits 0.
`--scale N` forces the window scale.

> **`W` moves up-*right*, not up.** Input is still world-aligned. This is a known open decision,
> not a bug — see §9.

---

## 5. Code map — `src/main.c`, in order

| Line | Section | What lives there |
|---|---|---|
| 23 | Tunables | All `#define`s. Everything designers would touch is here |
| 83 | **Isometric projection** | `ISO_*`, `ELEV_*`, `FACE_*`, void colour |
| 150 | RNG | PCG32, three independent streams (terrain / entities / audio) |
| 243 | Audio | Callback, device open, restore confirm beat |
| 374 | Args | `arg_int`, `arg_flag`, `arg_val` |
| 400 | World | Region/World/Scratch/**Building** structs, terrain enums |
| 406 | Regions | World gen, flood fill, BFS partition, adjacency |
| 596 | **Building placement** | `place_buildings` — runs *before* the reachability verifier |
| 815 | Reachability | `regions_reachable`, `world_solvable`, entity placement, generate-then-verify |
| 990 | Movement | `tile_blocked`, `player_blocked`, `move_axis`, `sim_step` (1162) |
| 1094 | Restoration | `entity_in_reach`, `try_restore`, `game_complete`, `world_stage` |
| 1249 | **Heights** | `world_heights` (derived elevation), `height_at` (1299) |
| 1406 | **Perf** | `Perf`, counters, `perf_report`. All behind `WAYFARER_PERF` |
| 1472 | Graphics | `fill_rect`, `vspan`, `iso_tile`, `iso_diamond`, `iso_ring`, `blit_scale`, `tile_hash`, `fog_lerp`, `tile_detail` |
| 1727 | **House parts** | `BV_*` variant accessors, wall/roof palettes; `draw_building` (2036) |
| 1857 | **Props** | palettes, `draw_tree`/`bush`/`rock`/`reed`/`flower`/`crystal`/`stump`, `prop_at` (1992), `draw_prop` |
| 2188 | Render | `render` — the band sweep; `render_grid` (2404), `camera_follow` (2476) |
| 2490 | Window | `pick_scale`, `backbuffer_new`, `present` |
| 2565 | Self-test | Everything under `#if WAYFARER_SELFTEST` — compiled out of the shipping build |
| 3972 | `main` | Fixed-timestep loop, input, debug keys, frame cap |

### Tunables worth knowing

| Constant | Value | Notes |
|---|---|---|
| `TILE` | **32** | Diamonds are 64×32. Changing it is free — the projection identity holds at any size |
| `WORLD_W` × `WORLD_H` | 80 × 45 | = 2560×1440 world px, 4000×2000 in iso screen space |
| `LOGICAL_W` × `LOGICAL_H` | 960 × 540 | Rasterised size; window is this × an integer scale |
| `WIN_SCALE_MAX` | 3 | Chosen at startup from SDL's *usable* display bounds |
| `PLAYER_SPEED` / `PLAYER_SIZE` | 220 / 24 | Both scaled with `TILE`; collision is scale-invariant |
| `INTERACT_RADIUS` | 44 px | Same fraction of a tile as before the scale change |
| `ELEV_STEP` / `ELEV_MAX` | 12 / 48 | Rock terraces per ring of distance into a mass |
| `ELEV_WATER` / `ELEV_LEDGE` | −6 / 16 | Water sinks, Climb terrain reads as a shelf |
| `FACE_L` / `FACE_R` | 58 / 76 | Side-face brightness, per cent. **The entire lighting model** |
| `STOREY_H` / `WALL_BASE` | 14 / 10 | Building wall height = `WALL_BASE + levels × STOREY_H` |
| `BUILDING_MAX` | 40 | Placement makes ~22 per world from 3000 attempts |
| `LOBES` | 6 | Tree canopy lobes. Was 5 and read as a stack of discs |
| `REGION_COUNT` | 16 | **Hard cap 32** — adjacency is a `Uint32` bitmask |
| `FRAGMENT_COUNT` / `SOUL_COUNT` | 14 / 5 | Combined **must stay ≤ 32** — restored-mask is `Uint32` |
| `SIGHT_MAX` | 0.42 | How far walking alone reveals. Restoration goes to 1.0 |
| `REVEAL_TILES` / `REVEAL_RATE` | 5 / 2.5 | In tiles, so unaffected by the scale change |
| `TICK_HZ` / `FRAME_HZ` | 60 / 60 | Simulation is fixed-step; render is capped separately |

---

## 6. Decisions already made — do not re-litigate without flagging

1. **C + static SDL2, MinGW-w64.** Rationale in [[Agent Prompt]]. Chosen deliberately for byte
   control, not by default.
2. **We compile our own SDL2.** The official prebuilt `libSDL2.a` cost **1,656,876 bytes** for a
   do-nothing window — over the hard limit before any game code existed. Our cut-down build took
   the exe from 1,714,176 → 669,696.
3. **No `SDL_Renderer`.** The entire render subsystem is compiled out. Drawing is direct pixel
   writes into a surface. `SDL_CreateRGBSurfaceWithFormatFrom`, `SDL_GetDisplayUsableBounds`,
   `SDL_SetWindowFullscreen` and `SDL_SaveBMP_RW` **do** survive the cut and are used.
4. **No `-flto`.** w64devkit's GCC is built without LTO. If we outgrow one `.c`, use a **unity
   build** rather than changing toolchain.
5. **Continuous movement + tile collision**, fixed 60 Hz step. Settled 2026-08-02.
6. **PCG32, not xorshift.** Terrain/entity/audio streams must be independent *by construction*.
7. **Video and audio initialise separately.** `SDL_Init` fails if *any* subsystem fails. **Do not
   merge these calls back together.**
8. **Self-test lives in a separate binary**, not behind a runtime flag. `WAYFARER_PERF` defaults to
   `WAYFARER_SELFTEST` for the same reason: "no debug code in the submission" is structural.
9. **Fog has two contributions** — sight (shape, capped 0.42) and restoration (full colour,
   permanent). An *interpretation* of [[Fog and Reveal]], flagged there.
10. **Isometric 2.5D, decided 2026-08-04.** [[Agent Prompt]] names "switching rendering approach"
    as the canonical change requiring a flagged decision. Three options were put; the largest was
    chosen. Full rationale in [[Isometric Rendering]].
11. **Per-column span rasterisation, not scanline diamonds.** Chosen for *correctness*: the column
    spans are the exact preimage of the tile under the inverse projection, so the tiling is
    provably gap-free. A scanline diamond needs a second parallelogram routine whose edge must
    agree with the first to the pixel — the seam bug class.
12. **`height`, `bld_at` and all decoration are render-only.** Collision reads `solid` and
    `regions[].terrain` and nothing else. This is what let the 50-seed completability proof be
    re-*run* rather than re-*argued* after every visual change. **Keep it that way.**
13. **Buildings are placed before the reachability verifier**, not after — so a layout that walls
    something off is rejected and regenerated. The guarantee is the safety net, not something
    worked around. This is why the village work did not invalidate anything.
14. **Decoration draws from a stateless hash, never from the RNG streams.** So it cannot perturb a
    single fragment placement, and seed-based results stay valid by construction.
15. **Roofs are stacked shrinking diamonds**, not a pitched-plane rasteriser — same seam argument
    as (11). In a 2:1 projection a 45° roof over half-width `rw` rises exactly `rw/2` on screen.

---

## 7. Traps — each of these already cost time once

**Build / toolchain**

- **Zero-initialised statics land in `.data`, not `.bss`.** On PE/COFF, `-fdata-sections` emits
  them as file-backed `.data$name` COMDATs. Four world-sized `static` arrays once put **39,648
  bytes of literal zeros** into the exe. **Never declare a world-sized buffer `static`** — use a
  stack local or `SDL_malloc`. Check with `objdump -h`: currently `.data` 17,520 B / `.bss`
  2,912 B, which is the SDL baseline. Large `.data` + small `.bss` means it happened again.
- gcc shells out to `as.exe` / `ld.exe` **by bare name**, so the devkit's `bin` must be on PATH.
  `build.ps1` does this; a manual gcc invocation will fail with "cannot execute 'as'".
- SDL refuses `-DSDL_DYNAMIC_API=0` on the command line. `build-sdl2.ps1` patches
  `src/dynapi/SDL_dynapi.h` instead and fails loudly if the guard text stops matching.
- `SDL_VIDEO` requires `SDL_LOADSO` on Windows. The only subsystem dependency that could not be cut.
- **The linker intermittently fails with "cannot open output file … Permission denied."** No
  process holds the exe; it is a transient file-lock (antivirus or indexer). **Just re-run the
  build.** It cost two false alarms.

**C**

- **`sizeof` on a pointer.** A refactor turned `Uint8 seen[3600]` into `Uint8 *seen`, so
  `sizeof(seen)` silently became **8**. Watch for this whenever an array parameter becomes a
  pointer.
- **`game_init` must zero the whole `Game`.** It previously left progress counters alone, so
  pressing **R** carried the old world's fragment count into the new one and the win condition
  fired on an untouched world.

**Source hygiene**

- **Never run source through a PowerShell text filter.** `Get-Content -Raw | Set-Content` on
  `src/main.c` read it as CP1252 and rewrote it as UTF-8, turning **all 76 em-dashes into mojibake
  and adding a BOM**. It compiled cleanly, so nothing caught it until an editor edit failed to
  match its own search string. PowerShell 5.1 does not reliably detect BOM-less UTF-8, and
  `Set-Content -Encoding utf8` writes a BOM. **Use the editor.** The same hazard applies to commit
  messages written via `Set-Content` (one landed with a BOM in the subject line and needed a
  `filter-branch` to fix).
- PowerShell here-strings (`@'...'@`) as a `git commit -m` argument silently failed to parse and
  git received the message as a dozen pathspecs. **Write the message to a file and use
  `git commit -F`.**

**Testing**

- **Relative assertions are not correctness.** Every structural region test once passed on a
  partition covering **5 of 1585** walkable tiles, because all of them checked counts against
  counts. Prefer absolute invariants — `--village-test` is written that way deliberately.
- **A verifier that never rejects anything proves nothing.** Every checker here has a negative
  control: reachability, iso rasterisation, and both village invariants.
- **Suspect the harness.** The playthrough test twice reported worlds unwinnable when the test
  walker was wrong, not the game.
- **Screenshots are poor evidence of *direction*** — with a follow camera the player stays centred.
  Use `--input-test`. They are, however, the *right* tool for judging rendering, and `--shot`
  exists for that.

**Windows / PowerShell (this environment)**

- `SetForegroundWindow` is **blocked for background processes**, so synthetic keystrokes silently
  go elsewhere. Use `PostMessage(hwnd, WM_KEYDOWN, vk, lparam)` straight to the window.
- `FindWindow(null, "X")` fails from PowerShell — `$null` marshals as `""`. Use `EnumWindows`.
- PowerShell 5.1: **no `&&`, no `||`, no ternary.** `2>&1` on a native exe turns stderr into
  `NativeCommandError` and can fail on a mere warning — don't redirect.
- Beware PowerShell function names colliding with built-in aliases (`H` shadowed `Get-History`).

**Rendering**

- **A 1920×1080 window does not fit a 1920×1080 desktop.** Guessing at window chrome cost a whole
  scale step. Ask SDL for `SDL_GetDisplayUsableBounds` (already excludes the taskbar) and allow
  only for the title bar.
- **A fullscreen window is rarely an exact multiple of the logical size.** `blit_scale` must centre
  *and clear the margin*, or the border holds whatever was in the surface before.
- **The screen clear is mandatory now.** The old flat loop covered every pixel by construction
  (measured: exactly 1.00× the screen). Diamonds only tile where the world exists.

---

## 8. Verified vs NOT verified

### Verified — measured, not assumed

- Builds clean, zero warnings; binary **stripped**, **static**, imports only OS DLLs; no
  SDL_image/ttf/mixer; no self-test or perf code in the shipping exe (proved by a **+0 byte delta**
  when the instrumentation was added)
- PRNG: reproducible, streams provably independent, `rng_below` bias 1.4–3.3%
- Collision: 20 seeds, zero solid-tile overlaps, no escapes, no drift, identical trajectories,
  straight and diagonal both exactly 220.00 px/60 ticks
- Region graph: 30 seeds — 100% tile coverage, all regions contiguous, adjacency symmetric, graph
  connected, spawn never gated
- Reachability: 50 seeds solvable first attempt **with ~22 buildings placed**, gating never
  relaxed, plus a negative control proving the verifier rejects sealed worlds
- Gating: walk-reachable == graph-reachable at all 4 ability tiers, 30 seeds
- **Full playthroughs: 50/50 seeds completed** by autopilot through real collision
- **Isometric rasterisation: exact.** Diamonds tile with zero gaps *and* zero overdraw; zero seam
  pixels under random elevation; zero pixels owned by the wrong tile; integer upscale exact at
  ×1/×2/×3 with margins cleared. Negative control rejects a 1 px offset
- Building placement: 30 seeds, footprints solid, indexed, disjoint and approachable; two negative
  controls both fire
- Audio callback: **0.141 ms worst case against a 21.333 ms deadline**; zero partial writes, zero
  NaN, no clipping
- Render cost: 0.859 ms + 1.259 ms present of a 16.67 ms budget (~13%), measured at every slice

### NOT verified — be honest about these

- **Whether any of it is fun, or even pleasant to look at in motion.** Everything above is geometry
  and byte counts.
- **Nobody has played it by hand.** Every playthrough was the autopilot. Interact affordance, reach
  radius, movement speed, tree density, whether the isometric camera is comfortable, and whether
  world-aligned input is disorienting — all unjudged.
- **No audio has ever been heard**, only measured
- **Never run on another machine.** [[QA Checklist]]'s "runs clean without dev tools" is unchecked
- Perf measured on one machine (2048×1152, scale ×2) with the window unoccluded. `present` is an
  OS blit whose cost depends on the compositor
- **Pacing after the scale change is unmeasured.** The old 30–82 s shortest-path number was taken
  before buildings existed and before `TILE` doubled. `PLAYER_SPEED` was doubled to keep
  tiles-per-second constant, so it *should* hold, but nobody has re-run it
- Whether `fog_lerp` keeps four canopy shades distinguishable at low reveal — checked by eye at a
  few levels, not measured
- The gating-relaxation fallback **has never fired** (0/50 seeds), so that path is untested
- SDL's own resampler never ran — this device satisfied both 48000 and 44100 exactly
- Generation time not profiled (noticeable when the grid view builds 12 worlds)

---

## 9. Open decisions — status

| Decision | Status |
|---|---|
| Grid vs continuous movement | **RESOLVED** — continuous, 2026-08-02 |
| Rendering approach | **RESOLVED** — isometric 2.5D, 2026-08-04. See [[Isometric Rendering]] |
| Tile size / resolution | **RESOLVED** — 32 px tiles, 960×540 logical, integer-scaled |
| **Input orientation** | **OPEN, and it matters.** `W` currently travels up-right on screen. Screen-aligned input was chosen in planning but is not built. It is a *simulation* change: it rewrites every trajectory and invalidates `--input-test`, `--move-test`'s diagonal assertion and the autopilot's steering. Give it its own slice |
| Landmass size / region count | **Provisional 16.** Pacing needs re-measuring after the scale change |
| Fragment + Found Soul counts | **Provisional 14 + 5.** Awaiting sign-off |
| Kindle: passive radius vs active ping | **Open.** Currently a plain region gate |
| Inventory/tool icons from mockup | **Open.** Treated as pitch-art decoration, not built |

[[Cut List]] is pre-committed if time runs short. **Never cut:** the fog-reveal core feel, the
reachability guarantee, staying under the byte limit, a defined completable end state.

### The size finding, and what it means for planning

The entire isometric pivot — projection, elevation, upscaling, surface detail, seven kinds of
procedural prop, and mix-and-match buildings — cost **9,728 bytes**. All game logic ever written
for this project is about 25 KB. SDL2 is the other 664 KB.

**Bytes are not the constraint and never were.** There is also ~250 KB of reclaimable SDL dead
weight documented in [[Toolchain Setup]] that has never been touched. The real constraint is that
every visual has to be *written*, in C, by hand — so plan against authoring effort, and stop
citing the byte budget as a reason to keep things simple.

---

## 10. How to work on this

Follow [[Agent Prompt]]'s loop, and narrate which stage you are in:
**plan → implement → build → measure → verify → report.**

- Report the exact `.exe` byte size and delta after **every** build
- Treat warnings as defects
- Batch-test ≥20 seeds after any change to generation or placement
- **Any new checker needs a negative control.** Every existing one has one
- **Keep render-only data render-only.** The moment collision reads `height` or `bld_at`, the
  completability proof needs re-arguing instead of re-running
- State plainly what you did **not** verify. Never claim audio sounds right or that something
  feels good — those need a human
- Every new note must link to an existing one; orphans break the graph
- Log every session to `devlog/YYYY-MM-DD-session-NN.md`, and append a `## Session NN` section if
  a file for today already exists. Update [[INDEX]] every session

---

## 11. What to do next

Three candidates, in the order I would take them.

**1. Get someone to play it.** This is now the third handover in a row saying nobody has. Two
provisional counts, the pacing number, the tree density, and whether the isometric camera is
comfortable are all blocked on it — and it costs an afternoon, not a week.

**2. Restoration-driven rebuild** — the highest-value *building* task left. A building's drawn
state becomes a function of its region's `restoration` float, expressed as **parts suppressed**
rather than a second set of art: at 0.0 walls only and gapped, at 0.4 roof partial, at 0.7 roof and
windows complete, at 1.0 windows lit and chimney smoking. This makes the game's own hook —
*restore memories, rebuild lives, return colour and life to the world* — literally visible, and it
reuses `regions[].restoration`, which already eases smoothly. Perhaps 60 lines.

**3. Week 4 — [[Audio and Synth]].** Softsynth, pattern data, the five named layers, SFX, and
callback profiling under full five-layer load. `try_restore` is where a layer would be switched
on; the hook already exists. Current callback headroom is ~99%.

**Schedule reality.** Today is 2026-08-04; the deadline is 2026-09-04. Week 4 (a softsynth from
zero) and Week 5 (bitmap font, save/load, HUD, minimap, win state, game-feel pass) are both
completely untouched, and the isometric pivot consumed time that was not budgeted for it.
**Judging order is finished → under size → fun.** A beautiful isometric village with no audio, no
font and no save scores worse than the flat build with all three. If something has to give, take
it from [[Cut List]] — the most likely candidate is cutting Kindle and shipping Wade + Climb only.
