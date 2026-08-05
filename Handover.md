---
tags: [process, handover, wayfarer]
updated: 2026-08-05
exe_size_bytes: 755200
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

## 0. Start here — the ninety-second version

| | |
|---|---|
| **State** | 755,200 bytes, builds clean, full suite green, plays to completion on 50/50 seeds |
| **Deadline** | 2026-09-04. **Hard stop on art/backbone work 2026-08-14** — nine days from now |
| **Do first** | **[[Phase 12 - Dream Realm]]** (new direction, approved 2026-08-05), then [[Phase 08 - Save Load]] |
| **Then** | 10 (motion) → **11 is non-negotiable**. Phases 00–07 are done and 07 absorbed most of 09 |
| **Biggest risk** | **Audio does not exist at all.** A softsynth from zero, plus save/load and a HUD, all still ahead of a 30-day deadline |

**The team's art is in the build as of 2026-08-05.** [[Phase 07 - Asset Seam]] built the
PNG→header bake and pushed **37 real sprites** through it: a 4-direction 4-frame walking
character, 10 buildings, 11 nature props. Cost **+62,976 bytes** against 684,800 still free. That
retires four separate "does not exist" items at once — the orange square, the walk cycle, facing,
and any animation at all.

> **THE OCCLUSION QUESTION IS SETTLED — 2026-08-05. Props fade when they cover the player.** Four
> handovers carried this as the top open item: with props disabled the character renders perfectly,
> with props on she is frequently invisible, because a 96 px tree on a 24 px tile grid covers a
> 48 px character often and **the depth sort is behaving correctly**. The user chose the real fix
> over the two cheap ones — not thinning the trees, not scaling the sprites down, but **fading any
> prop drawn over the player**. See decision 40. It is not yet built.

**Three habits this project runs on**, learned the expensive way:

- **Look at the screen.** Every visual bug of consequence here was found by a screenshot, never by a
  test: lollipop trees, ziggurat roofs, a checkerboard, a roof half a tile off its own walls for
  four sessions. `--shot` is in §10.
- **Re-run the proof, never re-argue it.** Any change touching generation or `solid` means the full
  suite, especially `--play-test --seeds 50`.
- **A checker that has never rejected anything proves nothing.** Every test here has a negative
  control. New ones must too.
- **When a phase file names a test to write FIRST, write it first.** Phase 07 said to write the RLE
  round-trip before trusting the decoder visually. It was written after, and a screenshot loop went
  on suspecting a decoder bug that the test disproved in one run. Screenshots are the slowest
  debugging loop this project has; use them for judgement, not for existence.

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

> **The size constraint is not the binding one, and has never been.** Everything ever built for this
> game — the isometric renderer, elevation, seven kinds of procedural prop, mix-and-match buildings,
> an island generator, village clustering, the fog rewrite, a bitmap font, a live tuning overlay,
> screen-aligned input, an eased camera, rivers and bridges, and five test harnesses with negative
> controls — comes to **22,528 bytes against 747,776 free**. All game logic ever written is a
> rounding error next to SDL2's ~664 KB. Two whole phases (03 and 05) cost **+0 bytes**.
>
> **Plan against *authoring effort*, and against *judgement*.** Every expensive thing this project
> has hit was a judgement call or a wrong assumption, never a byte count: three fog-tuning passes, a
> roof drawn half a tile off its walls for four sessions, an autopilot livelock. Budget your
> attention accordingly.

### The project's framing — read this before touching anything

**This is a backbone build.** The user is building the playable foundation while teammates design
the game in parallel; they will hand over a real map, characters and assets later. Current art is
**provisional** — good enough to keep only if it turns out good enough. Two consequences that shape
every decision below:

1. **Art goes behind a swap seam**, not fused into the renderer, so a teammate's asset replaces a
   named entry point rather than requiring a renderer rewrite. See [[Art Bible]] §8 and
   [[Phase Roadmap]] Phase 07.
2. **Zero external files still applies to the team's art.** They cannot hand over a PNG or a GLB and
   have it load at runtime. It has to be baked into a compiled-in C header at build time. This was
   not hypothetical: an untracked **`tree.glb`** (36 KB, binary glTF) sat at the vault root for
   several sessions and was exactly the mistake this note exists to prevent. **It has since been
   deleted — verified gone 2026-08-05.** The rule stands for the next one: a model can be a *bake
   input* if someone renders it to sprite frames first, and can never ship as-is.

Full memory of this framing, plus which Claude skills apply to this project and which explicitly do
not, lives outside the vault at `C:\Users\nabil\.claude\projects\g--1-44mb-game\memory\` — see §10.

---

## 2. Current state

| | |
|---|---|
| **`build\wayfarer.exe`** | **755,200 bytes** — 684,800 under the ship target |
| `build\wayfarer-selftest.exe` | 793,600 bytes — **not a deliverable**, never shipped |
| `src\main.c` | ~6,500 lines, single translation unit |
| `src\art_data.h` | **GENERATED** by `tools/bake.ps1`, committed. 37 sprites, 63,600 bytes of const data. Never edit by hand |
| Warnings | zero, under `-Wall -Wextra` |
| Plan progress | Weeks 1–3 (original plan) complete. Isometric pivot complete. **Phases 00–07 all done**; 07 absorbed most of 09. See [[Phase Roadmap]]. Audio, save and UI are all still untouched |

> **If you are starting here: settle the player-occlusion question in §0, then start
> [[Phase 08 - Save Load]].** Nothing is half-finished behind you.

### What actually works right now

- Procedural **island**, not a cave: coastline, ocean with a stepped sea floor, inland rock
  outcrops, **108×60 tiles at 24 px**, seeded, regenerable in-game with **R**. See [[Phase Roadmap]]
  Phase 01
- **Screen-aligned input** — `W` moves up on screen, verified per direction, with a negative control
  that rejects the old world-aligned mapping. See [[Phase 04 - Traversal]]
- **An eased follow camera** with a deadzone, replacing the per-frame hard snap
- **Rivers, bridges and waterfalls.** Rivers descend a BFS distance-to-sea field from the interior
  to the coast; bridges deck them. **A bridge is not a collision special case** — it clears `solid`,
  so collision, the region graph and the verifier all see a crossable tile through the path they
  already used. River beds **terrace** by that same field carried out to a render-only `sea_dist`,
  3–4 levels per river, so `iso_tile` draws a water-coloured side face at each drop — a waterfall
  from the rasteriser that already existed. See decisions 32–33, 35 and
  [[Phase 06 - Water And Bridges]]
- **Isometric 2.5D renderer**: 2:1 diamonds, elevation with cliff faces, band-sweep depth sort
- **The team's baked art, through a swap seam.** `tools/bake.ps1` turns authored PNGs into
  `src/art_data.h` at build time; nothing decodes a PNG at runtime. **A walking character**
  (4 directions × 4 frames, driven by screen-space intent), **10 building sprites**, **11 nature
  props**. Each category dispatches through one table — `prop_art[]`, `building_sprite_id()`,
  `player_frames[][]` — and deleting a row falls straight back to the procedural routine, which is
  what makes the swap reversible. See decisions 37–39 and [[Phase 07 - Asset Seam]]
- **Procedural scenery, still present as the fallback**: layered trees with round `fill_ellipse`
  canopies (not the earlier axis-aligned lollipops), bushes, rocks, reeds, flowers, crystals,
  stumps — all from a per-tile hash, none stored. Flowers, crystals and stumps are *still drawn
  this way*, because no delivered sprite matches them
- **Procedural buildings**: clustered into up to 4 village sites rather than scattered over every
  open plot, **mean 21 per world**, 640,000 mix-and-match combinations. Roofs have a left/right face
  split (`iso_diamond_lr`) so they read as pitched; the whole facade (windows per storey, doors on
  the ground line) is derived from the wall-top diamond. User's verdict: *"fine, not perfect but
  workable"*
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
- Restore confirm beat (audio), real-time safe: 0.136 ms worst case against a 21.333 ms deadline
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
- **Any ambient animation.** Nothing sways, shimmers, bobs or smokes. The *character* now walks
  (4 frames per direction), but the world around her is static
- **Buildings do not respond to restoration.** The ruin→whole rebuild is designed but not built
- **No worn paths between buildings.** The village reads as buildings-in-a-field, not as inhabited
- Idle sway/breathe for Found Souls
- Audio-layer-per-restore (the hook is wired; the layers are not)

### Git

Remote: **`https://github.com/Nishmam12/matha-noshto-game`** — private, branch `main`.

```
33f4cd2  Track tools/bake.ps1, which .gitignore was silently swallowing
ecb2649  docs: catch up devlog/INDEX.md with latest session entry
c884d95  docs & devlog: update Phase 07 documentation, handover notes, and main.c tweaks
df480a6  Phase 07: add sprite decoder selftest, baked art_data header, and source PNG assets
378e8b7  docs: record the art handoff, and the pixel-density question it makes blocking
0e85464  Phase 06, part 2: waterfalls, and bridges stop being an argument
ef09d9e  Handover for a fresh chat: Phase 06 half-done, and the asset answer
e42f2f4  Phase 06, part 1: rivers that reach the sea, and bridges that cross them
```

**`assets/` (217 files, 1.2 MB) IS committed** as of `df480a6` — the source PNGs plus their Godot
`.import` sidecars. The sidecars are editor metadata that nothing reads; whether they should be in
the repo at all is still undecided.

**Everything is committed AND pushed, as of 2026-08-05.** `main` is at `ae788b4` on the remote; the
two commits this handover previously listed as unpushed (`ae788b4`, `33f4cd2`) are up. Phase 12's
work lives on **`feat/phase-12-dream-realm`**, also pushed. Older notes saying "nothing is pushed to
the remote yet" are stale.

**Do not add `Co-Authored-By` trailers to commits.** This was asked for explicitly and one had to
be stripped and force-pushed once already. It is recorded in persistent memory (§10) so it should
never need saying again.

`build/` and `.obsidian/` are gitignored. `wayfarer.exe` is therefore not in the repo — attach it
to a GitHub Release if a playable download is wanted.

> **THE TEAM'S ART IS IN THE BUILD.** `assets/` holds **130 PNGs** across `buildings/`, `nature/`,
> `player/`, `magical/`, `generated/`, plus 81 Godot `.import` sidecars, 1.2 MB total. **37 of them
> are baked and wired** (player, nature, buildings) via `tools/bake.ps1` → `src/art_data.h`.
> What is deliberately NOT baked, and why:
> - **`magical/`** — 56 portal and crystal effect frames with no caller in the renderer. Baking a
>   sprite nothing draws is pure byte cost; same rule that kept the bitmap font at +0 bytes.
> - **`generated/`** — duplicates of the building sprites plus two sprite *sheets*.
> - **The `.import` files** are Godot editor metadata: not shipped, not baked, and arguably should
>   not be committed at all. That is still undecided.
>
> **The scale worry turned out to be unfounded**: the base unit across the set is 48 px, exactly one
> diamond width at `TILE 24`. The team authored against the scale already shipping, so nothing
> needed re-authoring and no resolution change was required. See §9.
>
> **`tree.glb` is gone** — verified absent from disk on 2026-08-05. Any older note treating it as a
> live risk is stale. An empty `devlog.md` (0 bytes) is still at the vault root, created by nobody
> in any build session; left alone rather than deleted without asking.

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

powershell -File tools\bake.ps1   # ONLY when the art changes -> regenerates src\art_data.h
```

**`tools\bake.ps1` is not part of a normal build.** `src\art_data.h` is committed, so a clean
checkout compiles without ever running it. Re-run it only when a PNG under `assets\` changes, then
commit the regenerated header. It prints the sprite count and the exact const-data byte total.

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
& $e --land-test  --seeds 30 --seed 1       # island coverage, connectivity, buildable ground
& $e --fog-test                             # value hierarchy + shade separability through fog_lerp
& $e --village-test --seeds 30 --seed 1     # building placement invariants
& $e --rng-test    --seed 1                 # PRNG: reproducibility, stream independence, bias
& $e --move-test   --seeds 20 --seed 1      # collision, no drift, determinism, diagonal speed
& $e --region-test --seeds 30 --seed 1      # region graph structure + coverage
& $e --reach-test  --seeds 50 --seed 1      # reachability invariant + negative control
& $e --bridge-test --seeds 200 --seed 1     # bridges are load-bearing (suppression control)
& $e --sprite-test                          # RLE round-trip, baked data, anchors + 2 controls
& $e --gating-test --seeds 30 --seed 1      # walk-reachable == graph-reachable, all 4 tiers
& $e --play-test   --seeds 50 --seed 1      # full headless playthroughs to completion
& $e --audio-test 3000 --sfx                # callback timing under restore-beat load
& $e --autoplay 20000 --seed 3              # windowed autopilot; watch restoration happen
& $e --input-test 4000 --seed 5             # real keyboard path, reports position delta
& $e --frames 400 --perf --seed 4           # render/present/sleep ms, px and calls per frame
& $e --frames 60 --seed 4 --overlay --shot out.bmp   # scripted screenshot — see the recipe in §10
```

**All currently pass.** Last full run, 2026-08-05, after Phase 06 part 2 (waterfalls and
`--bridge-test`) — re-run fresh for this handover rather than carried forward:

```
sprite  : PASS  round-trip 700 px -> 283 bytes -> 700 px pixel-exact; 37 sprites,
                91,049 px from 60,029 RLE bytes (1.52x); anchors all bottom-centre;
                both controls fire (over-long run, wrong frame size)
bridge  : PASS  200/200 bridge-bearing seeds shrank the player's reachable
                component when bridge decking was suppressed
land    : PASS  30 seeds (100 also clean); both controls (drowned map,
                shattered island) fire
fog     : PASS  0 collapsed ramps; 45 colours x 5 reveals, 0 inversions,
                3 collapses; control (contrast-crushing blend) caught
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
play    : PASS (0 seeds could not be completed) — still 50/50 after the waterfall terracing
audio   : worst case 0.136 ms of a 21.333 ms deadline; 0 partial writes
perf    : render 1.065 ms mean (1.931 ms max), frame 16.875 ms = 59.3 fps
          — up 0.2 ms from 0.865 now every prop, building and the player is a
            decoded sprite. Palette-level fog is why it is 0.2 and not 2
```

**`--play-test` now takes ~2 minutes at 50 seeds** — the world is 1.8× the tiles it was. Do not
assume a long-running run has hung; and note that piping it through `Select-Object -Last N` hides
all progress until it finishes, which has already caused one wasted diagnosis (§7).

**The rule debt is paid.** `--land-test` and `--fog-test` landed in [[Phase 05 - Verification
Debt]] (`4847bf5`), each with its own negative control, so every generator and render contract in
the project now has a checker that is known to be able to fail. What they still do **not** cover is
whether any of it *looks good* — see §8.

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

Line numbers below were re-measured at `e42f2f4` (2026-08-05). They drift with every edit — treat
this as a map of the file's *order* and trust the grep, not the table.

| Line | Section | What lives there |
|---|---|---|
| 23 | Tunables | All `#define`s. Everything designers would touch is here |
| ~85 | **Isometric projection** | `ISO_*`, `ELEV_*`, `FACE_*`, `ROOF_L`, void colour |
| 216 | RNG | PCG32, three independent streams (terrain / entities / audio) |
| 309 | Audio | Callback, device open, restore confirm beat |
| 440 | Args | `arg_int`, `arg_flag`, `arg_val` |
| 466 | World | Region/World/Scratch/Building/**SURF_\*** structs, terrain enums; `RIVER_*`/`BRIDGE_SPACING` at 587 |
| ~95 | **Fog** | `FOG_TINT_*`, `FOG_KEEP`, and the **`FOG_*_V` / `FogTune` indirection** that makes them F3-adjustable in self-test builds and literal in the shipping one |
| 620 | **Island generation** | `solid_at`, `land_lattice`, `land_noise`, `world_gen` (742) — the island height field |
| 819 | **Rivers and bridges** (new) | `place_rivers` — BFS distance-to-sea descent, bridges clear `solid`, field carried out to `sea_dist` for waterfall terracing. Runs *before* buildings and the verifier. `g_suppress_bridges` (self-test only) gates the decking step for `--bridge-test` |
| 956 | **Building placement** | `place_buildings` — village-site clustering, also *before* the verifier |
| 1051 | Regions | `bfs_open`, `regions_build`, `regions_depth`, `regions_assign_terrain` |
| 1223 | Reachability | `regions_reachable`, `world_solvable`, entity placement, generate-then-verify |
| 1394 | Movement | `tile_blocked`, `player_blocked`, `move_axis`, `sim_step` — **reads `solid` and `regions[].terrain` only, still** |
| 1485 | Input | `input_poll` — screen-aligned since Phase 04 |
| 1498 | Restoration | `entity_in_reach`, `try_restore`, `game_complete`, `world_stage` |
| 1679 | **Heights** | `world_heights` — derived, render-only; island/rock/ocean/**bridge**/**river (terraced)** branches. Bridge is checked *before* river; see decision 35 |
| 1785 | World init | `game_init` — wipe, generate, **rivers**, buildings, flood-fill spawn, verify, derive heights |
| 1883 | **Perf** | `Perf`, counters, `perf_report`. All behind `WAYFARER_PERF` |
| 1949 | Graphics | `fill_rect`, `vspan`, `iso_tile`, `iso_diamond`, `iso_diamond_lr`, `iso_ring`, `fill_ellipse`, `blit_scale`, `tile_hash`, `fog_lerp`, `tile_detail` |
| ~2545 | **Baked sprites** | `art_stream_ok` (self-test only), `art_palette`, `draw_sprite`. Anchor = bottom-centre; fog applied to the palette once per draw |
| ~1745 | **Building art seam** | `art_bld_small`/`art_bld_large`, `building_sprite_id` — read by `world_heights`, `tile_colour` AND `draw_building`, deliberately one decision |
| ~3140 | **Prop art seam** | `prop_art[]` table; `draw_prop` dispatches to a baked sprite or falls back to the procedural routine |
| 1997 | **Bitmap font** | `FONT_*` constants, the flat `FONT_5X7` table (2007), `draw_glyph` (2068), `draw_text`, `draw_text_shadow`. All inside `#if WAYFARER_SELFTEST` |
| 2560 | **House parts** | `BV_*` variant accessors, wall/roof palettes; `draw_building` (2715) — roof face-split, facade derived from the wall-top diamond |
| 2380 | **Props** | palettes, `draw_tree`/`bush`/`rock`/`reed`/`flower`/`crystal`/`stump` (all with contact shadows), `prop_at`, `draw_prop` |
| 2921 | Render | `tile_reveal`, `tile_colour` (2921), `render` (2975) — the band sweep; `render_grid` (3195), `camera_follow` (3267, **now eased**) |
| 3329 | Window | `pick_scale`, `backbuffer_new`, `present` |
| ~3400 | **Tuning overlay** | `tune_adjust`, `tune_draw` (3431) — F3/TAB/`-`/`=`, fog constants only. Self-test-only |
| 3470 | Self-test | `move_selftest` onward. `fog_selftest` 4791, `land_check` 4952, `font_selftest` 5141 |
| 5462 | `main` | Fixed-timestep loop, input, debug keys, frame cap |

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
| `LAND_ROCK_T` | **0.74** | Outcrop threshold. Raised once already — 0.68 covered ~40% of frame in rock. `--land-test` now bounds this at 40% |
| `RIVER_COUNT` / `RIVER_SRC_MIN` | **2 / 10 (new)** | Rivers per world; a source must be ≥10 BFS hops from the sea or the "river" is a puddle on the beach |
| `BRIDGE_SPACING` | **9 (new)** | River tiles between bridge attempts. A bridge is taken only where there is open ground on both sides, with a fallback sweep so a spacing accident does not burn a whole world |
| `WALK_FRAMES` / `WALK_FPS` | **4 / 8.0 (new)** | Character walk cycle. `anim` resets to 0 on key release so a standing player shows frame 0 rather than freezing mid-stride |
| Prop density (`prop_at`) | **tree 12.5%, bush 9.4%, stump 6.3%, flower 18.8%** | Was 22/15.6/6.3/15.6 — **retuned because baked sprites are far bigger than the procedural props they replaced.** Cumulative thresholds on a 0..31 roll |
| `RIVER_FALL_STEPS` / `RIVER_FALL_EVERY` | **4 / 5 (new)** | Waterfall terracing: 4 drops between a source and the mouth, one per 5 BFS hops of `sea_dist`. **`EVERY` was measured, not guessed** — at 8 a river reached only 2–3 of its 4 steps. Read as *depth*, see decision 35 |
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
30. **A detached islet across open water is scenery, not a generator defect.** `--land-test`
    therefore asserts the size of the component the **player spawns in**, not that the map is one
    piece — the single-component assertion fails on 3 of 100 seeds, all of which still play to
    completion because ocean is never walkable and entities only go in reachable regions. See
    [[Phase 05 - Verification Debt]] for the full reasoning; do not "fix" the generator over this
    without re-reading it.
31. **`iso_tile` takes `ax` as the diamond's CENTRE**, not its left edge — it does
    `x0 = ax - ISO_HW` internally, and its top vertex is at `ay`. So a tile's visual centre is
    `(ax, ay + ISO_HH)`, which is exactly what `world_to_iso` returns for the tile's centre point.
    **Projection and rasteriser already agree**, which is why props, entities and the player need
    no correction anywhere. Anything that adds one is wrong — that was the roof bug.
32. **Rivers descend a BFS distance-to-SEA field, not the height field.** [[Phase 06 - Water And
    Bridges]] proposed steepest descent and flagged its own trap: a height field has local minima
    that are not the coast, so a descent can wedge in a landlocked dip and needs a policy. A BFS
    field has **no local minima by construction** — every tile with a finite distance has a
    neighbour exactly one closer — so a walk stepping to `dist-1` strictly decreases and must
    terminate at water. The trap is designed out rather than handled. Meander comes from choosing
    randomly among the equally-good candidates.
33. **A bridge clears `solid`; it is NOT a collision special case.** This is a deliberate deviation
    from that phase's task 3, which called for `tile_blocked` to return not-blocked on a bridge.
    That would make collision read a second signal — and then `bfs_open`, `flood_open`,
    `regions_build` and `walk_regions` would all have to learn about bridges too, or walk-reachable
    and graph-reachable would disagree and `--gating-test` would be right to fail. Clearing `solid`
    instead means collision, the region graph, the verifier and the autopilot all see a crossable
    tile through the code path they already used. `bridge[][]` exists only so the renderer can draw
    planks. **This phase therefore adds ZERO new inputs to collision** — stronger than the "exactly
    one" the plan allowed, and decision 12's invariant survives untouched.
34. **Team art bakes into a compiled-in header at BUILD time; PNGs are source, never shipped.**
    Asked directly on 2026-08-05 ("should I add PNG assets?"). The answer is yes to PNGs *as bake
    inputs* — `SDL_image` is compiled out and zero external files may ship, so nothing decodes a
    PNG at runtime. A 32×32 sprite at 4 bpp is 512 bytes baked against 747,776 free, so **bytes are
    not the constraint**; authoring effort and pixel density are. See §9 for the size-and-scale
    advice given, and [[Phase 07 - Asset Seam]] for the pipeline.

New decisions from the Phase 06 part 2 session (2026-08-05):

35. **A river's descent is expressed as DEPTH, not elevation, and a bridge deck gets its own height
    branch.** The land a river cuts through sits at height 0, so terracing the bed *upward* going
    inland clamps at ground level and the river stops being a channel — it renders as a blue path
    painted on the grass. The working form is shallow at the source stepping **down** to the sea,
    with the deepest step landing exactly on the sea floor's own deepest step. Separately, a bridge
    tile is still `SURF_RIVER` (which is what keeps the water reading as continuous under it), so it
    inherited the river's sunken height and drew its deck at the bottom of the channel as a walkable
    pit; it now takes ground level, checked *before* the river branch. **Neither fault was visible
    while the river was one flat depth**, and neither is testable — `height` is render-only by
    construction, so both were found by screenshot. `sea_dist[][]` is likewise render-only, so the
    completability proof needed no new argument, only a re-run.
36. **`--bridge-test` measures an empirical claim, not a fallible checker, and that is why its pass
    condition is different.** Every other negative control here builds a broken world and confirms a
    checker rejects it. There is no such construction for bridges: the flood fill is exact and
    cannot "pass when it shouldn't". What is being verified is the *claim* that bridges are
    load-bearing, so the test generates each seed twice — normally, and with decking suppressed —
    and fails only if it never once observes the reachable component shrink. Same "a check that has
    never rejected anything proves nothing" bar, applied to a measurement.

New decisions from the Phase 07 session (2026-08-05):

37. **Baked sprites use an 8-bit index against a PER-SPRITE palette with NO quantisation, not the
    ≤16-entry 4 bpp [[Art Bible]] §8 specified.** Measured on the real art: 7–49 colours per
    sprite, mean 18, with 36 of 93 over 16. 4 bpp was never viable. Quantising toward a shared
    table would have saved a few KB and cost visible fidelity on pixel art — the wrong trade when
    684,800 bytes are free. Sprites are also **trimmed to their opaque bounding box** before
    encoding, since 71% of the authored canvas is transparent; that is the biggest single saving
    and it makes the anchor honest.
38. **The anchor is the ground-contact point: bottom-centre of the trimmed box.** Deliberately the
    same `(cx, by)` convention `draw_tree`/`draw_prop` already used, so a caller never needs to
    know whether it is calling a procedural routine or a sprite. **This is a contract** — a
    teammate authors against it, and changing it means re-baking and possibly re-authoring.
39. **A building sprite is the WHOLE building, so the ground under it must be flattened — and that
    decision is consulted in three places from ONE function.** Walls were never drawn by
    `draw_building`; footprint tiles get a wall height from `world_heights` and the tile rasteriser
    extrudes them. A sprite carries its own walls, so leaving the extrusion stood a 96 px cottage
    on a 42 px plinth. `building_sprite_id()` is therefore read by `world_heights`, `tile_colour`
    **and** `draw_building`, so there is one decision rather than three that can drift. Footprint
    tiles stay `solid`: collision and reachability are untouched, and `height` remains render-only.

New decisions from the direction-change session (2026-08-05):

40. **A prop drawn over the player FADES; the trees are not thinned and the sprites are not
    rescaled.** The occlusion problem is real and confirmed by ablation, but it is a design problem
    and not a depth-sort bug — see §0. Of the three candidate fixes the user chose the expensive one
    deliberately: thinning `prop_at` further would strip the woodland that makes the island read as
    inhabited, and scaling the sprites down would break the 48 px base unit the whole set was
    authored against (§9). **Fading is the only option that fixes visibility without spending
    either.** It is render-only by construction — the fade reads the player's screen position and
    writes nothing back — so collision, reachability and the 50-seed proof cannot observe it.
41. **The bush sprite's magenta base disc is stripped at BAKE time, and a real contact shadow is
    drawn in code instead.** The disc is authored into the team's art and reads as a halo on grass.
    Repainting the PNG by hand would silently fork the source from what the teammate has; stripping
    a known key colour in `tools/bake.ps1` keeps the authored file canonical and the fix
    reproducible on re-delivery. `draw_bush` and every other procedural prop **already draw contact
    shadows**, so the replacement is the routine that exists, not a new one.
42. **Rock outcrops keep their geometry; what changes is stone's VALUE.** They read as pale cubes
    floating out of the fog because stone is still the lightest large surface in the world — the
    same value-hierarchy fight [[Art Bible]] describes and Session 03 already had once. Deleting
    them from the generator was considered and rejected: it would change open ground, which would
    make the 50-seed reachability and playthrough proofs a re-argument rather than a re-run, to fix
    what is actually a palette fault. **This is a render-only change, tuned live on the F3 overlay**
    — explicitly not by rebuild-and-screenshot, which thrashed for three passes last time (§7).

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

**From the 2026-08-04 sessions:**

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
- **Do not assume a rasteriser's anchor — read it.** Chasing the askew roof, `iso_tile`'s `ax` was
  assumed to be the diamond's left edge and a compensating `+ISO_HW` was added; it is the *centre*
  (decision 31), so that doubled the error in the other direction. One screenshot caught it, but
  reading the six lines of `iso_tile` first would have been faster than the round trip.
- **A visual constant tuned against a buggy reference bakes the bug in.** The building facade was
  positioned relative to `cy - wall + ISO_HH`, a value that only made sense alongside the roof's own
  `ISO_HH` error. Fixing the roof moved the windows onto it. **When a defect is found in a
  reference point, re-derive everything measured from it** rather than nudging the dependants back.
- **Setting a test's threshold to make it pass is the failure this project keeps warning about.**
  `--land-test`'s connectivity bound failed 3 of 100 seeds; the fix was to go and *look* at those
  seeds, discover the assertion was asking the wrong question, and re-aim it at the property that
  actually matters — not to loosen the number until it went green. If a bound has to move, the
  justification belongs in the phase file.

**New in the Phase 06 part 2 session (2026-08-05):**

- **A terrain feature carved *into* the ground cannot be terraced upward.** The river's first
  descent added height going inland, which clamped at ground level within two steps and turned the
  channel into a blue path painted on the grass — strictly worse than the flat version it replaced.
  Anything below the ground plane has to be expressed as **depth increasing toward its outlet**, not
  elevation increasing away from it. See decision 35.
- **Giving a flat thing a height gradient breaks everything that was silently sharing its height.**
  Bridge decks had always taken the river's height branch; that was invisible while the river was
  one flat shallow depth and became a walkable pit the moment it wasn't. **When a constant becomes a
  gradient, go and find everything that was relying on it being constant** — a grep for the field is
  faster than waiting for the screenshot.
- **A 3 px feature is at the edge of what a screenshot can settle.** Whether the waterfall steps
  existed at all was far quicker to answer by probing the height field directly (a throwaway that
  printed each river's distinct heights) than by zooming into captures. `RIVER_FALL_EVERY` was
  wrong — only 2–3 of 4 steps materialised — and that showed up instantly in the numbers and not at
  all by eye. **Probe the data for existence, use the screenshot for judgement.**

**New in the Phase 07 session (2026-08-05):**

- **Writing the decoder's round-trip test AFTER wiring it visually cost a detour, exactly as that
  phase file predicted.** Sprites were wired first; a screenshot showed bushes rendering paler than
  their source; the next stretch went on suspecting a palette off-by-one. `--sprite-test` then
  proved the decoder pixel-exact in one run, and the real causes were mundane. **When a phase file
  names a test to write first, write it first** — the cost of ignoring it is paid in
  screenshot round trips, which are the slowest debugging loop this project has.
- **Retuning art means retuning everything that was calibrated against the OLD art's size.**
  `prop_at`'s 22% tree rate read as scattered woodland with ~20 px procedural blobs and as a solid
  canopy with 64×96 sprites — hiding the terrain, the buildings and the player. Nothing warns
  about this: density is not a collision input, so no test has an opinion, and it is only visible
  on screen.
- **"The sprite isn't drawing" and "the sprite is behind something" look identical.** The character
  was invisible in several captures. Rather than keep zooming, one build with props skipped settled
  it instantly: she rendered perfectly, so the depth sort was right and the trees were simply in
  front. **A one-build ablation beats a third screenshot** when the question is "is it absent or
  occluded".
- **A running or just-exited `wayfarer.exe` still blocks the relink**, and it fired again this
  session with no game open — the previous `--frames` run had not fully released the file. It is
  transient: `Get-Process -Name wayfarer` showed nothing and an immediate retry linked fine. Do not
  go looking for a code fault.

---

## 8. Verified vs NOT verified

### Verified — measured, not assumed

Everything from the previous handover's list still holds (builds clean; PRNG properties; collision
determinism; region graph invariants; reachability with a negative control; gating parity across all
4 tiers; **50/50 playthroughs — re-verified after the landform rewrite, still 50/50**; exact
isometric rasterisation with a negative control; building placement invariants with two negative
controls; audio callback timing; render cost). New this session:

- **The game has been played by several people, WITH the team's art in the build.** Four
  consecutive handovers carried "nobody has played it by hand" as a standing risk; the 2026-08-05
  art landing immediately replaced it with "nobody has played it *with the art in*". **Both are now
  retired.** On 2026-08-04 the user played the pre-art build and reported *"even though the gameplay
  is in early stage, it did feel slightly enjoyable"*; on 2026-08-05 the user **and several friends**
  played the current build with the baked character, buildings and nature props. **The overall
  verdict is explicitly still pending** — this retires the risk "nobody has moved around in it", not
  the question "is it good". [[QA Checklist]]'s "runs clean on a machine without dev tools" is a
  *different* item and is still unchecked.
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
- **`fog_lerp` preserves the value hierarchy.** 45 real palette colours × 5 reveal levels: zero
  inversions, zero collapsed palette ramps at reveal 0. The Phase 02 claim that `FOG_KEEP` keeps a
  four-shade canopy separable is now measured rather than argued, and the negative control proves
  the checker can fail (14 collapsed ramps, 206 collapsed pairs against a crushing blend).
- **The island generator's coverage and connectivity hold over 100 seeds**, with both negative
  controls firing. Includes an assumption `place_buildings` had always made silently: that some
  buildable ground exists at all.
- **Rivers and bridges did not weaken the completability proof.** Rivers make tiles solid and
  bridges make them open, both *before* the verifier, so the guarantee was re-**run** not re-argued:
  reach 50/50 with its control, gating 30/30, play **50/50**, land 30/30.
- **River pathfinding terminates.** Not by testing but by construction — see decision 32. There is
  no seed on which the descent can fail to reach water.
- **BRIDGES ARE LOAD-BEARING — now measured.** `--bridge-test` regenerates each seed twice, once
  with bridge decking suppressed, and compares the size of the open component the player spawns in.
  **200 of 200 bridge-bearing seeds shrank when suppressed**, so a bridge is the thing reconnecting
  a cut island on every seed measured, not decoration. This retires the standing "highest-value
  single test left in the project" item that four handovers carried.
- **The waterfall staircase exists in the height field.** Probed directly across seeds 3/5/7/12:
  3–4 distinct river-bed levels per river, spanning the intended range down to the sea floor's own
  deepest step. Whether it *reads* as falling water is a separate question — see below.
- **Waterfall terracing did not weaken the completability proof.** `sea_dist` is render-only and
  `world_heights` has never been a collision input, but the guarantee was re-**run** not re-argued:
  bridge 200/200, land 30/30 + both controls, reach 50/50 + control, gating 30/30, play **50/50**.

- **The sprite decoder is pixel-exact, proven independently of the bake tool.** `--sprite-test`
  builds a pattern in C (a >128 run, alternating singles, an exactly-128 run, a lone tail pixel),
  encodes it, decodes it through the same walk `draw_sprite` uses, and compares. Deliberately not
  checked against the baker's own output, which would repeat `--font-test`'s known blind spot.
  Both negative controls fire.
- **All 37 baked streams decode to exactly `w*h`** with every index inside their own palette.
- **The art bake costs 62,976 shipping bytes and did not weaken anything.** Re-**run**, not
  re-argued, after the seam landed: sprite, iso, font, fog, rng, move, land (30 + both controls),
  village (30 + both controls), region (30), reach (50 + control), gating (30), bridge (200/200),
  **play 50/50**.

### NOT verified — be honest about these

- **The player is routinely hidden behind trees. DECIDED, NOT YET BUILT** — props will fade over
  the player, decision 40. Until that ships the defect is still in the build exactly as measured.
- **The bush sprite carries a magenta base disc** authored into the art, which reads as a halo on
  grass. **DECIDED, NOT YET BUILT** — stripped at bake time and replaced with the contact shadow
  the procedural props already draw, decision 41.
- **No test proves a sprite lands on the right tile in WORLD terms.** `--sprite-test` checks the
  anchor is bottom-centre of its own box; that `draw_building` passes the right screen point is
  screenshot-verified only.
- **The overall verdict on the art is still pending**, but "nobody has played with the art in" is
  **retired as of 2026-08-05** — see Verified. The user and several friends have now played the
  build with the baked sprites in it. What is still unjudged is whether the *look* is right, not
  whether anyone has seen it move.
- **The waterfall drops are 3 px and nobody has judged them.** The steps are confirmed present in
  the data and the channel now reads as a channel with banks, but whether a 3 px drop reads as
  *falling water* to a player is unjudged. [[Phase 10 - Motion]]'s shimmer is what would sell it;
  until then this is geometry that is correct rather than an effect that is convincing.
- **`--bridge-test` measures reachable-area shrinkage, not solvability.** The stronger claim — "the
  verifier would reject this seed outright without its bridge" — is not what is checked; entity
  placement re-runs against the smaller component and can still succeed. Shrinkage is the honest
  measurement and is what is reported.
- **Nobody has played at the new scale, or driven the new camera.** The screenshots say the world is
  denser and better-proportioned; whether 24 px tiles are *nice to walk around* is a different
  question. `CAM_DEADZONE`/`CAM_EASE` are first guesses and "does the easing feel right" cannot be
  claimed from here.
- **Rivers have been seen on four seeds** (3, 5, 7, 12), all of which read correctly. There is still
  no sweep of how often a river is scenic vs. a straight line across the map, no check that two
  rivers never merge into a lake, and no measurement of how often a river forces a long detour.
- **The user's verdict on the houses, 2026-08-05:** *"fine, not perfect but workable"*. The land is
  *"decent but lacks the pixelated game feel"* — see §9, that is a resolution/palette question and
  is still open.
- **Rock outcrops read as scattered pale blocks under fog at the new scale. DECIDED, NOT YET BUILT**
  — stone's value gets fixed so they recede into the haze; the outcrops themselves stay, so the
  landform and its proofs are untouched. Decision 42, and exactly what the F3 overlay exists to
  settle.
- **The overlay's liveness is proven by construction, not by a scripted keypress.** `fog_lerp` reads
  the struct the keys write, but no automated run presses a key and diffs two frames.
- **The camera ease runs per frame, not per simulation tick.** Stable while the frame cap holds;
  it would drift on a machine that cannot hold it. Known simplification, not an oversight.
- **Whether it is fun beyond one early, positive, informal reaction.** One playtest is not QA.
- **Whether the fog and palette values are actually *right***, as opposed to "no longer obviously
  wrong." `--fog-test` now proves the value *hierarchy* survives the blend; it has no opinion on
  whether the haze is the right colour. That is an F3 judgement and still unmade.
- **Whether the island generator produces *attractive* coastlines.** `--land-test` bounds coverage
  and connectivity across 100 seeds; it says nothing about shape. Still only judged on the handful
  of seeds that got screenshotted.
- **`--fog-test` reports 3 "collapses"** — colour pairs that tie under rounding at some reveal.
  Reported rather than failed, because a tie loses information without lying about the ordering.
  Nobody has looked at whether 3 is visible.
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
| Asset pipeline for team-authored art | **RESOLVED AND BUILT, 2026-08-05.** `tools/bake.ps1` → `src/art_data.h`, committed, compiled in, nothing loaded at runtime. 37 of the team's 130 sprites are wired; `magical/` (56 frames) is baked-out until something calls it. See decisions 37–39 and [[Phase 07 - Asset Seam]] |
| **Player occlusion behind props** | **RESOLVED 2026-08-05 — props fade over the player.** Carried as the top open item by four handovers. The two cheap fixes (thin the trees, rescale the sprites) were rejected in favour of the one that costs neither the woodland nor the 48 px base unit. See decision 40. **Decided, not yet built** |
| **The bush's magenta base disc** | **RESOLVED 2026-08-05** — stripped at bake time, replaced by the contact shadow the procedural props already draw. See decision 41. **Decided, not yet built** |
| **Rock outcrops as pale floating cubes** | **RESOLVED 2026-08-05** — stone's value gets fixed, the outcrops stay, so no generator change and no re-argued proof. See decision 42. **Decided, not yet built** |
| **A second biome** | **RESOLVED 2026-08-05 — new direction, approved and specced.** A portal in the `TERRAIN_DARK` region to a Lumiara-style dream realm, timeboxed into five separately shippable slices. See [[Phase 12 - Dream Realm]] |
| **"Lacks the pixelated game feel"** | **LARGELY ANSWERED BY THE ART, 2026-08-05.** The worry was that 960×540 ×2 reads too smooth. In practice the delivered pixel art supplies the chunkiness the procedural shapes lacked, and it was authored against a 48 px diamond — the scale already shipped — so **no re-authoring was needed and no resolution change was required.** Dropping `LOGICAL_W`/`LOGICAL_H` remains available as a taste lever, but it is no longer blocking anything |
| **Input orientation** | **RESOLVED, 2026-08-05** — screen-aligned, `dd8cfef`. See decision 28 |
| Camera easing | **RESOLVED in mechanism, OPEN in feel** — deadzone + exponential ease shipped, but `CAM_DEADZONE`/`CAM_EASE` are first guesses nobody has driven by hand |
| **Art scale** | **RESOLVED, 2026-08-05** — `TILE` 24 with everything authored through `PX()`. Whether 24 is the *right* number is still a judgement call; it is now a one-line change to try another |
| Landmass size / region count | **Provisional 16 regions over a now 1.8× larger tile grid.** Pacing still unmeasured, and the regions are now bigger in tiles than anything was measured against |
| Fragment + Found Soul counts | **Provisional 14 + 5.** Unchanged, still awaiting sign-off |
| Kindle: passive radius vs active ping | **Open.** Unchanged |
| Inventory/tool icons from mockup | **Open, and now explicitly addressed in [[Art Bible]] §7**: out of scope per [[Save and UI]]'s "no HUD clutter," reinstating any of it is its own flagged decision |

[[Cut List]] is pre-committed if time runs short. **Never cut:** the fog-reveal core feel, the
reachability guarantee, staying under the byte limit, a defined completable end state.

### What the team can and cannot hand over — answer given 2026-08-05

The user asked directly whether to add PNG assets. The answer, recorded here so it does not have to
be re-derived:

- **Yes, send PNGs — as *bake inputs*.** They are source files on a build machine. They never ship.
  `SDL_image` is compiled out and the zero-external-files rule is absolute, so nothing can decode a
  PNG at runtime.
- **Indexed pixel art, small.** A 32×32 sprite at 4 bpp bakes to 512 bytes. Against 747,776 bytes
  free that is room for well over a thousand. **Bytes are not the constraint** — authoring effort
  and pixel density are.
- **Authored against the tile.** Diamonds are 48×24 at `TILE 24`; anything hand-drawn should be
  sized to that, and its dimensions wrapped in `PX()` so it survives another scale change.
- **A shared fixed palette** across assets is what keeps the bake small and the look coherent.
- The bake script is a build-time PNG→`src/assets.h` converter (.NET's `System.Drawing` reads PNG
  fine from PowerShell, and this project already uses it for screenshots). **It does not exist
  yet** — that is Phase 07.
- **The untracked `tree.glb` at the vault root is exactly the mistake this prevents.** It can be a
  bake input if someone renders it to sprite frames. It can never ship.

### The size finding, restated with current numbers

**The picture changed on 2026-08-05, and in the expected direction.** All game *logic* ever written
— the island generator, village clustering, elevation, seven kinds of procedural prop,
mix-and-match buildings, the fog rewrite, a bitmap font, a tuning overlay, screen-aligned input, an
eased camera, rivers, bridges, waterfalls, and six test harnesses with negative controls — still
comes to about **23 KB**, a rounding error next to SDL2's ~664 KB.

**Art is the first thing to cost real bytes: 62,976 for 37 sprites**, roughly 2.7× everything else
ever written. It is still only 8% of the free space, and the remaining 93 unbaked sprites would fit
several times over. So the conclusion is unchanged in substance — **bytes are not the constraint** —
but the *shape* is now worth knowing: if anything ever threatens the limit it will be assets, not
code, and the lever is which sprites get baked, not how the game is written.

Authoring judgement remains the real cost. This session's expensive mistakes were writing a test
after the code it was meant to de-risk, and forgetting that constants calibrated against small
procedural props do not survive being handed 96 px sprites. Neither cost a byte.

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

**Current position, in one paragraph:** **Phases 00–06 are all done.** 00–02
(memory + skill policy + Art Bible; island landform + village clustering; roof shading + the fog
rewrite) as `5ffdb38` and `e5c8942`; **Phase 03** (bitmap font + F3 fog-tuning overlay, +0 bytes) as
`60b4e3a`, its human-usability gate discharged the same day; **Phase 04** (screen-aligned input +
eased camera) as `dd8cfef`; **Phase 05** (`--land-test`, `--fog-test`, both with negative controls,
+0 bytes) as `4847bf5`; **Phase 06** (rivers + bridges as `e42f2f4`, then waterfalls and
`--bridge-test`, +0 bytes). Two unplanned pieces landed alongside them, both from the user looking
at the screen: the world was **rescaled** (`e03138d`, `TILE` 32→24 with every authored dimension
routed through `PX()`, grid grown to 108×60), and the **houses were fixed** (`b4bf446` — the roof
had been drawn half a tile off its own walls since buildings landed, which also put the windows on
the roof).

**Phase 07** (the bake pipeline **and** 37 of the team's real sprites, +62,976 bytes) landed the
same day the art arrived, which absorbed most of what Phase 09 was holding.

**Start here: settle the player-occlusion question** (§0 — a 96 px tree hides a 48 px character,
and the depth sort is correct), then **[[Phase 08 - Save Load]]**. After that: motion (Phase 10) and
the rest of the art work — the restoration rebuild and worn paths are what remain of Phase 09 —
all timeboxed, with a **hard stop on 2026-08-14** before the ship-critical remainder (Phase 11:
audio, font-dependent HUD, QA, submission) takes over regardless of how much art work is finished.

**Nine days to that hard stop.** Audio is a softsynth from zero and is completely untouched; so are
save/load and any on-screen HUD. If something has to give, take it from [[Cut List]].

**Schedule reality, unchanged in substance from the last handover:** today is 2026-08-05; the
deadline is 2026-09-04. Audio (a softsynth from zero) and the rest of Week 5 (save/load, HUD, win
state, game-feel pass) are both completely untouched. **Judging order is finished → under size →
fun.** If something has to give, take it from [[Cut List]] — the most likely candidate remains
cutting Kindle and shipping Wade + Climb only.
