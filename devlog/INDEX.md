---
tags: [devlog, wayfarer]
---

# Devlog Index

See [[Wayfarer MOC]] for the project hub. Updated every session per [[Agent Prompt]]'s
session-logging rules. Picking this up cold? Start with [[Handover]].

**Current `.exe` size:** 1,085,440 bytes (on `feat/phase-14-castle-full-set-dev-mode` — TASK-01 Groups A–D)
**Suite status:** default gate **23/23 green in one run**, `--play-test --seeds 50` **50/50**, `--audio-test` PASS — all re-run from a fresh build in Session 17. `--land-test --seeds 500` **re-swept: fails exactly `85/417/430`**, identical to the pre-existing set at `428c9fd`; the character switch introduced no regression. `--fade-test` now carries the `facing6` truth table with two non-sentinel controls plus a real `g->clock`-not-`p->anim` assertion, each proven against a break.
**Current status:** **TASK-01 character switch Groups A–D done (136 sprites, six-way bob on `g->clock`) and reviewed; Group E closed except the cadence.** `idle_down` confirmed showing the face, character legible at `TILE 18`; **the 8 fps bob cadence remains unjudged and needs a human to watch it run.** Phases 00–12 all done; Phase 13 Aetherhold single-castle switched; invisible-wall + foliage + bridge/water bans + portal/enterprise thinning + cluster fog done pending visual screenshot. Phase 11
(ship-critical) is built and verified:
5-layer procedural softsynth (Base/Strings/Pad/Bells/Voice of Souls, deterministic, measured
0.325 ms worst case vs a 21.333 ms deadline), chime/shard/portal SFX, and the HUD on the Phase 03
font — counters, minimap, restore toasts, win banner, seed line — which required extending the
font with lowercase a–z and `/` (the old table was uppercase-only; every HUD string silently
skipped). Phase 12 (dream realm) remains feature-complete on its own branch.
**Forward plan:** see [[Phase Roadmap]] — Aetherhold's keep/dungeon slices remain deferred; submission
checklist is second-machine smoke test, repo visibility, final wrap.
**Headroom:** 354,560 bytes under the 1,440,000 ship target (`389,120` under hard `1,474,560`) — delta `+5,120` over 1,080,320

> **Note on the entry below:** written by a second, separately-run agent (`qwen`, via a tool called
> `opencode`) that was pointed at this same working directory while Phase 12 slice 5 was mid-flight.
> Its Agent Log row in [[Handover]] said it was WAITING on the `ART_FX_WELL_*` symbols slice 5
> hadn't baked yet — that blocker is resolved; slice 5 is done and committed. Its own devlog file is
> left as-is as a historical record; this index entry is corrected to match, since INDEX.md is a
> shared summary every session is supposed to be able to trust cold. Its `opencode.json` (API keys
> in plaintext) was found untracked at the vault root and is now gitignored — never commit it.

## Sessions

- [[2026-08-12-session-01]] *(Session 15+16 — Session 15 body + Session 16 appended section)* — **Session 15: Invisible-wall squares, bridge/water foliage bans, 30% thinning, single `castle_full_multitier_v2`, cluster fog + `--path-test` green** (release `1,080,320`, `110` sprites, `25/25` default). **Session 16: TASK-01 character switch Groups A–D** — six-way idle `assets/player_new/` `384×64` pitch 48 `0–6` (frame 7 dup skipped) via `CharSheets` + `ConvertTo-SpriteFromRegion` (same magenta strip/trim/anchor/RLE); `FACE6_*`, `facing6_from_intent(sx,sy)` pure screen intent `±X→down-diag`, `Player.facing6` render-only, `player_idle[6][7]` + `player_sprite_id(Player*,clock)` on `g->clock` `IDLE_FPS 8.0`; `--sprite-test 136` + `--fade-test facing6 9 + control 6/9` green; release `1,085,440` (`+5,120`), bake `370390` const (`110→152→136`), headroom `354,560`. **Group E visual gate owed:** six facings standing+moving at `TILE 18`, `idle_down` face check, bob cadence, size read, full `25/25 --seeds 20` + `--land-test 500 =85/417/430`.

- [[2026-08-11-session-01]] *(Session 14)* — **[[Bug Fix Plan]] implemented in full, all 8 issues.**
  A real heap overflow in `iso_diamond`/`iso_diamond_lr` (inclusive loop against an exclusive clamp,
  writing past the backbuffer on the last row); three self-tests that could not fail on the fault
  they claimed to check (`gating_selftest`, `audio_selftest` outside `--layers`, and the missing
  `abilities` mask in `game_load`); the `audio.rng` main-thread data race, now a `reset_req`-style
  atomic handoff; the Aetherhold watchtower relocated to `(74,33)` behind shared constants so its
  generation and render copies cannot drift; and ability grants finally *enforced* into the
  overworld — they were landing in the dream sector on **529 of 500 seeds**. Every fix proven
  against a check that fails without it. Release 1,029,120 bytes (+512), zero warnings.
  Not verified: ThreadSanitizer unavailable in this toolchain. Open decision: the new watchtower
  position sits across the mainland forest road.

- [[2026-08-06-session-07]] *(Session 13)* — **Aetherhold layout corrected and supplied dark-fantasy
  assets integrated.** The previous rectangle/NE/Dream-gap implementation was replaced with a fixed
  southeast overworld island (rows 35–76), mainland watchtower key `(88,59)`, overworld causeway
  `x94..107,y56`, and an inner fortress/courtyard. `tools/bake.ps1` now emits the supplied
  `assets/dark_fantasy` walls/buildings/props as `AETHER_*`: 93 records, 82 streams, 11 dream
  variants. Full suite + `--aether-test` green; release 899,584 bytes, 540,416 headroom; render
  mean 1.832 ms, 59.7 fps.

- [[2026-08-06-session-06]] *(Session 12)* — **Phase 11: ship critical, done.** 5-layer softsynth
  on static pattern tables, pure functions of a sample counter (deterministic: two fresh states,
  96,000 bit-identical samples). Fragment restores switch on Strings→Pad→Bells at frag counts
  1/2/3, souls switch on the Voice; chime/shard/portal SFX; R/F9 zero the synth through a
  callback-latched `reset_req`. `--audio-test --layers --sfx`: worst 0.325 ms of 21.333 ms, peak
  0.9151, no NaN/clip/partial writes. HUD on the un-gated font: the font was **uppercase-only**,
  so the first HUD pass rendered nothing but digits — root-caused by pixel probes and fixed by
  adding lowercase a–z + `/` (0x20–0x7A, 91 glyphs). Counters, minimap (cached, 2 px/tile),
  toasts (fade over last 30 frames), one-shot win banner, seed line; `--hud-test` probes all of it
  green, `--font-test` now covers 91 glyphs with its stride negative control. Full suite green
  end-to-end; release 786,432 bytes (+4,608), 653,568 headroom; `nm`: 1 symbol, no
  SDL_image/ttf/mixer. Perf with HUD live: render mean 1.017 ms.
- [[2026-08-06-session-05]] *(Session 11)* — **Phase 10: motion, done.** `g->clock` was already the
  render-only animation clock Phase 10's task 1 asked for — no new state. Tree sway at the prop
  dispatch (one formula, `tree_sway`, covers baked and procedural) plus a per-lobe ripple in
  `draw_tree`; water shimmer ±5 px from `water_ripple`; waterfall fall-lines as a scrolling dash
  on Phase 06's terraced faces (`waterfall_dash`, same column math as `iso_tile`); chimney smoke
  (`draw_smoke`) at 1 puff on the rebuild's 0.7–1.0 window and 2 puffs over the baked sprite at
  full restoration; fireflies gated by `mote_gate` (grass, sparse, restoration ≥ 0.7, fading in);
  Found Souls bob via `soul_bob`, fragments deliberately static. `--motion-test`: render
  determinism at one clock, 23,552 px differ at another, per-helper bounds and the 8-row gate
  table — all PASS. Full suite re-run green end-to-end; two static frames with the camera parked
  differ in hash (motion is live without simulation drift). Release 781,824 bytes, +2,048,
  658,176 headroom. Perf after all effects: render mean 1.123 ms of a 16.67 ms budget.
- [[2026-08-06-session-04]] *(Session 10)* — **Phase 08: save/load, done.** `SDL_RWFromFile`
  confirmed to link despite `SDL_FILESYSTEM=OFF` (measured with a probe before writing any save
  code). 28-byte flat versioned save — seed + position + abilities + restored/shard masks; load
  regenerates from the seed through `game_init` and replays deltas via `apply_restore` (split out
  of `try_restore` so play and load share one definition of "restored"). Validation-first load:
  the live game is only touched after every check passes, position included against the
  regenerated solid map. F5/F9, title-bar feedback. `--save-test`: round-trip bit-identical,
  deterministic, five negative controls (truncated, wrong version, bad magic, out-of-bounds
  position, missing file) each leaving the game untouched — 10/10 seeds. Full suite green.
  +1,536 bytes. Also pushed `feat/phase-09-restoration` (`57b7de9`), which the handover notes had
  called nonexistent.
- [[2026-08-06-session-03]] *(Session 09)* — **Phase 12 slices 4 and 5: the phase is done.** The
  prompt indicator (procedural, sized against the bitmap font), a real Kindle-gate fix
  (`try_portal` now requires the far end be standable — `--gating-test` had modelled a stricter
  interact than the real one and couldn't see the hole), fragments/Souls split into the dream
  sector, dream shards, and the Dream Well. `--play-test` 50/50 with the whole loop exercised —
  portal crossings, shard collection, the Well unlocked and redeemed — on every seed. Two placement
  bugs found by looking (the Well inside the portal arch's silhouette; the player's own sprite
  occluding it in a screenshot). Also: found and flagged a second agent (`qwen`/`opencode`) running
  against this same working tree with a plaintext-API-key config file; cleaned up the shared docs
  it had reverted.
- [[2026-08-06-session-02]] *(Session 08)* — **Phase 09 picked up by a second agent (`qwen`, via
  `opencode`) run against this same working tree.** Restoration rebuild, worn paths, and ground
  marks scoped; the session stopped at a compile gate because Phase 12 slice 5 was mid-flight in
  the same files. Resolved as of the next session: slice 5 finished and committed. Phase 09's scope
  is still open for whoever picks it up next.
- [[2026-08-06-session-01]] *(Session 07)* — **Phase 12 slice 3: the dream realm gets a look.**
  Palette recolour (`dream_shift`, one formula shared with `tools/bake.ps1` and checked by
  `--sprite-test`), the portal's FX and arch, a violet starfield void. Found and fixed a real bug
  the same day it shipped: the portal landed the player in a Kindle-gated region on 11 of 100
  seeds with no way to move, invisible to every existing test because a component you cannot stand
  in is still one you can reach.
- [[2026-08-05-session-01]] *(Session 06)* — **The team's art arrives, and the seam it goes through.**
  [[Phase 07 - Asset Seam]] done: `tools/bake.ps1` turns authored PNGs into a committed, compiled-in
  `src/art_data.h`, and **37 real sprites are wired** — a 4-direction 4-frame walking character, 10
  buildings, 11 nature props — retiring four "does not exist" items at once. **+62,976 bytes**, the
  first time art has cost more than all game logic ever written (~23 KB), though still 8% of free
  space. The Art Bible's ≤16-entry palette spec **did not survive measurement** (7–49 colours, mean
  18), so the format is 8-bit per-sprite palettes with no quantisation, trimmed to the opaque box
  because 71% of canvas is transparent. **Walked into this phase's own named trap**: wired the
  decoder before writing its round-trip, then spent a screenshot loop suspecting a palette bug that
  `--sprite-test` disproved in one run. Also: prop density had to be retuned (22%→12.5%) because
  sprites are far bigger than the blobs they replaced, and a building sprite is the *whole*
  building, so `world_heights` must flatten its footprint. **Open: the player is routinely hidden
  behind trees** — confirmed by ablation, correct depth sort, needs a design decision.

- [[2026-08-05-session-01]] *(Session 05)* — **The waterfalls, and bridges stop being an argument.**
  [[Phase 06 - Water And Bridges]] closed at **+0 bytes**. `--bridge-test` regenerates each seed
  twice, once with decking suppressed, and compares the player's spawn component: **200/200
  bridge-bearing seeds shrank**, so "bridges are load-bearing" is measured rather than argued —
  retiring the item four handovers carried as the highest-value test left. Waterfalls came from
  carrying `place_rivers`' BFS field out to a render-only `sea_dist` and quantising it, so
  `iso_tile` draws each drop with no new routine. Two faults found by **screenshot, not test**
  (`height` is render-only, so no checker can see either): the descent had to be **depth, not
  elevation** — terracing upward clamped at ground level and turned the channel into a blue path on
  the grass — and a bridge deck, still `SURF_RIVER`, drew at the bottom of the channel as a walkable
  pit until it got its own height branch. Also: **probe the data for existence, screenshot for
  judgement**; a wrong `RIVER_FALL_EVERY` was invisible by eye and instant in the numbers.
  Re-run not re-argued: play 50/50, reach 50/50, land 30/30, gating 30/30.

- [[2026-08-05-session-01]] *(Session 04)* — **Rivers reach the sea, and the chat handed over.**
  [[Phase 06 - Water And Bridges]] part 1: rivers descend a BFS **distance-to-sea** field rather
  than the height field, which designs out the local-minima trap the phase file flagged — a BFS
  field has none, so the walk provably terminates at water. Bridges **clear `solid`** rather than
  special-casing `tile_blocked`, so collision, the region graph, the verifier and the autopilot all
  see a crossable tile through the path they already used; the phase adds **zero** new collision
  inputs. Completability re-run not re-argued: play 50/50, reach 50/50. Also answered the team's
  asset question (PNGs are bake *inputs*, never shipped) and flagged that "lacks the pixelated
  feel" is a resolution/palette question, not an asset one. **Left half-done on purpose**:
  waterfalls and the bridge-suppression negative control are both still owed.

- [[2026-08-05-session-01]] *(Session 03)* — **The houses were a mess, and the rule debt is paid.**
  Three defects in `draw_building`, all present since buildings landed: the roof was drawn half a
  tile below its own walls (`iso_tile` takes `ax` as the diamond's **centre**, so `world_to_iso`
  already agrees with it and the roof's extra `ISO_HH` was pure error); the facade was positioned
  relative to that same broken reference, so fixing the roof put the windows on it until they were
  re-derived from the wall-top diamond; and walls were half the height their footprints needed.
  Then [[Phase 05 - Verification Debt]]: `--land-test` and `--fog-test`, each with a working
  negative control, at **+0 shipping bytes**. `--land-test`'s obvious assertion failed 3 of 100
  seeds — and looking at those seeds showed the *assertion* was wrong, not the generator: a
  detached islet across unwalkable ocean is scenery, so the check was re-aimed at the component the
  player spawns in. Suite green, play 50/50.

- [[2026-08-05-session-01]] *(Session 02)* — **The world was too big, and `W` finally points up.**
  Diagnosed "everything seems too big" to its actual cause: every prop was authored in absolute
  pixels against `TILE == 32`, so `TILE` scaled the ground but not the art. New `PX()` makes `TILE`
  one honest knob for the whole visual scale; `TILE` 32→24 and the world 80×45→108×60 so the island
  keeps its extent and gains resolution (safe because `land_noise` is normalised). **−512 bytes, and
  render unchanged despite 37% more draw calls.** Then [[Phase 04 - Traversal]]: input rotated
  through the inverse projection basis, `move_axis` untouched. **Rotating the autopilot's deadband
  naively livelocked every playthrough** — 3 of 3 seeds capped at 200,000 steps, presenting as a
  hang rather than a failure; fixed by thresholding in world space and rotating only the discrete
  intent. `speed_selftest` rewritten to assert a basis-*independent* invariant (travel distance, all
  8 directions) and given the negative control it never had. Camera got a deadzone + exponential
  ease. Suite green throughout, play 50/50.

- [[2026-08-05-session-01]] — **[[Phase 03 - Legibility Tools]]: a bitmap font, and the live tuning
  overlay built on it.** 5×7 bit-packed glyphs (`0x20`–`0x5F`), `draw_text` / `draw_text_shadow`, and
  an F3 overlay (`TAB` row, `-`/`=` adjust) that tunes `FOG_TINT_*` and `FOG_KEEP` live — replacing
  the three-pass edit-rebuild-screenshot loop that settled those same constants last session. Overlay
  scope was **narrowed to render-only constants by an explicit decision**; the generation constants
  would need a full `game_init` per keypress and are excluded on the record. New `--font-test` with an
  off-by-one-stride negative control, its reference count derived from the glyph table so it tracks
  edits automatically. **Shipping delta +0 bytes**, re-confirmed after `fog_lerp` was rewritten. Whole
  suite re-run, all PASS. Two glyphs (`=`, `>`) were missing while every test stayed green — found by
  screenshot, which is the same lesson as the lollipop trees. Phase is code-complete, **not closed**:
  its gate requires a human to actually use the overlay once.

- [[2026-08-04-session-01]] *(Session 04)* — **Handover rewrite, and a phase-by-phase roadmap.**
  Documentation only, ahead of a fresh chat. [[Handover]] rewritten against a fresh build and a
  fresh full test run (nothing carried forward unverified). New `design/phases/` folder: an index
  ([[Phase Roadmap]]) plus 12 phase files, Phases 00–02 marked done with commit/byte evidence,
  Phases 03–11 planned with a definition of done, concrete file/line-referenced tasks, a
  verification gate, and phase-specific traps each. No code changed.

- [[2026-08-04-session-01]] *(Session 03)* — **Roof volume, and the fog rewrite.** `iso_diamond_lr`
  gives roofs the same left/right face split terrain gets from `FACE_L`/`FACE_R`, fixing the flat-plate
  read diagnosed last session. `fog_lerp` rewritten: unrevealed land now resolves toward a light cool
  haze keeping `FOG_KEEP` of its own luminance contrast, instead of a dark grey that crushed 95% of
  the screen to one dead colour — the actual cause of "traversal feels suffocating". Took three
  passes; the first two overcorrected, and stone had to be pulled back down after it became the
  brightest surface in the world. **First human playtest ever.** Render 0.844 ms, suite PASS,
  play 50/50. Still owed: `--fog-test`, `--land-test`, screen-aligned input, camera easing.

- [[2026-08-04-session-01]] *(Session 02)* — **The landform, and a first honest look at it.**
  Replaced the cave generator with an island height field: coastline, ocean, inland rock outcrops,
  villages clustered onto sites instead of scattered over every open plot. Found three defects by
  screenshotting rather than testing — flat-slab outcrops, a checkerboard height jitter, and canopy
  lobes that were axis-aligned rectangles — and fixed all three, adding `fill_ellipse` and prop
  contact shadows. New provisional [[Art Bible]]. **No collision input changed**, so the 50-seed
  completability proof was re-run rather than re-argued; whole suite PASS. Left explicitly undone:
  `--land-test` and its negative control, the fog rewrite, screen-aligned input, and building roof
  face-shading (diagnosed, with a note in `draw_building` recording a failed attempt).
- [[2026-08-04-session-01]] — **Isometric pivot, part 1.** Render instrumentation first (the old
  "56 fps" measured `SDL_Delay`, not drawing). Then 32 px tiles, a 2:1 isometric projection with a
  provably gap-free column rasteriser, elevation with cliff faces, a 960×540 logical buffer
  integer-scaled into the window, per-tile surface detail, and layered procedural trees with a
  real two-sub-pass depth sort. New `--iso-test` with a negative control. No simulation code
  touched; all existing tests pass unchanged. Corrupted the source encoding with a PowerShell text
  round-trip and had to repair it.
- [[2026-08-02-session-03]] — **Week 3.** Ability gating in collision, the restoration loop,
  Found Soul states, win condition, restore confirm beat. 50/50 seeds played to completion by
  autopilot through real collision. Caught a counter-reset bug that would have carried progress
  across in-game regeneration.
- [[2026-08-02-session-02]] — **Week 2.** Region graph (farthest-point sampling + multi-source
  BFS), terrain/ability gating, the reachability invariant with a negative control, and the
  debug overlay + 12-seed grid view. Caught a `sizeof`-on-pointer bug that every *relative*
  structural test had passed.
- [[2026-08-02-session-01]] — **Week 1.** PCG32 with independent terrain/entity/audio streams;
  continuous movement + swept AABB tile collision on a fixed 60 Hz step; first-pass fog-to-color
  reveal. Fixed sealed spawn pockets, corner spawns, an uncapped render loop, and recovered
  39,424 bytes of zeros wrongly stored in `.data`.
- [[2026-08-01-session-01]]
  - **Session 01** — pipeline proof: SDL2 window + software framebuffer, static stripped build.
    Stock `libSDL2.a` blew the hard limit at 1,714,176 bytes; a cut-down SDL2 build brought it
    to 669,696. See [[Toolchain Setup]].
  - **Session 02** — audio pipeline: procedural synthesis in the callback, seeded RNG with
    verified replay. Callback uses 0.4% of its deadline. Found and fixed a launch-blocking bug
    on machines with no audio device.

## Size history

| Date | Bytes | Delta | Note |
|---|---:|---:|---|
| 08-01 | 1,714,176 | — | first build, stock prebuilt `libSDL2.a` — over the hard limit |
| 08-01 | 669,696 | −1,044,480 | after rebuilding SDL2 with unused subsystems cut |
| 08-01 | 670,208 | +512 | procedural audio + seeded RNG |
| 08-02 | 670,720 | +512 | PCG32 with independent streams |
| 08-02 | 712,704 | +41,984 | world + movement + fog — **40 KB of zeros in `.data`** |
| 08-02 | 673,280 | −39,424 | statics moved to stack; see [[Toolchain Setup]] |
| 08-02 | 674,304 | +1,024 | frame cap + centroid spawn |
| 08-02 | 678,400 | +4,096 | region graph, reachability invariant, debug overlay, grid view |
| 08-02 | 679,424 | +1,024 | ability gating, restoration loop, Found Soul states, confirm beat |
| 08-04 | 679,424 | +0 | render instrumentation — byte-identical, which is the proof it is absent |
| 08-04 | 680,448 | +1,024 | 32 px tiles, isometric projection, `vspan`/`iso_tile`, `--iso-test` |
| 08-04 | 680,960 | +512 | elevation: derived heights, cliff side faces, terraced rock |
| 08-04 | 682,496 | +1,536 | 960×540 logical backbuffer, integer upscale, F11 fullscreen |
| 08-04 | 683,008 | +512 | per-tile hash, ground grain, per-terrain surface marks |
| 08-04 | 684,544 | +1,536 | layered trees and bushes, two-sub-pass depth sort |
| 08-04 | 686,592 | +2,048 | rock/reed/flower/crystal/stump props, isometric interact ring |
| 08-04 | 689,152 | +2,560 | procedural buildings, mix-and-match house parts, `--village-test` |
| 08-04 | 690,176 | +1,024 | island landform, village clustering, `fill_ellipse`, palette pass |
| 08-04 | 690,688 | +512 | roof face split (`iso_diamond_lr`), fog rewrite |
| 08-05 | 690,688 | +0 | bitmap font, `--font-test`, F3 tuning overlay — all self-test-only |
| 08-05 | 690,176 | −512 | world rescale: `PX()`, `TILE` 32→24, grid 108×60 — *saved* bytes |
| 08-05 | 690,688 | +512 | screen-aligned input, eased camera, 8-direction speed test + control |
| 08-05 | 691,200 | +512 | houses: roof back on the box, windows back on the wall, taller storeys |
| 08-05 | 691,200 | +0 | `--land-test` and `--fog-test`, both with negative controls |
| 08-05 | **692,224** | +1,024 | rivers via BFS-to-sea, bridges that clear `solid`, river/plank colours |
| 08-06 | **779,776** | +5,632 | Phases 08+09 merged onto `main` (PR #1): save/load + restoration rebuild |
| 08-06 | **781,824** | +2,048 | Phase 10: motion — sway, shimmer, waterfall fall-lines, smoke, fireflies, soul-bob |
| 08-06 | **786,432** | +4,608 | Phase 11: softsynth (5 layers) + SFX + HUD/minimap/toasts/win banner + font lowercase |
| 08-11 | 1,029,120 | +512 | Bug Fix Plan 8/8 (iso overflow, gating/audio/abilities/seed/U64, watchtower/grant) |
| 08-12 | **1,080,320** | +51,200 | Invisible-wall + bridge/water veto + 30% thinning + single `castle_full` + cluster fog + path green |
| 08-12 | **1,085,440** | +5,120 | TASK-01 character switch Groups A–D: 6-way idle 136 sprites, `facing6_from_intent`) + `player_idle` + `IDLE_FPS 8.0` on `g->clock` |
| 08-12 | **1,085,440** | +0 | Session 17 review: real `facing6` controls, real `g->clock` assertion, one sprite encoder instead of two (`art_data.h` byte-identical), dead `WALK_*` removed |
| 08-12 | **1,085,440** | +0 | Session 17b: levitating houses fixed — restored `world_heights` flatten for baked buildings + sprite back to `cy` (Session 15 regression, same-seed before/after) |

Self-test builds (`wayfarer-selftest.exe`) are not deliverables and are deliberately excluded
from this table and from the budget gate.

## Standing risks

- **One playtest is not QA.** The game has been played by hand exactly once (2026-08-04, "slightly
  enjoyable"), by the person who wrote the design. The interact affordance, reach radius, movement
  speed and confirm beat have had one informal look and no more. [[QA Checklist]]'s "runs clean on a
  machine without dev tools" is a *different* item and is still unchecked.
- **Pacing may be short.** A shortest-path full clear is 30–82 seconds of walking. Real play with
  fog will be longer by an unknown multiplier. Feeds the region-count question in
  [[Open Decisions]].
- Audio exists now (5-layer synth, `--audio-test --layers --sfx` measured) but has never been
  **heard** by a human, only measured — same standing gap as motion's "does it read as alive".
- Nothing has been run on a machine other than the development box — [[QA Checklist]]'s
  "runs clean without dev tools" is discharged in code terms (static, stripped, no assets, no
  SDL_image/ttf/mixer symbols) but the actual second-machine smoke test is still a human task
  on the submission checklist.
- The gating-relaxation fallback in world generation has never fired (0 of 50 seeds), so that
  code path is untested against real failure.
- Physical keyboard input verified via posted window messages, not a real key press.
- **Nothing in the world moves.** RESOLVED by Phase 10 — sway, shimmer, waterfall fall-lines,
  chimney smoke, fireflies, soul-bob, every one a pure `(tile_hash, clock)` render-time function,
  `--motion-test` green (and the player is no longer a flat orange square — Phase 07's layered
  character shipped long since).
- **Buildings do not respond to restoration yet.** RESOLVED by Phase 09 — the ruin→whole rebuild
  (`bld_phase`, `--rebuild-test`) is in and green on `main`.
- **Pacing has not been re-measured** and is now further out of date: the tile size has changed
  twice (16→32→24) and the grid is 1.8× larger in tiles than anything measured. The 30–82 s figure
  predates all of it.
- **The new camera and the new art scale have never been judged by a human.** `CAM_DEADZONE`,
  `CAM_EASE` and `TILE` itself are all defensible numbers nobody has actually played with.
- **Rock outcrops read as pale scattered blocks under fog** at the new scale — stone is still the
  lightest large surface, so more, smaller outcrops pop out of the haze. A palette job for the F3
  overlay, not a renderer bug.
- Render cost is measured on one machine with the window unoccluded. `present` is an OS blit whose
  cost depends on the compositor and window state.
