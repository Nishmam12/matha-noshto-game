---
tags: [process, handover, wayfarer]
updated: 2026-08-05
exe_size_bytes: 690688
---

# Handover — Wayfarer

**Read this first if you are picking this project up cold.** It is the single-file context dump:
what exists, how to build it, what was decided and why, what is verified, and every trap that
already cost time once.

Hub: [[Wayfarer MOC]] · Rules of engagement: [[Agent Prompt]] · Game plan: [[Overview]] ·
Visual identity (provisional): [[Art Bible]] · Build environment: [[Toolchain Setup]] ·
Renderer: [[Isometric Rendering]] · **Forward roadmap, phase by phase:** [[Phase Roadmap]] ·
Running log: [[INDEX]]

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
| Assets | **Zero external files.** No PNG/WAV/TTF/OGG/MP3/GLB. Everything procedural, or baked into a compiled-in header — see [[Art Bible]] §8 |
| Excluded libraries | SDL_image, SDL_ttf, SDL_mixer — rendering, fonts and audio are all hand-rolled |
| Judging order | **finished → under size → fun** |

"1.44 MB" has three definitions in common use. We build against the smallest.

> **The size constraint is not the binding one, and has never been.** The entire isometric pivot —
> projection, elevation, upscaling, surface detail, seven kinds of procedural prop, mix-and-match
> buildings, an island generator, village clustering, roof face shading and a fog rewrite — has cost
> **21,536 bytes** against 785,408 free. All game logic ever written for this project is a rounding
> error next to SDL2's ~664 KB. **Plan against *authoring effort*, and against *judgement* — the
> renderer being byte-cheap does not make it look right on the first attempt, and it hasn't yet.**

### The project's framing — read this before touching anything

**This is a backbone build.** The user is building the playable foundation while teammates design
the game in parallel; they will hand over a real map, characters and assets later. Current art is
**provisional** — good enough to keep only if it turns out good enough. Two consequences that shape
every decision below:

1. **Art goes behind a swap seam**, not fused into the renderer, so a teammate's asset replaces a
   named entry point rather than requiring a renderer rewrite. See [[Art Bible]] §8 and
   [[Phase Roadmap]] Phase 07.
2. **Zero external files still applies to the team's art.** They cannot hand over a PNG or a GLB and
   have it load at runtime. It has to be baked into a compiled-in C header at build time. Tell them
   this early — an untracked **`tree.glb`** (36 KB, binary glTF) has been sitting at the vault root
   since before this framing was agreed, and it is exactly the mistake this note exists to prevent.
   It can be a *bake input* if someone renders it to sprite frames first. It can never ship as-is.

Full memory of this framing, plus which Claude skills apply to this project and which explicitly do
not, lives outside the vault at `C:\Users\nabil\.claude\projects\g--1-44mb-game\memory\` — see §10.

---

## 2. Current state

| | |
|---|---|
| **`build\wayfarer.exe`** | **690,688 bytes** — 749,312 under the ship target |
| `build\wayfarer-selftest.exe` | 717,312 bytes — **not a deliverable**, never shipped |
| `src\main.c` | ~4,830 lines, single translation unit |
| Warnings | zero, under `-Wall -Wextra` |
| Plan progress | Weeks 1–3 (original plan) complete. Isometric pivot complete. **Phases 00–02 done; Phase 03 (font + tuning overlay) code-complete, awaiting one human check** — see [[Phase Roadmap]]. Audio, save and UI are all still untouched |

### What actually works right now

- Procedural **island**, not a cave: coastline, ocean with a stepped sea floor, inland rock
  outcrops, **108×60 tiles at 24 px**, seeded, regenerable in-game with **R**. See [[Phase Roadmap]]
  Phase 01
- **Screen-aligned input** — `W` moves up on screen, verified per direction, with a negative control
  that rejects the old world-aligned mapping. See [[Phase 04 - Traversal]]
- **An eased follow camera** with a deadzone, replacing the per-frame hard snap
- **Isometric 2.5D renderer**: 2:1 diamonds, elevation with cliff faces, band-sweep depth sort
- **Procedural scenery**: layered trees with round `fill_ellipse` canopies (not the earlier
  axis-aligned lollipops), bushes, rocks, reeds, flowers, crystals, stumps — all from a per-tile
  hash, none stored. Every prop casts a ground-contact shadow
- **Procedural buildings**: clustered into up to 3 village sites rather than scattered over every
  open plot, mean ~12 per world, 640,000 mix-and-match combinations. Roofs now have a left/right
  face split (`iso_diamond_lr`) so they read as pitched rather than as flat plates
- **Fog rewrite**: unrevealed land resolves toward a light cool haze that keeps a fixed fraction of
  its own luminance contrast, rather than crushing to a dark, cave-like grey. This — not the
  camera — was the cause of "traversal feels suffocating"; see [[Fog and Reveal]]
- **A first palette pass**: sage grass, a value-corrected stone ramp, a stepped water depth ramp,
  and two dead-reading autumn tree palettes deleted. See [[Art Bible]] for the full provisional
  system, most of which is not yet applied everywhere
- 960×540 logical framebuffer, integer-scaled into the window; F11 borderless fullscreen
- Region graph: 16 connected regions with terrain types and ability gates
- Continuous movement, swept AABB tile collision, fixed 60 Hz simulation
- Ability gating enforced in collision (Wade / Climb / Kindle)
- 14 fragments + 5 Found Souls placed with a **proven** reachability guarantee — re-*run*, not
  re-argued, after every generation change so far, because collision only ever reads `solid` and
  `regions[].terrain`
- Restoration loop, Found Soul states, win condition, 4-stage world-growth read
- Restore confirm beat (audio), real-time safe: 0.141 ms worst case against a 21.333 ms deadline
- Debug overlay, 12-seed grid view, title-bar stats, render instrumentation
- **A 5×7 bitmap font** (`draw_text`, `draw_text_shadow`) and an **F3 live tuning overlay** for the
  fog constants — `TAB` cycles rows, `-`/`=` adjust. **Both are self-test-only and cost the shipping
  build 0 bytes**; `fog_lerp` reads `FOG_*_V` macros that expand back to the literals in a release
  build. See [[Phase 03 - Legibility Tools]]
- **The game has been played by a human being, once**, and read as "slightly enjoyable" — see §8

### What does NOT exist yet

- **Any music.** The layered synth is still ahead. Only the confirm beat exists
- **Any text in the shipping build.** The font exists but is gated behind `WAYFARER_SELFTEST`,
  because nothing in the release build calls it yet — Found Soul restoration lines and a HUD are
  Phase 09/11. Un-gating is a one-line change once a real caller exists, and the gate is what keeps
  the +0-byte property true by construction rather than by remembering
- **Save/load**
- **Any animation at all.** Nothing sways, shimmers, bobs or smokes. The world is static
- **The player is still an orange square** (18×18 at the current tile size). No layered character,
  no walk cycle, no facing
- **Buildings do not respond to restoration.** The ruin→whole rebuild is designed but not built
- **No rivers, no bridges, no waterfalls.** Water is currently ocean only, with no inland flow
- **No worn paths between buildings.** The village reads as buildings-in-a-field, not as inhabited
- **No `--land-test` or `--fog-test`.** Both the island generator and the fog rewrite shipped without
  a checker of their own, which breaks this project's own "every checker needs a negative control"
  rule. This is tracked as rule debt, not forgotten — [[Phase Roadmap]] Phase 05
- Idle sway/breathe for Found Souls
- Audio-layer-per-restore (the hook is wired; the layers are not)

### Git

Remote: **`https://github.com/Nishmam12/matha-noshto-game`** — private, branch `main`.

```
dd8cfef  Phase 04: screen-aligned input, and an eased follow camera
e03138d  Rescale the world: TILE 32 -> 24, and make TILE an honest knob
60b4e3a  Bitmap font and a live fog-tuning overlay, both at +0 shipping bytes
a2e87a4  Handover rewrite, and a phase-by-phase roadmap for the next chat
e5c8942  Roof face shading, and rewrite the fog so distance reads as haze
5ffdb38  Island landform, clustered villages, and rounded foliage
545598f  Handover rewritten for the isometric build, plus devlog and INDEX catch-up
d622ed0  docs: record the isometric decision and supersede the flat-geometry notes
```

**Everything is committed.** Nothing is pushed to the remote yet — check before assuming.

**Do not add `Co-Authored-By` trailers to commits.** This was asked for explicitly and one had to
be stripped and force-pushed once already. It is recorded in persistent memory (§10) so it should
never need saying again.

`build/` and `.obsidian/` are gitignored. `wayfarer.exe` is therefore not in the repo — attach it
to a GitHub Release if a playable download is wanted.

> Two files at the vault root that nobody in any build session created: an empty `devlog.md`, and an
> untracked **`tree.glb`**. See §1 above — the `.glb` is not a hypothetical risk, it is the exact
> shape of mistake the asset-bake decision exists to prevent. Both left alone rather than deleted
> without asking.

> `.claude/` also appears untracked in `git status`. That is this session's own harness state
> (plugin/skill config), not project content — leave it alone; it is not part of the game.

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

**New trap this session, worth its own line even though §7 also has it:** if `wayfarer.exe` is
running (someone is playing it), `build.ps1`'s link step fails with `Permission denied` — not a
build error, a file lock. `Get-Process -Name wayfarer` tells you. Close the game, rebuild. Do not
assume a failed release link means the code is wrong if the self-test build succeeded moments
earlier from the same source.

---

## 4. The test suite — run this before believing anything

```powershell
.\build.ps1 -SelfTest
$e = ".\build\wayfarer-selftest.exe"

& $e --iso-test                             # rasteriser exactness, seams, depth, upscale
& $e --font-test --shot charset.bmp         # glyph table vs render, + stride negative control
& $e --village-test --seeds 30 --seed 1     # building placement invariants
& $e --rng-test    --seed 1                 # PRNG: reproducibility, stream independence, bias
& $e --move-test   --seeds 20 --seed 1      # collision, no drift, determinism, diagonal speed
& $e --region-test --seeds 30 --seed 1      # region graph structure + coverage
& $e --reach-test  --seeds 50 --seed 1      # reachability invariant + negative control
& $e --gating-test --seeds 30 --seed 1      # walk-reachable == graph-reachable, all 4 tiers
& $e --play-test   --seeds 50 --seed 1      # full headless playthroughs to completion
& $e --audio-test 3000 --sfx                # callback timing under restore-beat load
& $e --autoplay 20000 --seed 3              # windowed autopilot; watch restoration happen
& $e --input-test 4000 --seed 5             # real keyboard path, reports position delta
& $e --frames 400 --perf --seed 4           # render/present/sleep ms, px and calls per frame
& $e --frames 60 --seed 4 --overlay --shot out.bmp   # scripted screenshot — see the recipe in §10
```

**All currently pass.** Last full run, 2026-08-05, after the Phase 03 work:

```
font    : PASS  2,316 lit px expected from the glyph table and 2,316 rendered;
                negative control caught the off-by-one stride (2,448 vs 2,316)
iso     : PASS  0 px owned by the wrong tile under elevation; upscale x1/x2/x3 exact
village : PASS (0 failures across 30 seeds), mean 12 buildings per world (clustered, not
                scattered — see Phase 01). Both negative controls fire
rng     : PASS (0 checks failed)
move    : PASS (0 failures across 20 seeds); direction-independent speed confirmed
region  : PASS (0 failures across 30 seeds)
reach   : PASS (0 failures across 50 seeds); negative control PASS; gating relaxed on 0/50
gating  : PASS (0 failures across 30 seeds)
play    : PASS (0 seeds could not be completed) — still 50/50 after the landform rewrite
audio   : worst case 0.122 ms of a 21.333 ms deadline; 0 partial writes, 0 NaN, 0 out of range
perf    : render 0.749–0.844 ms, present ~1.3–1.4 ms, ~72,445 calls/frame, ~59.2–60.4 fps
```

**Rule debt, stated plainly:** the island generator (Phase 01) and the fog rewrite (Phase 02) both
shipped without a checker of their own — `--land-test` and `--fog-test` do not exist. Everything
above passing means those changes didn't *break* anything the existing suite watches; it does not
mean the new systems have their own verified invariants yet. See [[Phase Roadmap]] Phase 05.

### The game itself

```powershell
.\build\wayfarer.exe --seed 3
```

`WASD`/arrows move · `E`/`Space` restore · `F1` region overlay · `F2` 12-seed grid ·
**`F11` borderless fullscreen** · `R` regenerate with next seed · `ESC` quit. Stats are in the
**window title** (nothing in the release build draws text yet). `--frames N` runs exactly N frames
then exits 0. `--scale N` forces the window scale.

**Self-test binary only:** `F3` toggles the live fog-tuning overlay, `TAB` cycles the selected row,
`-`/`=` adjust it. `--tune` starts with it already shown, the same way `--overlay` starts with F1
held, so it can be screenshotted without a human at the keyboard.

> **`W` now moves up on screen.** Input was world-aligned until 2026-08-05; if any older note still
> says `W` travels up-right, that note is stale — see decision 28.

---

## 5. Code map — `src/main.c`, in order

Line numbers below were current as of the Phase 03 work and have **drifted by roughly +20 to +250**
since the rescale and Phase 04. Treat them as a map of the file's *order*, not as addresses —
trust the grep, not this table.

| Line | Section | What lives there |
|---|---|---|
| 23 | Tunables | All `#define`s. Everything designers would touch is here |
| ~85 | **Isometric projection** | `ISO_*`, `ELEV_*`, `FACE_*`, `ROOF_L`, void colour |
| 186 | RNG | PCG32, three independent streams (terrain / entities / audio) |
| 279 | Audio | Callback, device open, restore confirm beat |
| 410 | Args | `arg_int`, `arg_flag`, `arg_val` |
| 436 | World | Region/World/Scratch/**Building**/**SURF_\*** structs, terrain enums |
| ~95 | **Fog** | `FOG_TINT_*`, `FOG_KEEP`, and the **`FOG_*_V` / `FogTune` indirection** that makes them F3-adjustable in self-test builds and literal in the shipping one |
| 595 | **Island generation** | `solid_at`, `land_lattice`, `land_noise`, `world_gen` (678) — the island height field |
| 737 | **Building placement** | `place_buildings` — village-site clustering, runs *before* the reachability verifier |
| 832 | Regions | `bfs_open`, `regions_build`, `regions_depth`, `regions_assign_terrain` |
| 1004 | Reachability | `regions_reachable`, `world_solvable`, entity placement, generate-then-verify |
| 1175 | Movement | `tile_blocked`, `player_blocked`, `move_axis`, `sim_step` |
| 1266 | Input | `input_poll` — **world-aligned, see Phase 04** |
| 1279 | Restoration | `entity_in_reach`, `try_restore`, `game_complete`, `world_stage` |
| 1434 | **Heights** | `world_heights` (derived elevation, island- and rock-aware), `height_at` |
| 1527 | World init | `game_init` — wipe, generate, place, flood-fill spawn, verify, derive heights |
| 1623 | **Perf** | `Perf`, counters, `perf_report`. All behind `WAYFARER_PERF` |
| 1689 | Graphics | `fill_rect` (1694), `vspan`, `iso_tile`, `iso_diamond`, `iso_diamond_lr` (2136), `iso_ring`, `fill_ellipse`, `blit_scale` (1854), `tile_hash`, `fog_lerp` (2015), `tile_detail` |
| 1737 | **Bitmap font** (new) | `FONT_*` constants, the flat `FONT_5X7` table (1747), `draw_glyph` (1808), `draw_text` (1825), `draw_text_shadow` (1837). All inside `#if WAYFARER_SELFTEST` |
| 2075 | **House parts** | `BV_*` variant accessors, wall/roof palettes; `draw_building` (2451, roof face-split) |
| 2189 | **Props** | palettes (canopy pruned to 8 live entries), `draw_tree`/`bush`/`rock`/`reed`/`flower`/`crystal`/`stump` (all with contact shadows), `prop_at`, `draw_prop` |
| 2583 | Render | `tile_reveal`, `tile_colour`, `render` (2643) — the band sweep; `render_grid` (2863), `camera_follow` (2935, **still unsmoothed**) |
| 2949 | Window | `pick_scale`, `backbuffer_new`, `present` |
| 3024 | **Tuning overlay** (new) | `tune_adjust` (3047), `tune_draw` (3065) — F3/TAB/`-`/`=`, fog constants only. Self-test-only |
| 3099 | Self-test | Everything else under `#if WAYFARER_SELFTEST` — `font_selftest` at 4288 |
| 4609 | `main` | Fixed-timestep loop, input, debug keys, frame cap |

### Tunables worth knowing — current values, several changed this session

| Constant | Value | Notes |
|---|---|---|
| `TILE` | **24** (was 32) | Diamonds are 48×24. **Changing it now really is free**: every authored dimension goes through `PX()`, so the whole visual scale follows. It did not before — see decision 26 |
| `PX(n)` / `PXF(n)` | — | "n px, as authored at a 32 px tile" (`TILE_REF`). Wrap **every** new hand-authored pixel dimension in it, or that art stops scaling with `TILE` and re-creates the "everything is too big" bug |
| `WORLD_W` × `WORLD_H` | **108 × 60** (was 80×45) | = 2592×1440 world px. Grown so the island keeps its extent while being sampled 1.8× more finely |
| `LOGICAL_W` × `LOGICAL_H` | 960 × 540 | Rasterised size; window is this × an integer scale |
| `PLAYER_SPEED` / `PLAYER_SIZE` | `PXF(220)` / `PX(24)` = 165 / 18 | Both scale with `TILE`; collision is scale-invariant because `player_blocked` divides by `TILE` |
| `REVEAL_TILES` | **7** (was 5) | In *tiles*, so it does not scale with tile size — raised by hand to keep the sight circle ~160 world px |
| `CAM_DEADZONE` / `CAM_EASE` | `PX(30)` / 0.16 | Follow-camera feel. **First guesses, never judged by a human.** Y deadzone is halved because the projection compresses screen y 2:1 |
| `SIGHT_MAX` | **0.50** (was 0.42) | Raised alongside the fog rewrite so walked ground keeps more colour |
| `FOG_TINT_R/G/B` | **60 / 70 / 86** | Was (44, 52, 68) — a *dark* blue-grey. Now a light cool haze. Took three tuning passes; **tune these with the F3 overlay in a self-test build, never by rebuild-and-screenshot again** |
| `FOG_KEEP` | **0.50** | Fraction of a colour's own luminance contrast preserved at reveal 0. Replaces a flat 0.55 luminance scale + 0.45 tint-pull that crushed contrast. Also F3-adjustable |
| `FONT_W` / `FONT_H` / `FONT_SCALE` | **5 / 7 / 2 (new)** | Glyph cell and its logical-pixel magnification. Charset is `0x20`–`0x5F`: uppercase, digits, punctuation. No lowercase |
| `FACE_L` / `FACE_R` | 58 / 76 | Unchanged — terrain side-face shading, per cent |
| `ROOF_L` | **64 (new)** | Roof down-left slope shading, per cent of true colour. New this session — see Phase 02 |
| `ELEV_STEP` / `ELEV_MAX` | 12 / 48 | Unchanged |
| `ELEV_WATER` / `ELEV_LEDGE` | −6 / 16 | Unchanged in value; `ELEV_WATER` now also drives a 4-step sea-floor ramp, see `world_heights` |
| `LAND_SEA` | **0.24 (new)** | Height-field threshold below which a tile is ocean. Lower = bigger island |
| `LAND_ROUGH` | **0.55 (new)** | How far coastline noise pushes the shore in and out |
| `LAND_ROCK_T` | **0.74 (new)** | Outcrop threshold. Raised once already — 0.68 covered ~40% of frame in rock |
| `VILLAGE_SITES` / `VILLAGE_RADIUS` / `VILLAGE_SPACING` | **4 / 12 / 29** | All in *tiles*, so all re-derived by hand for `TILE` 24. Radius/spacing × 32/24 keeps a village the same physical size; sites raised because 3 in a 1.8× larger world read as an empty island |
| `BUILDING_TARGET` / `BUILDING_MAX` | **22** / 40 | Raised with the world size; mean is 21 per world. `BUILDING_MAX` stays the array bound |
| `STOREY_H` / `WALL_BASE` | 14 / 10 | Unchanged |
| `LOBES` | 6 | Unchanged, but lobes are now `fill_ellipse` calls, not `fill_rect` |
| `REGION_COUNT` | 16 | **Hard cap 32** — adjacency is a `Uint32` bitmask |
| `FRAGMENT_COUNT` / `SOUL_COUNT` | 14 / 5 | Combined **must stay ≤ 32** — restored-mask is `Uint32` |
| `TICK_HZ` / `FRAME_HZ` | 60 / 60 | Unchanged |

---

## 6. Decisions already made — do not re-litigate without flagging

Items 1–15 are unchanged from the previous handover (C + static SDL2; our own cut-down SDL2 build;
no `SDL_Renderer`; no `-flto`; continuous movement; PCG32; separate audio/video init; self-test in a
separate binary; the two-contribution fog split; isometric 2.5D; per-column span rasterisation;
render-only decoration; buildings placed before the verifier; decoration from a stateless hash;
stacked-diamond roofs). Full text for those is in git history (`545598f`) if the reasoning is
needed verbatim. New decisions from this session:

16. **The world is generated from a radial height field with layered value noise, not a
    cellular-automaton cave.** The cave gave the *landform itself* — not just decoration — the shape
    of cave noise, which read as random brown lumps with no coastline. Ocean and rock are both
    `solid`; which kind a tile is lives in a new render-only `surf[][]` field. **Collision still
    reads only `solid` and `regions[].terrain`** — this did not weaken decision 12, it extended it
    to a new generator. See [[Phase Roadmap]] Phase 01.
17. **Buildings cluster into village sites rather than scattering over every open plot.** Uniform
    placement read as a suburb the moment the landmass grew past the old cave's size. Up to
    `VILLAGE_SITES` sites, `VILLAGE_SPACING` apart, plots drawn from two summed `rng_below` calls so
    they bunch toward a centre and thin at the edge.
18. **Roofs get a left/right face split (`iso_diamond_lr`), the same trick terrain uses via
    `FACE_L`/`FACE_R`.** A stack of concentric diamonds has no volume regardless of how its steps
    are shaded — every slice is one flat colour. An eave-shadow diamond was tried as a cheaper fix
    first, made it worse (a ring under a ring is still rings), and was reverted; the failed attempt
    is documented in a comment in `draw_building` so it is not retried.
19. **Fog now models aerial perspective: unrevealed land goes lighter and lower-contrast with
    distance, never darker.** The previous blend scaled luminance to 0.55 and pulled toward a dark
    tint, and since walking only ever reveals to `SIGHT_MAX`, ~95% of any screen was one dead colour.
    This — not the camera — was the mechanical cause of "traversal feels suffocating." `fog_lerp`
    remains the single path from true colour to screen colour; only its destination changed.
20. **Assets from the team's eventual art handoff bake into a compiled-in C header at build time —
    they are never loaded at runtime.** Forced by the zero-external-files rule combined with the
    backbone/team framing in §1. Not yet built; the architecture and contract are specified in
    [[Art Bible]] §8 and scheduled as [[Phase Roadmap]] Phase 07.
21. **A provisional [[Art Bible]] exists**, explicitly superseded when the team's real art direction
    lands. It fixes palette ramps, a value hierarchy (walls lightest, foliage darkest, the two
    accent colours reserved), and the sprite/bake contract — see the file itself.
22. **No subagents on this project.** Recorded in persistent memory, not just here: two `Explore`
    subagents died mid-task on a monthly spend limit, and this codebase is one file with one
    Handover — a cold subagent re-derives context that direct reading already has. See §10.

New decisions from the Phase 03 session (2026-08-05):

23. **The bitmap font is hand-rolled, bit-packed, and its array is deliberately FLAT** — indexed
    `idx * stride + row` with the stride passed in, rather than declared `[glyph][row]`. That exposes
    the stride as a seam **so `--font-test`'s negative control can corrupt it**; a 2D array would
    make the off-by-one impossible to express, and an inexpressible fault is one the checker never
    proves it can catch.
24. **The tuning overlay controls render-only constants and nothing else.** `FOG_TINT_*` and
    `FOG_KEEP` live in `fog_lerp` alone, so a keypress shows on the next frame with no regeneration
    and no stale state. `SIGHT_MAX` was excluded because reveal only ever *grows* (lowering it live
    leaves walked ground stale); `LAND_ROCK_T` and `VILLAGE_*` were excluded because they feed
    `world_gen` and would re-run `game_init` — and the reachability verifier — on every keypress.
    **This was put to the user and decided explicitly**, not defaulted into. Widening it later is a
    real design change, not a small extension.
25. **Both the font and the overlay stay behind `WAYFARER_SELFTEST` until a real caller exists.**
    `fog_lerp` reads `FOG_*_V` macros that expand to `FogTune` struct fields in a self-test build and
    straight back to the literals otherwise, which is what makes the +0-byte claim structural rather
    than something to re-measure.

New decisions from the rescale + traversal session (2026-08-05):

26. **All hand-authored pixel dimensions go through `PX(n)`, referenced to a 32 px tile.** Before
    this, `TILE` was a lie: the projection identity held at any size, but props were authored in
    absolute pixels, so shrinking `TILE` shrank the ground and left the trees alone. **Any new art
    code must wrap its dimensions in `PX()`** or it silently opts out of the scale system and
    re-creates "everything is too big".
27. **The world grew to 108×60 as tiles shrank to 24, so the island keeps its size and gains
    resolution rather than shrinking.** Safe because `land_noise` reads normalised coordinates over
    fixed lattices — the island's *shape* is resolution-independent. Constants denominated in
    *tiles* (`REVEAL_TILES`, `VILLAGE_*`) do **not** follow `TILE` and had to be re-derived by hand;
    that asymmetry is the easy thing to forget here.
28. **Input is screen-aligned; `move_axis` is not involved.** `sim_step` rotates screen intent into
    a world velocity through the inverse of the projection basis and normalises by its true length.
    `move_axis` still resolves a world velocity into collision-respecting motion and has no opinion
    about its origin — which is precisely why collision needed re-*running*, not re-*arguing*.
29. **When you rotate a control signal, rotate the decision, not the measurement.** Applying the
    autopilot's deadband *after* rotating its world deltas livelocked every playthrough — see §7.
    Thresholds are judgements about the space the target lives in.

---

## 7. Traps — each of these already cost time once

Items from the previous handover (the `.data`/`.bss` static-buffer trap; gcc needing PATH; SDL's
`-DSDL_DYNAMIC_API=0` patch; `SDL_LOADSO` dependency; the transient linker permission error;
`sizeof` on a decayed pointer; `game_init` must zero the whole `Game`; never run `src/main.c`
through a PowerShell text filter; here-strings breaking `git commit -m`; relative assertions proving
nothing; a verifier that never rejects proves nothing; suspect the harness; screenshots being poor
evidence of *direction* but the right tool for *rendering*; `SetForegroundWindow` blocked for
background processes; `FindWindow(null, ...)` failing from PowerShell; PowerShell 5.1 having no
`&&`/`||`/ternary; a 1920×1080 window not fitting a 1920×1080 desktop; a fullscreen window rarely
being an exact multiple of the logical size; the screen clear being mandatory now) **are all still
true and are not repeated in full here — see git history at `545598f` for verbatim text.**

**New this session:**

- **A running `wayfarer.exe` blocks the release relink with `Permission denied`**, not a build error.
  This happened because the user was actively playing the game while a rebuild was attempted.
  `Get-Process -Name wayfarer` before assuming the build is broken. The self-test binary uses a
  different filename and is unaffected.
- **A per-tile height jitter checkerboards.** The first attempt at breaking up flat outcrop tops
  hashed height jitter per individual tile at ±4; since adjacent tiles almost always disagreed, the
  rasteriser drew a visible step between every pair and the result was a checkerboard, not rock.
  Fixed by hashing on the 2×2 block instead of the tile, at a smaller ±2. **Any per-tile visual
  jitter needs to be checked for this before it ships** — the fix is "hash a coarser unit," not
  "reduce the amplitude," though both were tried.
- **A stack of concentric diamonds has no volume, however its steps are shaded.** This looks like a
  shading problem (wrong colours per step) but is actually a *geometry* problem (no left/right
  distinction exists anywhere in the shape). No amount of retuning the per-step colour fixes it;
  the fix has to add a face split. Costly to learn by iterating on colour first — don't.
- **Fog and palette constants were tuned by guess-rebuild-screenshot, three passes, and it thrashed.**
  Pass 1 undercorrected (still dark). Pass 2 overcorrected (washed-out uniform grey). Pass 3 found
  stone had *also* been pushed too light in an earlier commit and was now the brightest surface in
  the world, fighting the fog fix. **This is the direct argument for building the live tuning
  overlay (Phase 03) before doing more colour work by hand** — every future palette decision should
  be made with a slider and instant feedback, not a rebuild-and-look loop.
- **A `village_selftest` mean can silently reflect an unintended threshold change**, not a bug in
  the test. When `VILLAGE_RADIUS` went from 6 to 9 the mean building count moved from 5 to 12 with
  every existing test still passing — the tests check placement *validity*, not placement *density*
  against a design target. If a density number matters, it needs its own assertion, not an eyeball
  of the printed mean.

**New in the Phase 03 session (2026-08-05):**

- **A checker that derives its reference from the same table it is checking cannot catch a wrong
  table.** `--font-test` counts lit pixels against `FONT_5X7` itself. Two glyphs (`=` and `>`) were
  entirely blank in the table, so they were blank in the reference too, the counts matched perfectly,
  and the test passed while the overlay rendered its own help line as `TAB ROW  -  ADJUST` with an
  invisible row cursor. **Found by screenshot, not by test** — the same lesson as the lollipop trees
  and the ziggurat roofs. This is not a fixable flaw in that checker; it is the boundary of what
  pixel-counting can prove, and it is why every visual slice still gets looked at.
  (The flip side is genuinely good and worth keeping: because the reference is derived, adding those
  glyphs moved the expected count 2,236 → 2,316 with **no test edit**. A hardcoded number is how a
  checker quietly stops checking.)
- **Scaled rendering makes pixel-count expectations wrong by a clean multiple, which looks like a
  real bug.** `--font-test`'s first run expected 559 and got 2,236 — exactly 4×, because `FONT_SCALE`
  is 2 and every font pixel is a 2×2 block. The checker was right and the expectation was wrong.
  When a count is off by a suspiciously round factor, suspect the units before the code.

**New in the rescale + traversal session (2026-08-05):**

- **Rotating a control signal and thresholding it afterwards livelocks.** The autopilot's deadband
  was applied to the *rotated* deltas: `ddx=+0.5, ddy=−0.5` is inside the rest zone on both world
  axes, but rotates to `sdx=1.0`, which clears the 0.6 threshold. The autopilot twitched where it
  used to rest, overshot by a full step, and oscillated between two tiles forever — 3 of 3 seeds hit
  the 200,000-step cap. **Apply the deadband in the space the target lives in, then rotate only the
  resulting discrete intent.** This cost the most time of anything this session.
- **A livelock in `--play-test` presents as a hang, not a failure.** 200,000 steps × two BFS passes
  over 6,480 tiles is minutes per seed, so `--play-test --seeds 50` just stopped returning and the
  command hit its timeout with no output. **`Select-Object -Last N` hides all progress until the
  command completes**, so the output file was empty and looked like nothing had run. Re-run with a
  small `--seeds` count and *no* output filter to turn "something is slow" into a diagnosis.
- **A test can encode the very assumption the change is removing.** `speed_selftest` measured world-x
  displacement under `in.right` — fine while input was world-aligned, but a *correct* screen-aligned
  simulation now reports 0.707 of the speed and "fails". The fix was not to retune the number: it was
  to notice the invariant was never about axes (travel *distance* is direction-independent) and
  assert the basis-independent thing instead, so the next orientation change doesn't rewrite it again.
- **Growing the world does not scale constants denominated in tiles.** `REVEAL_TILES`, `VILLAGE_RADIUS`,
  `VILLAGE_SPACING`, `VILLAGE_SITES` and `BUILDING_TARGET` all had to be re-derived by hand when
  `TILE` changed, because `PX()` scales *pixels* and these are *tile counts*. Nothing warns about
  this; the sight radius silently shrinks and the villages silently thin out.

---

## 8. Verified vs NOT verified

### Verified — measured, not assumed

Everything from the previous handover's list still holds (builds clean; PRNG properties; collision
determinism; region graph invariants; reachability with a negative control; gating parity across all
4 tiers; **50/50 playthroughs — re-verified after the landform rewrite, still 50/50**; exact
isometric rasterisation with a negative control; building placement invariants with two negative
controls; audio callback timing; render cost). New this session:

- **The game has been played by a human being, for the first time.** Four consecutive handovers
  carried "nobody has played it by hand" as a standing, named risk. On 2026-08-04 the user played it
  and reported: *"even though the gameplay is in early stage, it did feel slightly enjoyable."* This
  is one datapoint from the person who wrote the design, not a QA pass, but the specific risk "we
  have built something nobody has ever moved around in" is retired. [[QA Checklist]]'s "runs clean
  on a machine without dev tools" — a *different* item — is still unchecked.
- **The island generator does not weaken the collision invariant.** `solid` is still the only thing
  `tile_blocked` reads; the full test suite, including the 50-seed reachability and playthrough
  tests, was re-run (not re-argued) after the generator was replaced and stayed green.
- **Render cost did not regress from adding `fill_ellipse` and the roof/fog changes.** Measured
  0.749–0.844 ms across runs, against the previous session's 0.859 ms baseline — if anything, faster.
- **The font and tuning overlay cost the shipping build exactly 0 bytes.** 690,688 before and after,
  re-confirmed *after* `fog_lerp` was rewritten to read `FOG_*_V` — that was the change that could
  actually have broken it, and checking only after adding the font would have proven the easy half.
- **`--font-test` rejects an off-by-one glyph stride**, 2,448 px against an expected 2,316. The
  negative control fires, so the checker is known to have teeth.
- **The full suite still passes with `fog_lerp` modified** — re-*run*, not re-argued: iso, village
  (30), rng, move (20), region (30), reach (50 + control), gating (30), play (50/50), audio.
- **The tile rescale and the input rotation did not weaken the completability proof.** `move_axis`
  and the collision inputs were untouched by both; the full suite including 50/50 playthroughs was
  re-*run* after each, on a world with 1.8× the tiles.
- **`W` moves up on screen, per direction, with a negative control.** All 8 directions travel
  165.00 px in 60 ticks and land in the right screen direction; the control replays the old
  world-aligned mapping and is rejected 4 of 4.
- **The rescale cost −512 bytes and no render time** (0.824 ms, unchanged) despite 37% more draw
  calls, because it is the same screen area drawn as finer tiles.

### NOT verified — be honest about these

- **Nobody has played at the new scale, or driven the new camera.** The screenshots say the world is
  denser and better-proportioned; whether 24 px tiles are *nice to walk around* is a different
  question. `CAM_DEADZONE`/`CAM_EASE` are first guesses and "does the easing feel right" cannot be
  claimed from here.
- **Rock outcrops read as scattered pale blocks under fog at the new scale.** More, smaller outcrops
  are visible at once and stone is still the lightest large surface, so they pop out of the haze as
  floating cubes. Same value-hierarchy fight [[Art Bible]] describes and Session 03 already had once
  — and now exactly what the F3 overlay exists to settle.
- **The overlay's liveness is proven by construction, not by a scripted keypress.** `fog_lerp` reads
  the struct the keys write, but no automated run presses a key and diffs two frames.
- **The camera ease runs per frame, not per simulation tick.** Stable while the frame cap holds;
  it would drift on a machine that cannot hold it. Known simplification, not an oversight.
- **Whether it is fun beyond one early, positive, informal reaction.** One playtest is not QA.
- **Whether the fog and palette values are actually *right***, as opposed to "no longer obviously
  wrong." They were tuned by eye, by one person, in three iterative passes, with no measurement of
  shade separability under fog. `--fog-test` does not exist. See Phase 05.
- **Whether the island generator produces a good *distribution* of coastline shapes, island sizes,
  or rock coverage across many seeds** — `--land-test` does not exist, so this has only been checked
  on the 2–3 seeds that got screenshotted, not swept.
- **Whether the village clustering produces villages that read as villages across many seeds** — the
  clustering logic has a structural test (via `--village-test`, which checks *validity*) but no test
  of *how it looks*, which is the actual goal.
- **No audio has ever been heard**, only measured — still true, unchanged.
- **Never run on another machine.** [[QA Checklist]]'s "runs clean without dev tools" is unchecked —
  still true, unchanged.
- **Pacing has not been re-measured** since either the tile-size change or this session's landmass
  rework. The island's open-ground fraction is different from the old cave's; nobody has timed a
  shortest-path clear against it.
- SDL's own resampler never ran — unchanged.
- Generation time not profiled — unchanged, and now slightly more expensive (three noise lattices
  sampled per tile instead of a cellular automaton pass), though not measured.

---

## 9. Open decisions — status

| Decision | Status |
|---|---|
| Grid vs continuous movement | **RESOLVED** — continuous, 2026-08-02 |
| Rendering approach | **RESOLVED** — isometric 2.5D, 2026-08-04 |
| Tile size / resolution | **RESOLVED** — 32 px tiles, 960×540 logical, integer-scaled |
| Landform generation method | **RESOLVED, this session** — radial height field + layered noise, not a cave. See decision 16 |
| Building placement pattern | **RESOLVED, this session** — clustered village sites, not uniform scatter. See decision 17 |
| Fog destination colour | **RESOLVED, this session, but tuned by eye and unmeasured** — light haze, `FOG_KEEP` contrast preservation. See decision 19 and Phase 05 |
| Asset pipeline for team-authored art | **RESOLVED, this session** — build-time bake to a compiled-in header, never runtime load. See decision 20, [[Art Bible]] §8, Phase 07 |
| **Input orientation** | **RESOLVED, 2026-08-05** — screen-aligned, `dd8cfef`. See decision 28 |
| Camera easing | **RESOLVED in mechanism, OPEN in feel** — deadzone + exponential ease shipped, but `CAM_DEADZONE`/`CAM_EASE` are first guesses nobody has driven by hand |
| **Art scale** | **RESOLVED, 2026-08-05** — `TILE` 24 with everything authored through `PX()`. Whether 24 is the *right* number is still a judgement call; it is now a one-line change to try another |
| Landmass size / region count | **Provisional 16 regions over a now 1.8× larger tile grid.** Pacing still unmeasured, and the regions are now bigger in tiles than anything was measured against |
| Fragment + Found Soul counts | **Provisional 14 + 5.** Unchanged, still awaiting sign-off |
| Kindle: passive radius vs active ping | **Open.** Unchanged |
| Inventory/tool icons from mockup | **Open, and now explicitly addressed in [[Art Bible]] §7**: out of scope per [[Save and UI]]'s "no HUD clutter," reinstating any of it is its own flagged decision |

[[Cut List]] is pre-committed if time runs short. **Never cut:** the fog-reveal core feel, the
reachability guarantee, staying under the byte limit, a defined completable end state.

### The size finding, restated with current numbers

Everything built across both sessions this cycle — the island generator, village clustering,
`fill_ellipse`, contact shadows, the roof face split, and the fog rewrite — cost **1,536 bytes**
(689,152 → 690,688). All game logic ever written for this project remains a rounding error next to
SDL2. **Bytes are still not the constraint.** Authoring judgement is — see the traps section on
tuning by guess-and-rebuild, which cost real session time this cycle for zero byte cost.

---

## 10. How to work on this

Follow [[Agent Prompt]]'s loop, and narrate which stage you are in:
**plan → implement → build → measure → verify → report.**

- **Read [[Phase Roadmap]] before picking a task.** It sequences everything left, with a definition
  of done and a verification gate per phase — do not re-derive the ordering from scratch each
  session.
- Report the exact `.exe` byte size and delta after **every** build.
- Treat warnings as defects.
- Batch-test ≥20 seeds after any change to generation or placement.
- **Any new checker needs a negative control.** Every existing one has one — `--land-test` and
  `--fog-test` are the two currently missing this, and that is tracked debt, not an oversight to
  repeat.
- **Keep render-only data render-only.** The moment collision reads `height`, `bld_at`, or `surf`,
  the completability proof needs re-arguing instead of re-running.
- **Do not tune colour or shading by rebuild-and-screenshot for more than one or two passes.** If a
  third pass is needed, that's the signal to build the live tuning overlay (Phase 03) instead of
  continuing to guess.
- State plainly what you did **not** verify. One playtest is not many; never claim audio sounds
  right or that something feels good without a human saying so.
- Every new note must link to an existing one; orphans break the graph.
- Log every session to `devlog/YYYY-MM-DD-session-NN.md`, and append a `## Session NN` section if
  a file for today already exists. Update [[INDEX]] every session.

### Persistent memory — read this too, it survives across chats

Outside this vault, at `C:\Users\nabil\.claude\projects\g--1-44mb-game\memory\`, there is a small
set of memory files that a fresh session should pull in automatically. They cover things that don't
belong in a vault note because they're about *how to work*, not *what the game is*:

- **`wayfarer-skill-policy.md`** — which Claude Code skills genuinely apply to this project (a
  small list: `art-bible`, `run`, `code-review`, `simplify`) and the much longer list of installed
  skills that target web/mobile stacks and do not transfer to a C program writing pixels into an SDL
  surface. Check this before reaching for an unfamiliar skill.
- **`wayfarer-screenshot-recipe.md`** — the exact commands to actually *see* what the renderer
  produces: `--shot` and `--overlay` exist only in the **self-test** binary, output BMP, and need a
  conversion step before they can be read as an image. This has been used every single visual
  session so far and will be needed again.
- **`wayfarer-asset-pipeline.md`** — the zero-external-files rule and the build-time bake decision,
  stated for an audience that might not read [[Art Bible]] §8 first.
- **`wayfarer-team-context.md`** — the backbone/provisional-art framing from §1 of this document,
  including the user's own words about what "good enough" means for keeping current art.
- **`no-subagents-on-wayfarer.md`** — why this project's sessions read `src/main.c` directly instead
  of delegating to subagents, and what happened the one time that was tried.

If a future session doesn't have these loaded, that's worth noticing and fixing, not working around.

---

## 11. Roadmap — see [[Phase Roadmap]] for the real detail

This handover intentionally does **not** duplicate the forward plan. `design/phases/` holds one file
per phase — what it is, why it's sequenced where it is, its definition of done, exactly which
functions and lines it touches, and its verification gate. The index is [[Phase Roadmap]].

**Current position, in one paragraph:** Phases 00–04 are **done**. 00–02 (memory + skill policy +
Art Bible; island landform + village clustering; roof shading + the fog rewrite) as `5ffdb38` and
`e5c8942`; **Phase 03** (bitmap font + F3 fog-tuning overlay, +0 shipping bytes) as `60b4e3a`, its
human-usability gate discharged the same day; **Phase 04** (screen-aligned input + eased camera) as
`dd8cfef`. Between 03 and 04 the world was **rescaled** (`e03138d`) — `TILE` 32→24 with every
authored dimension routed through `PX()`, and the grid grown to 108×60 so the island keeps its
extent and gains resolution. **Phase 05** (the missing `--land-test` and `--fog-test`, now three
sessions of rule debt) is next, and it is the cheapest it will ever be to write. After that: water
features, the asset bake pipeline, save/load, the remaining placeholder art, and motion (Phases
06–10) — all timeboxed, with a **hard stop on 2026-08-14** before the ship-critical remainder
(Phase 11: audio, font-dependent HUD, QA, submission) takes over regardless of how much of the art
work is finished.

**Schedule reality, unchanged in substance from the last handover:** today is 2026-08-05; the
deadline is 2026-09-04. Audio (a softsynth from zero) and the rest of Week 5 (save/load, HUD, win
state, game-feel pass) are both completely untouched. **Judging order is finished → under size →
fun.** If something has to give, take it from [[Cut List]] — the most likely candidate remains
cutting Kindle and shipping Wade + Climb only.
