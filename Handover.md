---
tags: [process, handover, wayfarer]
updated: 2026-08-07
exe_size_bytes: 846848
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

## Agent Log — who is doing what

> Append-only per agent. Do not rewrite another agent's row — the row below is qwen's own and is
> left as it wrote it; the resolution is appended as a note rather than an edit to its text.

| Agent | Focus | Branch | Tree state | Since | Notes |
|---|---|---|---|---|---|
| qwen | Phase 09: restoration rebuild + worn paths + ground marks | `feat/phase-09-restoration` | **DONE** — all three DoD items committed | 2026-08-06 | Rebuild → baked at 1.0; RNG-free paths; hash-gated marks |
| qwen | Phase 08: save/load | `feat/phase-08-save-load` | **DONE** — committed | 2026-08-06 | 28-byte versioned save; regen-from-seed + deltas; `--save-test` with 5 negative controls; +1,536 bytes |
| opencode | Phase 10: motion | `feat/phase-10-motion` | **DONE** — all 7 DoD items, committed | 2026-08-06 | sway/shimmer/fall-lines/smoke/fireflies/soul-bob; `--motion-test` 8 checks green; full suite green; +2,048 bytes → 781,824 |
| opencode | Phase 11: ship critical | `feat/phase-11-ship-complete` | **DONE** — all DoD items, committed (`8a27404`), PR pending | 2026-08-06 | 5-layer softsynth (deterministic, 0.325 ms worst case vs 21.333 ms deadline), chime/shard/portal SFX; HUD (counters, minimap, toasts, win banner) on the font extended with lowercase a–z + `/` (0x20–0x7A, 91 glyphs); `--hud-test` + `--font-test` green; full suite green; `nm` clean (no SDL_image/ttf/mixer); +4,608 bytes → 786,432 |
| opencode | Phase 11 bugfix: shard pickup | `feat/phase-11-ship-complete` | **DONE** — committed (`7cd408a`), pushed | 2026-08-06 | E-key restructure left the shard branch unreachable (nothing caught it: autopilot and `--shard-test` both bypassed the key path). Extracted the whole interaction into `try_interact()`, handler now calls it, `--shard-test` drives it (`shard pickup: PASS`); full suite re-run green; release size unchanged 786,432 |
| opencode | Rescale TILE 18 + HUD | `feat/phase-11-ship-complete` | **DONE** — committed (`068fea8` + `a8b178f`), pushed | 2026-08-06 | `TILE 24→18` denser tiles Option B (`144×138` world, 19,872 tiles, `REVEAL_TILES 7→9`, villages `4→6`/`12→16`/`29→39`/`22→32`), stack guard `400→700KB`, `soul_bob` floor 2px, minimap `2→1px/tile` (19,872px vs 79,488px); full 23/23 PASS, render 1.393ms, `+512` → 786,944 |
| opencode | Phase 13 Aetherhold Slice 1–2 | `feat/phase-11-ship-complete` | **SUPERSEDED, see note below** — `5630985`, `42a4081`, `86fad03`, `6580258` | 2026-08-06 | Replaced bad rectangle/old Dream-gap gate with southeast fixed island rows 35–76, mainland key `(88,59)`, overworld causeway `x94..107,y56`; supplied `assets/dark_fantasy` wall/building/prop pack baked as `AETHER_*` (93 records / 82 streams / 11 dream variants); full suite + `--aether-test` green; dungeon/keep follow-up deferred |
| (unattributed) | Aetherhold relocated + world density +30% | `feat/phase-11-ship-complete` | **DONE** — `eac01fd`, `95f108d`, `47736d6`, `f9ebc16`, `25b50de`, `0dae0ab` | 2026-08-07 | Superseded the row above same-day: world `144×138→164×157` (`OVERWORLD_H 91` / `DREAM_GAP 6` / `DREAM_H 60`, kept `TILE 18`, assets unchanged size); Aetherhold moved from the SE mainland-connected island to a **water-locked top-right island** (`110,2`, `42×42`, causeway still `x94..107,y56`); the mainland `castle_key` item was **removed** — the causeway now opens as a side effect of restoring the Dream Well's Soul (`has_castle_key` set when `WELL_SOUL_IDX` restores, verified by `--aether-test`'s "castle bridge" case); minimap now marks dream shards (cyan); asset bake trimmed to the 78 sprites actually drawn (133 KB, was 108/332 KB) after a short-lived 108-record top-right pack: net release size **fell** to `846,848` bytes despite two density bumps. Full suite + `--aether-test` + 50-seed `--play-test` re-verified green by a Claude session same day — see the note directly below |

**Doc-sync note, 2026-08-07 (Claude).** This Handover, `Phase Roadmap.md`, and both `Phase 13` files described the Slice 1–2 state above (southeast island, mainland key) for a full day after the code moved past it — the only commit touching this file in between (`0dae0ab`) patched two byte-count numbers, not the narrative. If you are reading a cached or older copy of this section, or of the Phase 13 files, do not trust the SE-island/mainland-key description; the row immediately above and the Phase 13 files (now corrected) are current. Lesson for future sessions: a "docs" commit that only edits numbers is not the same as a docs commit that re-reads its own prose.

**Resolved 2026-08-06, later still.** Phases 08 and 09 merged into `feat/phase-08-and-09`, PR #1
merged into `main` (`b4f2fde` — that branch also carried all of Phase 12, so `main` already has the
dream realm). Everything after `main` now sits in ONE straight line on **`feat/phase-11-ship-complete`**:
Phase 10 (`d78b106`, `0f90bb2`), Phase 11 (`8a27404`, `95e8872`, `0f87552`), shard-pickup fix
(`7cd408a`) plus docs (`7a39eac`, `b2f24b0`, `aa4336f`), rescale `TILE 18` (`068fea8`) and minimap
`1px/tile` (`a8b178f`) — all pushed, branch tracking origin. The older `feat/phase-10-motion`,
`feat/phase-11-ship` and `feat/phase-11-ship-latest` branches are superseded; PR and merge should
come from `-complete`. **Phase 11 (ship critical) is the last required phase and it is DONE.**
What remains is the submission checklist: merge to `main`, second-machine smoke test, repo
visibility at submission time, final wrap.

**Resolved 2026-08-06, same day.** `fx_well` is baked, slice 5 (dream shards + the Dream Well) is
finished and committed on `feat/phase-12-dream-realm` — the branch qwen's row names does not
actually exist in this repo, locally or on the remote, so its edits landed directly on
`feat/phase-12-dream-realm` alongside this session's. **Two things any future session should know
before trusting a shared doc here:** qwen's edit to [[INDEX]] reverted that file's status summary
to stale Phase-07-era text, since corrected; and it left `opencode.json` (its own tool's config,
with live-looking API keys in plaintext) untracked at the vault root. That file is now gitignored
and was never committed, but it is still on disk — if a fresh clone doesn't have it, that is
expected, not a regression.

**Correction 2026-08-06, later session (qwen).** The Phase 09 work the note above describes as
having landed on `feat/phase-12-dream-realm` did not: it was committed on its own branch,
`feat/phase-09-restoration` (`57b7de9`, created from `098d232`), now pushed to origin — so the
"branch does not exist" statement above is stale. Phase 09's three items (restoration rebuild,
worn paths, ground marks) are DONE there. Phase 08 (save/load) is DONE on `feat/phase-08-save-load`
(`0752672`). Both are now merged into `feat/phase-08-and-09`, which carries the combined work.

---

## 0. Start here — the ninety-second version

| | |
|---|---|
| **State** | 846,848 bytes, builds clean, full suite green (incl. `--hud-test`, `--font-test`, `--audio-test --layers --sfx`, `--aether-test`, and the `try_interact` shard/key regressions), plays to completion on 50/50 seeds — re-verified live 2026-08-07. All twelve phases done — plus `TILE 18` denser `164×157` world, `1px` minimap, and Aetherhold Slice 1–2 wiring (now on a top-right island, see the 2026-08-07 Agent Log rows) |
| **Branch** | **`feat/phase-11-ship-complete`** (Phase 10+11+shard fix+rescale+minimap+Aetherhold, pushed through current work) — `main` is at `b4f2fde` (Phases 08+09 merged via PR #1); this branch is that `main` + later work. Working tree clean |
| **Deadline** | 2026-09-04 — **all development phases are complete**; only the submission checklist remains |
| **Do first** | **Everything through Aetherhold Slice 2 is built.** Audio (5-layer softsynth), HUD (`1px` minimap), save/load, dream realm (`164×157`), motion, supplied castle assets, a **top-right water-locked Aetherhold island** reached by a causeway that opens when the Dream Well's Soul is restored (no separate key item) — all in, all green. Remaining: merge `feat/phase-11-ship-complete` to `main`, run the [[QA Checklist]]'s human items on a second machine, confirm repo visibility, final wrap; dungeon/keep expansion remains Phase 13 follow-up |
| **Then** | Submission: final size audit (**540,416 headroom**), tag the submission commit, push |
| **Biggest risk** | **Nothing has been heard, watched or played by a human outside the dev loop.** Audio and motion are measured, not judged; the second-machine smoke test has never run |

> **THE PROJECT CHANGED DIRECTION ON 2026-08-05, AND THAT WHOLE PHASE IS NOW DONE.** A portal in the
> `TERRAIN_DARK` region leads to a second biome — the team's "Lumiara / Dream Realm" concept art.
> It was specced as [[Phase 12 - Dream Realm]], planned as [[Phase 12 - Dream Realm Plan]] (11
> tasks, 5 slices), and **all 5 slices are built, tested and committed** as of 2026-08-06. A session
> reading only §11's old ordering would still pick the wrong task — the roadmap in §11 below is the
> current one.

**Where Phase 12 landed (now denser at `TILE 18`):** the grid is **164×157** (bumped from `144×138`
on 2026-08-07, `+30%` density: `OVERWORLD_H 91` / `DREAM_GAP 6` / `DREAM_H 60`), holding two
landmasses — overworld rows 0–90, an always-solid void band 91–96, dream archipelago 97–156. A
portal pair links them, travel is an `E` interact gated by whether you could actually stand on the
far end, and `--gating-test` passes 30/30 with that edge live. The dream realm has a look, the
portal has art, a prompt indicator is the first UI and first ambient motion this game has had, 4
fragments and 2 Found Souls live past the portal, and 8 dream shards feed a Dream Well that unlocks
the second one. **`--play-test` is 50/50 with the whole loop exercised** — portal crossings, shard
collection, the Well unlocking and redeeming its Soul, all by the same autopilot that plays everything
else. Original was `108×104`; rescale `068fea8` kept the same screen footprint with 77% more tiles.

> **PLAY IT, NOT JUST THE TESTS — 2026-08-06 made the case again.** Slice 3 was reported done with
> the whole suite green. The user opened seed 1, walked through the portal, and **could not move**:
> the arrival tile was in a Wade-gated region, so `tile_blocked` refused it. **11 of 100 seeds.**
> No checker could have caught it — `--gating-test` and the arrival tile *agree* that a gated tile
> is unenterable, and `--portal-test` measured 100/100 either way because **a component you cannot
> stand in is still one you can reach**. See decision 48 and §7.

**The team's art is in the build as of 2026-08-05.** [[Phase 07 - Asset Seam]] built the
PNG→header bake and pushed **37 real sprites** through it: a 4-direction 4-frame walking
character, 10 buildings, 11 nature props. Cost **+62,976 bytes** against 684,800 still free. That
retires four separate "does not exist" items at once — the orange square, the walk cycle, facing,
and any animation at all.

> **THE OCCLUSION QUESTION IS SETTLED AND BUILT — 2026-08-05.** Four handovers carried this as the
> top open item. The user chose the real fix over the two cheap ones — not thinning the trees, not
> scaling the sprites down, but **fading any prop drawn over the player**. Shipped with
> `--fade-test` (a selection truth table plus a pixel-exact blend check, two negative controls),
> **+0 bytes**, and confirmed on screen: the character reads clearly through a ghosted canopy.
> See decision 40. Two sibling art fixes landed with it — decisions 41 and 42.

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
| **`build\wayfarer.exe`** | **846,848 bytes** — 593,152 under the ship target |
| `build\wayfarer-selftest.exe` | 914,944 bytes — **not a deliverable**, never shipped |
| `src\main.c` | ~11,840 lines, single translation unit |
| `src\art_data.h` | **GENERATED** by `tools/bake.ps1`, committed. **78 records over 67 pixel streams** (11 are dream palette variants sharing a twin's stream), 133,425 bytes of const data. Curated dark-fantasy subset actually used; no unused castle pack baked. Never edit by hand |
| Warnings | zero, under `-Wall -Wextra` |
| Plan progress | **Phases 00–12 all done** — including 08 (save/load), 09 (restoration rebuild), 10 (motion), 11 (audio + HUD, ship critical) and 12 (dream realm, 5/5 slices). See [[Phase Roadmap]]. Only the submission checklist remains |

> **If you are starting here: all twelve phases are done, and Phase 13 Slice 1–2 is implemented.**
> The changes since ship-critical are the denser `TILE 18` world (now `164×157`, bumped again
> 2026-08-07), smaller minimap, a **top-right** water-locked Aetherhold island reached by a
> causeway that opens when the Dream Well's Soul is restored (the earlier southeast island and
> mainland key item are gone — see the 2026-08-07 Agent Log rows), and supplied dark-fantasy castle
> assets. Phase 11 landed audio and the HUD;
> the E-key restructure had silently made shard pickups unreachable, now extracted into `try_interact`
> and covered by a `--shard-test` regression; the grid went `108×104→144×138` at `TILE 18` keeping
> the same screen extent with 77% more tiles. Everything ships from `feat/phase-11-ship-complete`
> (pushed `6580258`). What remains is the human submission checklist — merge to `main`,
> second-machine smoke test, repo visibility, final wrap. Check the Agent Log at the top of this
> file for anything another agent may have picked up.

### What actually works right now

- **FIVE-LAYER PROCEDURAL MUSIC, deterministic by construction** — [[Phase 11 - Ship Critical]]:
  Base (C2 saw drone), Strings (saw arpeggio), Pad (sine), Bells (sine plucks with decay),
  Voice of Souls (sine + 6 Hz tremolo). Static pattern tables; every voice is a pure function of a
  sample counter, so two fresh states produce bit-identical 96,000-sample streams (proven).
  Fragment restores switch on Strings→Pad→Bells at frag counts 1/2/3; souls switch on the Voice.
  `--audio-test --layers --sfx` measured: worst case 0.325 ms of the 21.333 ms deadline, peak
  0.9151, no NaN/clip/partial writes. Chime/shard/portal SFX beyond the confirm beat.
- **A HUD, on the un-gated bitmap font** — counters top-left (fragments, souls, "the land is
  whole" when complete), restore toasts bottom-centre (fading), a one-shot win banner, the
  shareable seed bottom-right, and a minimap top-right (1 px/tile, cached, redrawn on dirty or
  every 15 frames) with the confirmed legend You / Restored / Unrestored / Soul / Fragment. The
  font was extended from uppercase-only (0x20–0x5F) to 91 glyphs (0x20–0x7A): lowercase a–z and
  `/` — the original table silently skipped every lowercase char, which is why the first HUD
  pass rendered nothing but digits. `--hud-test` probes every element's pixels; `--font-test`
  now covers all 91 glyphs with its stride negative control
- **TWO landmasses in one grid, and a portal between them** — [[Phase 12 - Dream Realm]], now
  **entirely done, all 5 slices**. The grid is **144×138** (was `108×104`): overworld rows 0–79,
  an always-solid void band 80–84, dream archipelago 85–137. `world_gen` runs `gen_sector` twice,
  normalising `fy`
  *inside* each row range so the radial term makes two islands rather than one lobed one, and
  giving each its own water rim so they share no tile edge. `dream_sector(ty)` is the only thing
  that knows where the sector is
- **8 DREAM SHARDS FEED A DREAM WELL THAT UNLOCKS ITS OWN FOUND SOUL.** Shards live in their own
  array — never `ents[]`, so `game_complete`'s all-entities mask is untouched. Feeding
  `SHARD_REQUIRED` (6 of 8) makes the Well's Soul redeemable, which is a pure function of
  `shards_held` rather than a stored lock bit. Pickup runs through `try_interact` — the exact
  function the E key calls — because that branch was once unreachable and every test at the time
  bypassed the key path (see the Agent Log and decision 60). `--play-test` is 50/50 with her
  unlocked and redeemed on every seed. See decisions 53–56 and [[Phase 12 - Dream Realm]]
  Evidence, slice 5
- **THE DREAM REALM LOOKS LIKE A DIFFERENT PLACE, and it cost almost nothing.** `dream_shift()` is
  the one definition of the biome's colour; `tools/bake.ps1` carries the same formula because a
  sprite palette is recoloured at *bake* time, and `--sprite-test` checks every baked `_DREAM`
  entry against the C function so the two cannot drift. **11 dream props are the SAME pixel
  streams with a new palette** — ~600 bytes against ~19 KB to re-author — and `--sprite-test`
  asserts that sharing rather than assuming it. The void's starfield is `tile_detail`'s existing
  speckle with a pale colour. See decisions 46–47
- **The portal is drawn**, at both ends: `ART_BLD_PORTAL_ARCH` (baked since Phase 07 and drawn by
  nothing until now) with 8 `fx_portal` frames turning in its opening, driven by `Game.clock`
- **A PROMPT INDICATOR — the first UI this game has drawn.** `draw_prompt` is procedural (four
  rectangles make the `E`), sized against the bitmap font's legible 10×14 so it is not a speck, and
  bobs ±2 px off `Game.clock`. Three states: interact, travel, and a **padlock** for a portal you
  cannot use yet. See decision 51
- **The Kindle gate on the portal actually gates.** `try_portal` requires the end to be *standable*,
  not merely within `PORTAL_REACH` — which is 25.5 px against a 24 px tile step, so it used to fire
  from the tile next to the portal and the ability gated nothing. See decision 50
- **4 fragments and 2 Found Souls live past the portal**, quota-placed inside the reachability
  filter, and `--play-test` crosses on all 50 seeds. See decision 52
- **A portal that is a graph edge, not a collision case.** `portal_link()` is read by
  `tile_neighbours()`, which **six** traversals route through, so walk-reachable ==
  graph-reachable is true by construction and `--gating-test` needs no weakening. Travel is an
  `E` interact (`try_portal`), so `tile_blocked` is untouched. `--portal-test` measures 100/100
  seeds shrinking when the portal is suppressed. See decisions 43–45
- **Props fade when they cover the player** (decision 40) — `draw_sprite_fade` blends 50/50
  against the framebuffer, which already holds her because she is drawn in an earlier band
- Procedural **island**, not a cave: coastline, ocean with a stepped sea floor, inland rock
  outcrops, seeded, regenerable in-game with **R**. See [[Phase Roadmap]] Phase 01
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
  fog constants — `TAB` cycles rows, `-`/`=` adjust. The overlay is self-test-only and costs the
  shipping build 0 bytes; the **font is un-gated since Phase 11**, because the HUD is the real
  caller that lifted decision 25's gate. `fog_lerp` reads `FOG_*_V` macros that expand back to the
  literals in a release build. See [[Phase 03 - Legibility Tools]]
- **The game has been played by the user and friends, repeatedly** — the 2026-08-04 pre-art "the
  gameplay is in early stage, it did feel slightly enjoyable", the 2026-08-05 art-in session, and
  2026-08-06 sessions that found and re-verified the shard bug by hand — see §8

### What does NOT exist yet

Every item on the old list is now built: music, on-screen text, save/load, ambient motion, the
ruin→whole rebuild, worn paths, Found Soul sway and audio layers all ship. What genuinely remains:

- **Wade-splash SFX** — deferred per [[Cut List]]'s descoping order (#5), documented, not forgotten
- **A human judgement of the audio** — measured (deterministic, 0.325 ms worst case), never heard;
  "does it sound good" is a human-ear question nobody has answered
- **The second-machine smoke test** — the last unchecked [[QA Checklist]] item; a human task
- **A public repo** — private today; the contest's visibility requirement is unconfirmed
- **Shard-vs-decoration legibility** — the collect-8-find-6 loop's pickups still read like ambient
  `PROP_CRYSTAL` decoration in stills; the palette/size pass is an unjudged open item (see §11)

### Git

Remote: **`https://github.com/Nishmam12/matha-noshto-game`** — private, branch `main`.

**Current branch: `feat/phase-11-ship-complete`, HEAD `0dae0ab`, pushed, tracking
`origin/feat/phase-11-ship-complete`.** `main` is at `b4f2fde` (Phases 08+09 merged via PR #1) —
and that merge's base branch carried all of Phase 12, so `main` already has the dream realm. A
straight line from `main` holds everything else: Phase 10, Phase 11, and the shard-pickup fix. The
older `feat/phase-10-motion`, `feat/phase-11-ship`, `feat/phase-11-ship-latest` and
`feat/phase-12-dream-realm` branches are all superseded by it. Working tree is clean as of this
handover's own commit — verify with `git status` before trusting that, since this note has needed
correcting before.

```
0dae0ab  docs: Handover for trimmed bake (78 sprites, 846KB) and top-right island  <- HEAD, PUSHED
25b50de  trim bake to used assets only: 78 sprites (133KB) vs 108 (332KB), -199KB
47736d6  castle: bake new top-right island pack (108 sprites, 332KB) + bridge via Well Soul
f9ebc16  minimap: show dream shards (cyan) in dream realm
95f108d  aetherhold: top-right water-locked island (110,2 42x42) + dream-bridge via Well Soul,
                                                            remove mainland key, gap only
eac01fd  world density +30%: 144x138->164x157 (91+6+60) keep TILE 18, assets same size
00155a2  docs: finalize Handover with southeast Aetherhold and dark-fantasy pack   (now superseded
                                                            by the four rows above)
6580258  phase13: replace fake NE castle with supplied dark-fantasy assets on southeast island
7a39eac  docs: update Phase 11 ship summary (unified interaction + shard fix)
7cd408a  fix: shards uncollectable since the E-key restructure - extract try_interact, test it
0f87552  docs: Phase 11 ship-critical summary (softsynth, HUD, tests)          (user-authored)
95e8872  docs: Phase 11 ship-critical summary, devlog session-06, handover + roadmap + QA updates
8a27404  Phase 11: ship critical - 5-layer softsynth, multi-SFX, HUD with minimap, toasts, win
                                                            banner (main.c +671/-58)
0f90bb2  docs: Phase 10 motion summary detailing all changes and additions      (user authored)
d78b106  Phase 10: motion — sway, shimmer, smoke, fireflies, soul-bob
b4f2fde  Merge PR #1 (Phases 08+09 — and, through that branch, all of Phase 12)  <- main, PUSHED
```

**`assets/` (217 files, 1.2 MB) IS committed** as of `df480a6` — the source PNGs plus their Godot
`.import` sidecars. The sidecars are editor metadata that nothing reads; whether they should be in
the repo at all is still undecided.

**Do not assume pushed == committed, or committed == pushed.** This handover has needed correcting
on exactly this point more than once. Check `git log origin/feat/phase-12-dream-realm..HEAD` before
claiming anything is backed up, and `git status` before claiming the tree is clean.

**Do not add `Co-Authored-By` trailers to commits.** This was asked for explicitly and one had to
be stripped and force-pushed once already. It is recorded in persistent memory (§10) so it should
never need saying again.

`build/` and `.obsidian/` are gitignored. `wayfarer.exe` is therefore not in the repo — attach it
to a GitHub Release if a playable download is wanted.

> **THE TEAM'S ART IS IN THE BUILD, still through `dark_fantasy/`.** `assets/` also gained a
> `castle/` pack on 2026-08-07 (108 new PNGs: bridges/causeway, secondary buildings, dungeon/
> interior, keep structures, nature, props, rocks/cliffs) meant for the newly-relocated top-right
> island, alongside the original `buildings/`, `nature/`, `player/`, `magical/`, `generated/`, and
> `dark_fantasy/`. **What actually happened (`47736d6` then `25b50de`, same day): `castle/` was
> baked once as `CASTLE_*` records (108 total combined with an expanded `dark_fantasy/` list, 332
> KB), then reverted** — `castle/` is committed but entirely unbaked source right now, and the
> renderer's `AETHER_*` identifiers still come from the original `dark_fantasy/` pack, trimmed back
> to the **14 files it actually references** (was 29). Current bake: **78 records over 67 pixel
> streams**, 133,425 bytes. What is deliberately NOT baked, and why:
> - **`castle/` (all 108 PNGs)** — supplied for the top-right island but has no caller yet; the
>   island's walls/keep/gate sprites are still the older `dark_fantasy/AETHER_*` set. If a future
>   session wires `castle/` in, expect the byte cost to jump — `tools/bake.ps1` prints the exact
>   record/stream count on every run, trust that over any number written down here.
> - **`magical/`'s remaining frames** (`fx_rift`, `fx_crystal`) — still no caller in the renderer.
>   Baking a sprite nothing draws is pure byte cost.
> - **`generated/`** — duplicates of the building sprites plus two sprite *sheets*.
> - **The `.import` files** are Godot editor metadata: not shipped, not baked, and arguably should
>   not be committed at all. That is still undecided.
>
> **The scale worry turned out to be unfounded**: supplied dark-fantasy assets are baked at their
> authored pixel scale and remain intentionally larger than a single `TILE 18` diamond, which is
> what makes the keep/walls skyline-readable. Procedural dimensions still use `PX()`; assets are
> not rescaled at runtime. See §9.
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
& $e --sprite-test                          # RLE round-trip, baked data, anchors, key colour + 3 controls
& $e --fade-test                            # prop-fade selection truth table + exact blend, 2 controls
& $e --sector-test --seeds 30 --seed 1      # two landmasses, void band, separation, spawn sector
& $e --portal-test --seeds 30 --seed 1      # portal load-bearing + standable landing + Kindle gate
& $e --shard-test  --seeds 30 --seed 1      # shard placement, the Well's boundary, PICKUP driven
                                          #   through try_interact (the real E path), both controls
& $e --gating-test --seeds 30 --seed 1      # walk-reachable == graph-reachable, all 4 tiers
& $e --play-test   --seeds 50 --seed 1      # full headless playthroughs to completion
& $e --audio-test 3000 --sfx                # callback timing under restore-beat load
& $e --autoplay 20000 --seed 3              # windowed autopilot; watch restoration happen
& $e --input-test 4000 --seed 5             # real keyboard path, reports position delta
& $e --frames 400 --perf --seed 4           # render/present/sleep ms, px and calls per frame
& $e --frames 60 --seed 4 --overlay --shot out.bmp   # scripted screenshot — see the recipe in §10
& $e --frames 90 --seed 5 --dream 1 --shot d.bmp     # stand IN the dream realm; --dream 0 = the
                                                     #   overworld end. Self-test only
& $e --frames 1 --seed 5 --shards 6 --overlay --shot w.bmp  # the fed Well; --shards 0 = dormant.
                                                     #   --shard-at N does the same for a shard
```

**All currently pass, re-verified live 2026-08-07 (Claude session) after the top-right relocation,
`164×157` density bump, and trimmed bake** — full suite (including `--aether-test` against the new
island position and Well-Soul gate) plus `--play-test --seeds 50` all green, release rebuilt clean
at `846,848` bytes. The numbered detail below is the **2026-08-06** run, kept as evidence for the
southeast-island/mainland-key layout it was measured against — the shapes of most numbers (open
tile %, region counts) will differ slightly under `164×157` and the relocated island, but no suite
regressed. Re-run and replace this block wholesale next time a session has an hour to spend on it;
until then, trust the PASS/FAIL headline, not the exact per-seed numbers:

```
audio   : PASS  tone 439.9 Hz; --layers peak 0.6417; --layers --sfx peak 0.9151,
                NaN 0, clipped 0, partial writes 0; worst case 0.325 ms of the
                21.333 ms deadline (1.5%); determinism PASS — 96,000 samples
                from two fresh states bit-identical
hud     : PASS  counters 2,092 lit px; minimap 44,928/44,928 px covered; player
                marker exact-white on its minimap tile; seed line 296 lit;
                toast shown 1,138 lit then expired 0; win banner 3,364 lit;
                whole-land line 1,492 lit
font    : PASS  91 glyphs, 3,736 lit px expected == rendered; negative control
                caught the off-by-one stride (3,576 vs 3,736)
sector  : PASS  100 seeds; both sectors walkable, void band empty, an overworld
                flood leaks into 0 dream tiles, player spawns in the overworld on
                all 100; control (sector-blind predicate) rejected on 40/104 rows
portal  : PASS  100/100 seeds shrank when the portal was suppressed; travel works
                both ways and lands on open ground; control (E away from a portal)
                does nothing. LANDING: 100/100 seeds standable with no abilities,
                mean 818 tiles walkable, worst 6, 4 under 40; control (arrival
                region gated by hand) caught. GATE: with the end gated by hand E is
                refused ON the tile and BESIDE it, and works again with Kindle
shard   : PASS  30 seeds, all placements >=6/8 shards in the dream sector, Well
                and its Soul co-located; control (shard-starved world) rejected;
                PICKUP: a shard under the player is collected through the exact
                function E runs (try_interact) — regression for the branch that
                was unreachable once; WELL BOUNDARY: 5 shards locked, 6 shards
                unlocked, redeemed through the real interact path
fade    : PASS  selection 8/8 cases; blend 1,808 px exact half-blend vs an
                SDL_GetRGB-derived reference; both controls fire (band-blind
                predicate rejected on the 2 cases that matter, fade-ignoring
                blitter caught). PROMPT: bob within +/-2 AND asserted to move;
                NONE writes 0 px; the 3 kinds render 400/475/438 px so none is a
                duplicate; control (unclamped sine) rejected on 264/400 samples
sprite  : PASS  round-trip 700 px -> 283 bytes -> 700 px pixel-exact; 93 records,
                298,130 px from 194,068 RLE bytes (1.54x), 2,578 palette entries;
                anchors all bottom-centre; no key magenta in any baked palette;
                three-sided control (halo caught, outline kept, stone kept);
                11 dream pairs share their pixel stream and their palettes match
                dream_shift within 1; control (unshifted palette) rejected on
                30 of 30 entries
bridge  : PASS  200/200 bridge-bearing seeds shrank the player's reachable
                component when bridge decking was suppressed
land    : PASS  20 seeds (100 also clean); RE-AIMED PER SECTOR three times,
                never loosened - see the note below; both controls fire, and the
                drowned-map one now trips 4 assertions instead of 3
fog     : PASS  0 collapsed ramps; 56 colours x 5 reveals, 0 inversions,
                5 collapses; control (contrast-crushing blend) caught;
                value hierarchy: stone tops out at 74 vs grass 78, and the
                control rejects the ramp that actually shipped (89);
                DREAM hierarchy ground 52, stone 49, void 38, and its control
                rejects a mechanically shifted sea ramp (62 vs 52)
iso     : PASS  0 px owned by the wrong tile under elevation; upscale x1/x2/x3 exact
village : PASS (0 failures across 20 seeds), mean 12 buildings per world (clustered, not
                scattered — see Phase 01). Both negative controls fire
rng     : PASS (0 checks failed)
move    : PASS (0 failures across 20 seeds); direction-independent speed confirmed
region  : PASS (0 failures across 20 seeds)
reach   : PASS (0 failures across 20 seeds); negative control PASS; gating relaxed on 0/20;
                SPLIT: >=4 fragments and >=2 Souls past the portal on every seed,
                with an all-or-nothing control; attempts 1 on all 20 seeds
gating  : PASS (0 failures across 20 seeds)
play    : PASS (0 seeds could not be completed) — CROSSES THE PORTAL on every
                seed (2-4 times) and the Well's Soul is unlocked and redeemed on
                every seed. A crossing count of 0 fails the seed
save    : PASS  round trip bit-identical to the snapshot; 5 negative controls
rebuild : PASS  ruin->whole phase gate; control (always-baked predicate) rejected
ground  : PASS  determinism: two marks-on frames identical
motion  : PASS  render determinism at one clock; 23,552 px differ at a second;
                per-helper bounds and the 8-row mote gate table
perf    : render 1.832 ms mean (4.697 ms max) of a 16.67 ms budget, 59.7 fps
          — measured with the HUD (1px minimap) and supplied Aetherhold pack live at
          `TILE 18`/`144×138`; mean remains under 2 ms
```

> **`--land-test` has now been re-aimed THREE times for the two-sector grid, and never loosened.**
> Every time the *threshold* stayed and only the *denominator* changed, because `total` spans two
> landmasses and "can the player reach half the world" became the wrong question. The 50%
> reachability bound and the 12.5% landmass bound both measure against the player's **own sector**
> now, and a new assertion requires the far sector's largest component to be ≥6.25% of *its* area.
> Justifications are in [[Phase 12 - Dream Realm]] Evidence. **If a bound has to move again, write
> down why — do not nudge the number.**
>
> **`--play-test`'s 50/50 currently proves less than it looks.** Every entity still lives in the
> overworld, so the autopilot has no reason to cross the portal and travel is never exercised by
> it. Travel is proven by `--portal-test`'s assertions instead. This stops being true at task 9,
> which puts 4 fragments and 2 Souls in the dream sector — **that is the run to watch.**

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

`WASD`/arrows move · `E`/`Space` interact — portal, restore, or shard pickup via `try_interact` ·
`F1` region overlay · `F2` 12-seed grid · **`F11` borderless fullscreen** · `R` regenerate with
next seed (also restarts the music) · `F9` quick-save, `F5` quick-load · `ESC` quit. Stats are in
the **window title**; the on-screen HUD carries the counters. `--frames N` runs exactly N frames
then exits 0. `--scale N` forces the window scale.

**Self-test binary only:** `F3` toggles the live fog-tuning overlay, `TAB` cycles the selected row,
`-`/`=` adjust it. `--tune` starts with it already shown, the same way `--overlay` starts with F1
held, so it can be screenshotted without a human at the keyboard.

> **`W` now moves up on screen.** Input was world-aligned until 2026-08-05; if any older note still
> says `W` travels up-right, that note is stale — see decision 28.

---

## 5. Code map — `src/main.c`, in order

Line numbers below were measured at `7cd408a` (2026-08-06) — the file is now **~10,530 lines**.
Treat this as a map of the file's *order* and **trust the grep, not the number.**

**Phase 11's new symbols, in file order** (grep for these, they have no reliable line numbers):

| Symbol | What it is |
|---|---|
| `typedef struct { ... } Audio` (~288) | All synth state. The game thread may touch ONLY the atomics below |
| `sfx_fire(a, kind)` / `SFX_CHIME`/`SFX_SHARD`/`SFX_PORTAL` (317, 334) | One-entry SFX requests; the callback drains them |
| `layer_fire` / `voice_fire` / `reset_req` (460–462) | The `SDL_atomic_t` fields the game thread bumps — fragment layers, the Voice of Souls, full synth reset (R/F9) |
| `synth_latch` / `synth_step` (496, 527) | Callback-side: latch the atomics once per block, step every voice as a pure function of a sample counter |
| `FONT_FIRST`/`FONT_LAST`/`FONT_GLYPHS` (3345–3350) | **0x20–0x7A, 91 glyphs.** Was uppercase-only — every lowercase HUD string silently rendered nothing until this extension |
| `draw_glyph` / `draw_text` / `draw_text_shadow` (3442+) | The font, now un-gated: the HUD is the real caller that lifted decision 25's gate |
| `hud_draw` (3586) / `HUD_TOAST_FRAMES` (3489) | Counters top-left, cached minimap top-right (redraw on `mm_dirty` or every 15 frames), toasts bottom-centre (fade last 30 of 180), seed bottom-right, win banner |
| `try_interact` (3650) | **The whole interact key in one function**: portal → restore → shard pickup, returns 1/2/3/0. E/Space call only this. It exists because the shard branch was unreachable once — see decision 60 |
| `font_selftest` (9408) / `hud_selftest` (9522) | The pixel-probe checkers behind `--font-test` / `--hud-test` |

**Phase 12's new functions, in file order** (grep for these, they have no reliable line numbers):

| Symbol | What it is |
|---|---|
| `OVERWORLD_H` / `DREAM_GAP` / `DREAM_Y0` / `DREAM_H` | Sector geometry, in the tunables block beside `WORLD_W` |
| `dream_sector(ty)` | The one-line predicate that knows where the dream realm is |
| `wayfarer_stack_guard` | Compile-time assert: `World + Scratch` must fit 400 KB. Beside the `Scratch` typedef |
| `gen_sector` | One landmass over rows `[y0, y1)`; `world_gen` calls it twice |
| `DREAM_ROUGH` | Dream coast roughness, beside `LAND_ROUGH` |
| `PORTAL_SUPPRESSED` / `g_suppress_portal` | Self-test-only gate for `--portal-test`, mirroring `g_suppress_bridges` |
| `portal_link` | Paired tile for a portal end, or −1 |
| **`tile_neighbours`** | **Graph adjacency: 4 orthogonal + the portal. Six readers.** Just above `bfs_open` |
| `biggest_component_tile` | Random open tile in the largest component of a row range |
| `place_portal` | Both ends, in `game_init` after buildings and before `regions_build` |
| `portal_in_reach` / `try_portal` / `PORTAL_REACH` | Travel, beside `entity_in_reach` |
| `prop_covers_player` | Pure predicate for decision 40's fade selection |
| `draw_sprite_fade` / `draw_sprite` | Blitter with a 50/50 blend; `draw_sprite` is the opaque spelling |
| `stone_ramp` | Now at **file scope** beside `wall_pal`, so `--fog-test` can assert on it |
| `ART_KEY_MAGENTA` / `art_is_key_magenta` | Decision 41's explicit four-colour list |
| `sector_selftest` / `portal_selftest` / `fade_selftest` | The three new checkers |

**Slice 3's new symbols** (2026-08-06, grep for these):

| Symbol | What it is |
|---|---|
| `dream_palette(ty)` | **Render-only** sector predicate. Differs from `dream_sector` by the four void-band rows — see decision 49 |
| `dream_shift(r,g,b,...)` | THE definition of the biome's colour. `tools/bake.ps1`'s `ConvertTo-DreamColour` is the same formula, checked against it by `--sprite-test` |
| `void_ramp` / `water_ramp` | Both at file scope now, so `--fog-test` can compare them. `void_ramp` is authored, not shifted — decision 47 |
| `prop_art_dream[]` / `art_*_dream[]` | The dream flora tables, index-for-index with `prop_art[]` |
| `portal_frames[8]` / `PORTAL_FPS` | The vortex, beside `player_frames` |
| `Game.clock` | World animation time. Advanced by `sim_step`, read only by `render`. NOT `Player.anim`, which stops when she does |
| `walk_from` | Portal-BLIND gated flood, in the self-test. The second documented exception beside `land_flood` |
| `--dream N` | Self-test flag: 0 stands at the overworld end, 1 crosses. In `main`, beside `--grid` |

**Slice 4's new symbols** (2026-08-06, grep for these):

| Symbol | What it is |
|---|---|
| `draw_prompt` / `prompt_bob` / `PROMPT_NONE`/`INTERACT`/`TRAVEL`/`LOCKED` | The procedural keycap prompt. `prompt_bob` is pure, ±`PROMPT_BOB` clamped, checked to actually vary |
| `portal_usable` | Requires the end be STANDABLE (`!tile_blocked`), not merely within `PORTAL_REACH` — decision 50 |
| `DREAM_FRAGMENTS` / `DREAM_SOULS` | 4 and 2 — the sector quota, beside `FRAGMENT_COUNT`/`SOUL_COUNT` |
| `regions_by_sector` | One grid sweep producing `over_mask`/`dream_mask`, read by `place_entities` and `place_shards` |
| `entities_split_ok` | The quota check. Kept OUT of `world_solvable` — decision 52 |

**Slice 5's new symbols** (2026-08-06, grep for these):

| Symbol | What it is |
|---|---|
| `World.well` | The Dream Well's tile, or −1. Set once in `place_portal`, beside `portal[2]` |
| `SHARD_COUNT`/`SHARD_REQUIRED`/`WELL_SOUL_IDX` | 8, 6, and `FRAGMENT_COUNT` (the first dream Soul) |
| `Game.shards[8]` / `shards_held` | Tile-or-−1 per shard; the running collected count. Consumed on pickup, never carried |
| `near_open_tile` | Chebyshev ring search from a tile, starting at radius 4 — decision 53's neighbour, see the trap on why not 2 |
| `place_shards` / `shards_sufficient` | Placement (dream-sector only, no fallback) and the count clause, beside `entities_split_ok` |
| `shard_in_reach` / `try_collect_shard` | Same shape as `entity_in_reach`/`try_restore` |
| `draw_shard` | Procedural pickup, reuses `draw_crystal`'s shape with a fixed palette and a bob |
| `well_frames[8]` / `well_stage` / `well_frame` | The Well's 3-stage, pure frame selection — `well_frame` is swept the same way `prompt_bob` is |
| `--shards N` / `--shard-at N` | Self-test flags to screenshot the Well's stages / a specific shard without playing to that state |

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
| ~3345 | **Bitmap font** | `FONT_*` constants (3345), the flat `FONT_5X7` table, `draw_glyph` (3442), `draw_text`, `draw_text_shadow` — **0x20–0x7A, 91 glyphs, un-gated since Phase 11** (the HUD is the caller). The F3 tuning overlay is the only font-adjacent thing still self-test-only |
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
| `TILE` | **18** (was 32, then 24) | Diamonds are 36×18. **Changing it now really is free**: every authored dimension goes through `PX()`, so the whole visual scale follows. It did not before — see decision 26 |
| `PX(n)` / `PXF(n)` | — | "n px, as authored at a 32 px tile" (`TILE_REF`). Wrap **every** new hand-authored pixel dimension in it, or that art stops scaling with `TILE` and re-creates the "everything is too big" bug |
| `WORLD_W` × `WORLD_H` | **144 × 138** (was 108×104) | Two landmasses. `WORLD_H` is now *derived*: `DREAM_Y0 + DREAM_H`. `ISO_MAP_W/H`, `ISO_OX` and `BAND_MAX` all follow automatically. Rescale `24→18` kept the same screen footprint with 77% more tiles |
| `OVERWORLD_H` / `DREAM_GAP` / `DREAM_Y0` / `DREAM_H` | **80 / 5 / 85 / 53** | Overworld rows 0–79, always-solid void band 80–84, dream archipelago 85–137 |
| `DREAM_ROUGH` | **0.78 (new)** | Dream coast roughness vs `LAND_ROUGH` 0.55. **Roughness, not a higher sea threshold** — fragmenting the sector into genuinely separate islets would strand entities and the verifier would reject seeds forever |
| `PORTAL_REACH` | **`PXF(34)` (new)** | Interact radius for travel, same shape as `INTERACT_RADIUS` |
| `LOGICAL_W` × `LOGICAL_H` | 960 × 540 | Rasterised size; window is this × an integer scale |
| `PLAYER_SPEED` / `PLAYER_SIZE` | `PXF(220)` / `PX(24)` = 165 / 18 | Both scale with `TILE`; collision is scale-invariant because `player_blocked` divides by `TILE` |
| `REVEAL_TILES` | **9** (was 5, then 7) | In *tiles*, so it does not scale with tile size — raised by hand to keep the sight circle ~160 world px (7×24≈9×18) |
| `CAM_DEADZONE` / `CAM_EASE` | `PX(30)` / 0.16 | Follow-camera feel. **First guesses, never judged by a human.** Y deadzone is halved because the projection compresses screen y 2:1 |
| `SIGHT_MAX` | **0.50** (was 0.42) | Raised alongside the fog rewrite so walked ground keeps more colour |
| `FOG_TINT_R/G/B` | **60 / 70 / 86** | Was (44, 52, 68) — a *dark* blue-grey. Now a light cool haze. Took three tuning passes; **tune these with the F3 overlay in a self-test build, never by rebuild-and-screenshot again** |
| `FOG_KEEP` | **0.50** | Fraction of a colour's own luminance contrast preserved at reveal 0. Replaces a flat 0.55 luminance scale + 0.45 tint-pull that crushed contrast. Also F3-adjustable |
| `FONT_W` / `FONT_H` / `FONT_SCALE` | **5 / 7 / 2** | Glyph cell and its logical-pixel magnification. Charset is **`0x20`–`0x7A`: digits, A–Z, a–z, punctuation — 91 glyphs**, extended in Phase 11; the HUD renders lowercase now |
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
| `VILLAGE_SITES` / `VILLAGE_RADIUS` / `VILLAGE_SPACING` | **6 / 16 / 39** | All in *tiles*, so all re-derived by hand for `TILE` 24 then 18. Radius/spacing × 24/18 keeps a village the same physical size; sites raised because 4 in a 1.77× larger world read as empty |
| `BUILDING_TARGET` / `BUILDING_MAX` | **32** / 40 | Raised with the world size; mean is 32 per world at `144×138`. `BUILDING_MAX` stays the array bound |
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

New decisions from the Phase 12 slices 1–2 session (2026-08-05/06):

43. **The dream realm is a second landmass in the SAME grid, not a second `World`.** `WORLD_H`
    60 → 104. The alternative — two `World` structs swapped on travel — would have made
    `world_solvable` span two worlds, which is a **new completability argument rather than a
    re-run**, and this project's whole discipline is that re-running beats re-arguing. One grid
    means `bfs_open`, `regions_build`, `place_entities` and the verifier all pick up the second
    landmass through the code path they already used. Costs ~52 KB of `.bss` and **0 file bytes**.
44. **A portal is a graph EDGE, expressed once in `tile_neighbours()` and read by every
    traversal.** This is decision 33's shape applied to travel: a bridge clears `solid` rather
    than becoming a second signal collision reads, and a portal likewise never touches
    `tile_blocked` at all. **Six traversals route through `tile_neighbours`** — `bfs_open`,
    `flood_open`, `walk_regions`, `bfs_gated`, `autopilot_tick`, plus `regions_build`'s adjacency
    which needs the edge added separately because it only ever tests tile adjacency. It returns
    *candidates*, not passable tiles, because each caller has its own blocked test.
    **`land_flood` is the deliberate exception** and stays portal-blind: `--land-test` asks
    whether each sector is a real place on its own, which is a question about landmass shape and
    must stay independent of the graph.
45. **Travel is an `E` interact, not a step-on trigger.** Keeps `tile_blocked` reading exactly
    what it always read, so the completability proof stayed a re-run; and it removes the arrival
    ping-pong a step-on trigger would need a latch to suppress. **Kindle is not re-checked at the
    portal** — the ability gates the *route*, through the `TERRAIN_DARK` region collision already
    refuses without it, and checking again at the portal would be a second collision input wearing
    a disguise.

New decisions from the Phase 12 slice 3 session (2026-08-06):

46. **A dream sprite is the SAME pixel stream with a different palette, and a checker enforces
    that rather than a comment claiming it.** `ArtSprite` keeps `pal_off` separate from
    `data_off`, so a recolour costs one 16-byte record plus its palette — about 70 bytes against
    ~1,700 to re-author. The whole "the biome is nearly free" argument rests on this and it was
    asserted nowhere; `--sprite-test` now compares `data_off`/`data_len` across each pair, because
    an emitter that duplicated the stream instead would look identical on screen and quietly
    double the header.
47. **The recolour is a function of LUMINANCE ALONE, exists in two languages, and is kept in step
    BY TEST.** `dream_shift()` in `src/main.c` is the definition; `ConvertTo-DreamColour` in
    `tools/bake.ps1` is the copy that must exist because a sprite palette is recoloured at *bake*
    time. Luminance alone is load-bearing rather than lazy: every output channel is monotone in the
    input, so a recolour **cannot** reshuffle which of two colours is lighter — exactly the
    property `--fog-test` proves `fog_lerp` has. An RGB hue rotation offers no such guarantee and
    could silently invert a canopy ramp. `--sprite-test` checks every baked `_DREAM` entry against
    the C function; they agree within 1, which is PowerShell rounding half to even against C
    rounding half away from zero and nothing else. Same "in sync by test, not by discipline" shape
    as the key-magenta list. **The void ramp is the deliberate exception and is authored:** a
    mechanical shift of the sea ramp lands at luminance 62 against dream ground at 52 — a gulf
    brighter than the land floating in it, which is decision 42's fault in a new costume.
    `--fog-test`'s control rejects exactly that ramp.
48. **The portal's ARRIVAL REGION is a spawn, and `regions_assign_terrain` exempts it from
    gating.** `place_portal` must run before `regions_build` or the partition never sees the dream
    sector, so it picks both ends out of `solid` alone and cannot know what terrain they will be
    given — and the dream end sits deep in the region graph by construction, which is where the
    depth bias gates hardest. **Measured: 11 of 100 seeds teleported the player onto a tile
    `tile_blocked` refuses**, leaving her unable to move at all. A gate you arrive *inside* is not
    a gate, it is a wall behind you. Fixed in the one function that decides terrain; refusing to
    travel, or nudging her to a nearby open tile on arrival, were both rejected as collision logic
    papering over a generation fault. The new assertion is zero-versus-nonzero, so it has no
    threshold anyone can loosen later.
49. **A second, render-only sector predicate `dream_palette(ty)` sits beside `dream_sector(ty)`,
    and they differ by exactly the four void-band rows.** `dream_sector` is a statement about
    *gameplay* — which landmass a tile belongs to, read by placement and by the tests. Rows 60–63
    belong to neither, but they are the gulf the dream islands float in, and painting them in the
    overworld's sea blue drew a strip of ordinary sea along the horizon of a violet void. Widening
    `dream_sector` instead would have quietly handed four rows to the dream side in
    `--sector-test`'s counts and `--land-test`'s per-sector bounds.

New decisions from the Phase 12 slice 4 session (2026-08-06):

50. **`try_portal` requires the end to be STANDABLE, not merely within reach.** `PORTAL_REACH` is
    25.5 px and an orthogonally adjacent tile centre is 24 px away, so the interact fired from the
    tile *next to* the portal — and a portal in a Kindle-gated region could be taken from the
    ungated ground beside it. `--gating-test` could not see it: every traversal reaches the portal
    edge through `tile_neighbours` and expands from an end only after standing **on** it, so
    walk-vs-graph parity was true of a model *stricter than the real interact*. Requiring the end
    to be standable makes the two agree exactly. **This adds no input to collision** — collision
    still reads `solid` and `regions[].terrain` and nothing else; it is the interact consulting
    collision, which was always the allowed direction.
51. **The prompt indicator is PROCEDURAL, not three baked keycaps.** A departure from the plan,
    taken because nobody is authoring that art and a keycap generated by a script and then baked is
    the same machine drawing with a build step and ~900 bytes of blob in front of it. Drawn in code
    it costs no art data and **a real authored keycap later replaces the body of one function** —
    the same seam the team's sprites came in through. Not the bitmap font either, for the plan's own
    reason: `draw_text` is behind `WAYFARER_SELFTEST` and calling it would spend decision 25's
    +0-byte gate on a single letter. **Sized against the font rather than by eye:** the first
    version was a 10 px cap with a 4×5 px `E` and read on screen as a dark speck.
52. **The dream-sector entity quota is counted on TILE ROWS, applied INSIDE the reachability
    filter, and kept OUT of `world_solvable`.** Rows not regions, because `regions_build`
    partitions through `tile_neighbours` and a region can straddle both sectors — "which sector is
    this region in" has no answer for those. Inside the filter, because a dream region that cannot
    be entered must not be a candidate. Out of `world_solvable`, because that function means
    exactly one thing — every entity reachable in ability order — and should keep meaning it; the
    split is asked only by the primary generate-then-verify loop, and **the ungating fallback
    deliberately does not ask it**, since that path exists to guarantee a completable world at any
    cost. A thin dream realm still ships; an unwinnable one does not.

New decisions from the Phase 12 slice 5 session (2026-08-06):

53. **Whether the Well's Soul is redeemable is a PURE FUNCTION of `shards_held`, not a stored
    "locked" flag.** `entity_in_reach` excludes `WELL_SOUL_IDX` while `shards_held <
    SHARD_REQUIRED` and nothing else changes about her — no bit to set on unlock, no state that
    can go stale relative to the count that actually governs it. Same philosophy as `tile_reveal`
    being derived rather than stored.
54. **`place_shards` has NO non-dream fallback**, unlike the generic entity placement it otherwise
    mirrors. A region that cannot supply a dream-sector tile is simply skipped rather than falling
    back to anywhere in it — a "dream shard" found in the overworld would defeat the point. If too
    few land, `shards_sufficient()` rejects the seed and the existing retry loop tries again; no
    new mechanism, the same shape as `entities_split_ok`.
55. **The autopilot searches shards and entities in ONE combined nearest-target loop, and excludes
    the locked well-soul from it — for the same reason `entity_in_reach` does.** Without the
    exclusion the autopilot would walk straight to her, find `try_restore` refuses, and retarget
    her again next tick: the livelock shape decision 29 already names, just with a lock instead of
    a deadband. Caught before it ever ran, by reasoning from the existing pattern, rather than by
    watching `--play-test` hang.
56. **A shard pickup and a portal crossing both return 0, not 1, from `autopilot_tick`.** That
    function's contract is "1 if it RESTORED something"; neither is a restoration, and returning 1
    for either would inflate `--play-test`'s restored count past `ENTITY_COUNT` — the exact
    "restored 20/19" bug the portal-crossing code already fixed once, in the neighbourhood of
    decision 29.

New decisions from the Phase 11 ship-critical session (2026-08-06):

57. **The music is a static-table softsynth: every voice is a pure function of a sample counter,
    so determinism is by construction, not by discipline.** No samples, no allocation, no random
    access inside the callback. Two fresh states produce bit-identical 96,000-sample streams —
    proven — which is what "audio plays identically everywhere" rests on.
58. **The game thread touches synth state ONLY through atomics.** `layer_fire`/`voice_fire`/
    per-SFX `fire`/`reset_req` are `SDL_atomic_t`; the callback latches them once per block and
    owns everything else. R/F9 zero the synth via `reset_req` with no race by construction.
59. **Restore layers are a function of COUNT, not of which fragment.** Strings at frag count 1,
    Pad at 2, Bells at 3; souls add the Voice of Souls. No per-fragment identity lookup to
    maintain, and the reveal a player hears tracks the reveal she sees.
60. **The interact key runs ONE function: `try_interact` — portal → restore → shard pickup, in
    that order, returning 1/2/3/0.** This function exists because the Phase 11 restructure
    orphaned the shard branch and no test drove the key path (the autopilot calls
    `try_collect_shard` directly; `--shard-test` called `try_restore`). The ordering decision now
    lives in exactly one place, and `--shard-test` drives exactly that function — see §7.
61. **The HUD is verified by pixel probes, not screenshots.** `--hud-test` counts lit pixels per
    element, asserts the minimap's coverage, demands the exact-white player marker, and simulates
    world frames (surface refilled between draws) to prove the toast shows AND expires.
62. **A bitmap font with no lowercase is a silent black box.** The first HUD rendered nothing but
    digits because every string was lowercase and `draw_glyph` skipped out-of-range characters —
    no error, no warning. The charset is now 0x20–0x7A by decision, and `--font-test`'s expected
    count derives from the table itself (2,236 → 3,736 with zero test edits).

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

**New in the Phase 12 slices 1–2 session (2026-08-05/06):**

- **A colour-family heuristic could not separate the team's placeholder magenta from their real
  mauve stonework, and writing the checker FIRST is the only reason that was found.** The obvious
  rule — "magenta family and bright" — flagged the bridge's `9C839C`, a roof red `A51A35` and
  three purple-greys on the buildings. Saturation does not separate them either: the halo sits at
  0.59 and a perfectly good building colour at 0.54. **The populations genuinely overlap in RGB
  space, so no threshold exists to be found.** The fix is an explicit four-colour list. Had that
  test been written after the change, it would have passed against already-damaged art.
- **A constant that no checker can see gets retuned by eye and stays wrong.** `stone_ramp` was a
  `static` local inside `tile_colour`, was lowered once from 0x5c5a68 to 0x595764 by eye, and was
  *still* the brightest large surface. Moving it to file scope so `--fog-test` could assert on it
  found that in one run. **If a value matters, expose it to a checker.**
- **Growing the grid silently re-scoped everything that sampled over it.** `place_rivers` and
  `place_buildings` both picked `cy` across the whole world, so half their attempts would have
  landed in the dream sector, thinning the overworld's rivers and villages with nothing failing.
  This is the "constants denominated in tiles do not scale" trap in a new costume — the constants
  were fine, the **sampling range** was wrong.
- **`game_init` picked the largest component across the whole grid, which after slice 1 could
  spawn the player in the dream realm.** Fixing the obvious loop was not enough: once the portal
  exists `flood_open` crosses it, so the spawn component spans both sectors and **its centroid
  lands in the void band**, putting the nearest component tile in the dream sector anyway.
  Measured at row 69 on seed 15 of 30. **Nothing else catches this** — `--land-test` measures the
  spawn component from wherever the spawn is, so a dream spawn looks perfectly healthy.
- **"Place it anywhere open" is not the same as "place it where the player can get to it."**
  `--portal-test` first reported 97/100 rather than 100/100, because the overworld end landed on a
  detached lobe on ~3% of seeds — exactly the rate decision 30 records for detached lobes. Both
  ends now sample inside their sector's **largest** component. Three anomalous seeds out of a
  hundred were a real defect, not noise; **investigate the outliers rather than accepting the
  pass.**
- **A helper that returns "I did something" merges two different somethings.** `autopilot_tick`
  returns 1 for "restored", 0 for "moved"; the portal step returned 1, so `--play-test` printed
  `restored 20/19` — more restorations than there are entities. Harmless to the run, and caught
  only because the total exceeded a bound that **cannot legitimately be exceeded.** Prefer printed
  totals that have an impossible value.
- **The camera follows the player, so no ordinary capture can show a world with two landmasses.**
  Added `--grid` (self-test only, beside `--overlay` and `--tune`) to screenshot the 12-seed view
  without a human holding F2. Reach for it whenever the question is about world *shape*.

**New in the Phase 12 slice 3 session (2026-08-06):**

- **A COMPONENT YOU CANNOT STAND IN IS STILL A COMPONENT YOU CAN REACH**, and that gap swallowed a
  bug the whole suite was blind to. The portal dropped the player into a gated region on 11 of 100
  seeds, leaving her unable to move at all. `--gating-test` could not catch it: it asserts
  walk-reachable == graph-reachable, and both agree perfectly that a gated tile is unenterable —
  they are *supposed* to. `--portal-test`'s shrink measure read 100/100 before and after, because
  it floods on `solid` and never asks about terrain. `world_solvable` only asks about entities, and
  there are none in the dream sector yet. **A property about an arrival POINT needs its own
  assertion; reachability tests are about sets and will not notice.** Found by a human on the first
  seed they played, minutes after the slice was reported done.
- **A predicate that is right for gameplay can be wrong for rendering, and quietly.** `dream_sector`
  starts at row 64; the void band is rows 60–63, so the gulf between the landmasses rendered in the
  overworld's sea blue. The fix was a *second* render-only predicate, not a widened one — see
  decision 49. **Before reusing a domain predicate for a render decision, check its boundary rows.**
- **A clock chosen for a character is the wrong clock for the world.** The plan drove the portal
  vortex from `Player.anim`, which is reset to 0 on key release so a standing player shows frame 0.
  Correct for a walk cycle; it would have frozen the portal whenever the player stood still. Any
  future ambient motion wants `Game.clock`, which is what that field now exists for.
- **A contact shadow belongs to the ground, not to the prop.** The baked props' shadow was
  grass-green, so every violet dream tree drew a ring of lawn at its base — at the one place the eye
  is already looking, because a contact shadow is what says where the trunk meets the tile. Found by
  screenshot; no test has an opinion on it.
- **Check the bake for sprites nothing calls before adding more.** `ART_BLD_PORTAL_ARCH` had been
  baked and drawn by nothing since Phase 07 — it turned out to be exactly the art this slice needed.
  Conversely the plan asked for `fx_well` and `fx_crystal` to be baked here, and nothing draws
  either until slice 5 or at all; that would have been ~16 KB of dead weight.

**New in the Phase 12 slice 4 session (2026-08-06):**

- **TWO THINGS THAT AGREE WITH EACH OTHER CAN BOTH DISAGREE WITH THE GAME.** `--gating-test` compares
  walk-reachable against graph-reachable and they matched perfectly — because *both* model the
  portal as an edge you traverse by standing on an end, while the real interact fired from 25.5 px
  away. A parity test proves two implementations agree; it says nothing about whether either one is
  what ships. **When a test compares two models, ask what the game actually does.**
- **`fake_surface` zeroes the struct, so `format` is NULL and `SDL_MapRGB` returns 0.** Anything
  drawn onto it is written as *black*, which against a cleared buffer is indistinguishable from
  nothing being drawn at all. The prompt checker reported a perfectly good keycap as blank. Use
  `SDL_CreateRGBSurfaceWithFormat` whenever the test counts *coloured* pixels rather than positions.
- **A clamp is not a check.** "The bob stays within ±2 px" is satisfied perfectly by a bob that
  never moves. Any bounded-value assertion needs a companion assertion that the value *varies*, or
  it is a checker the null implementation passes.
- **Size UI against something legible, not against the thing it sits on.** The prompt was first
  authored relative to the portal arch and came out a 10 px cap with a 4×5 px glyph — invisible.
  The bitmap font is the reference this project already has for "how big is a readable glyph here":
  10×14 logical px.

**New in the Phase 12 slice 5 session (2026-08-06), both found by looking, neither by a test:**

- **A ring search that starts too close puts a landmark inside the thing it is supposed to stand
  beside.** `near_open_tile`'s first version started at Chebyshev radius 2, and at this projection
  a 2-tile diagonal offset is about one arch-height of screen distance — so the Dream Well's sprite
  drew overlapping the portal arch's own silhouette, and a screenshot of "every Well stage" showed
  only a portal. No test caught it because none had asserted the *distance* — `--shard-test`
  correctly asserted the Well and its Soul are co-located, which stayed true throughout. **"Close
  enough to read as separate" is a screen-space judgement, not a data invariant** — the same lesson
  as sizing the prompt against the font, one slice earlier.
- **Positioning a screenshot rig ON a landmark's own tile lets the player's sprite occlude it.**
  The first version of `--shards N` stood the player exactly on the Well's ground-contact anchor —
  the same point the Well's own sprite is anchored to — so her sprite covered it completely. Both
  `--shards` and the later `--shard-at` now stand two tiles off. **A capture flag that puts you
  where you want to LOOK is not the same as one that puts the camera where you want to SEE from.**
- **A concurrent second agent, run against the same working directory with no git-level isolation,
  reverted a shared doc to stale content while trying to help.** See the Agent Log at the top of
  this file. Not this project's own trap, but worth carrying forward: if a shared status file
  reads as surprisingly out of date, check whether something else has been writing to it before
  assuming your own last edit didn't take.

**New in the Phase 11 ship-critical session (2026-08-06):**

- **A harness that never drives the real input path cannot catch a branch the restructure
  orphaned.** The E-key chain `if (!grid && try_portal) … else if (!grid) { try_restore } …
  else if (!grid && try_collect_shard)` made the shard branch dead code the moment the restore
  branch was widened to `else if (!grid)` — and every test stayed green all day, because the
  autopilot collects shards by calling `try_collect_shard` directly and `--shard-test` drove
  `entity_in_reach`/`try_restore`, never the key path. Found by a human playing, within minutes.
  **The fix's regression test drives `try_interact` — the exact function E runs — and that is the
  load-bearing change, not the one-line handler.**
- **`--grid-test` is not a flag.** Unknown self-test arguments fall through to opening a game
  window and hanging silently — twice this session, ~10 minutes each, killed by timeout. Check the
  flag exists before running it (the real world-shape flag is `--grid`).
- **A draw-once test of a fading overlay fails for the wrong reason.** The toast's 180-frame fade
  only *looks* like 180 frames because the world redraws over it every frame; a test that draws the
  HUD onto a static surface and counts later sees stale pixels. The toast-expired check now
  refills the surface between draws, mirroring the real frame loop.
- **A font table's silent range check is the worst kind of failure: total.** `draw_glyph` returned
  early on out-of-range characters, so the first HUD pass showed nothing but digits with every call
  "working". The lowercase extension and the 3,736-px expectation were both derived from the table
  (no magic numbers), so the test needed no edits — the same property decision 23 was built for.

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
- **Decisions 40–42 shipped for +0 bytes, and the full suite was re-run after them.** 755,200
  before and after: the four palette entries the bake stopped emitting paid for the new code
  almost exactly. Re-**run**, not re-argued, even though all three changes are render-only —
  fade, sprite, fog, iso, font, rng, move(20), land(30 + both controls), village(30 + both
  controls), region(30), reach(50 + control), gating(30), bridge(**200/200**), **play 50/50**.
- **The prop fade is proven in two independent halves, each with a negative control.**
  `--fade-test` checks SELECTION (an 8-case truth table over `prop_covers_player`) separately from
  the BLEND (1,808 px compared against a reference computed through `SDL_GetRGB`/`SDL_MapRGB`,
  deliberately not through the channel-mask arithmetic the blitter uses). The selection control is
  a band-blind predicate, rejected on exactly the 2 cases that carry the decision; the blend
  control catches a blitter that ignores `fade`. **Seen working on screen**, not only measured.
- **The bush's magenta halo is gone: 163 px stripped, from that sprite alone.** The count matches
  an independent measurement of the source PNG exactly, and no other sprite lost a pixel.
  `--sprite-test` now fails if any of the four disc colours reaches a baked palette, with a
  three-sided control proving it catches the halo while keeping both the conifer's dark outline
  and the bridge's mauve stone.
- **Stone no longer out-values the ground it sits in**: the ramp tops out at luminance 74 against
  sage grass at 78, where it was 89. `--fog-test`'s new control rejects the ramp that actually
  shipped, so the checker is known to catch the real fault rather than an invented one.
- **The dream realm did not weaken the completability proof.** Re-**run**, not re-argued, after
  each of slices 1 and 2: sector(100), portal(100), land(30 and 100 + both controls), village(30),
  region(30), reach(50 + control), gating(**30**), bridge(200/200), **play 50/50**.
- **`--gating-test` passes 30/30 with a graph edge that is not a tile adjacency.** This is the
  single strongest result of Phase 12 so far: walk-reachable == graph-reachable across all four
  ability tiers, with a portal in the middle. It is what `tile_neighbours` exists to guarantee.
- **The portal is load-bearing — measured.** `--portal-test` regenerates each seed twice and
  **100 of 100 seeds shrank** the player's reachable component when the portal was suppressed.
- **The player spawns in the overworld on 100 of 100 seeds**, asserted rather than assumed, after
  two separate bugs that put her in the dream realm.
- **Phase 12 slices 1–2 cost +1,536 bytes total** (755,200 → 756,736), and render went 1.065 →
  1.016 ms — no regression from a 1.7× larger grid, because the band sweep still only draws what
  is on screen.
- **Slice 3 cost +10,240 bytes total** (756,736 → 766,976, the last 512 of it the arrival fix) and
  render *improved* again, 1.016 → 0.974 ms. **The biome itself is ~800 of those bytes**: 11 dream
  props are the same pixel streams with new palettes, asserted by `--sprite-test` rather than
  assumed. Almost all the rest is the 8 portal frames.
- **The bake's recolour and `dream_shift()` agree on every entry of every dream palette**, within
  the 1 that separates PowerShell's round-half-to-even from C's round-half-away. Two
  implementations of one formula, kept in step by a checker rather than by discipline.
- **The dream value hierarchy holds, and its control has teeth.** Ground 52, stone 49, void 38; the
  control rejects a mechanically shifted sea ramp at 62, which is the version the plan specified.
- **`--play-test` CROSSES THE PORTAL on all 50 seeds**, 2-4 times each, completing 19/19. Four
  handovers carried "the portal's effect on completability is untested"; that is closed. Crossings
  are counted rather than inferred, so a change that accidentally joined the landmasses fails the
  seed instead of quietly retiring the only end-to-end exercise travel has.
- **The Kindle gate on the portal is enforced and asserted from both positions** — refused standing
  on a gated end AND standing beside it, which is the half that was broken, and working again with
  Kindle. See decision 50.
- **The prompt renders three distinguishable states and nothing at all when there is nothing to
  do**: 400 / 475 / 438 px, and exactly 0 for `PROMPT_NONE`. Its bob is clamped to +/-2 px *and*
  asserted to move, because a clamp alone is a check the null implementation passes.
- **Slice 4 cost +1,024 bytes** (766,976 -> 768,000), of which **task 9 cost +0**.
- **The portal now lands the player somewhere she can stand, on 100 of 100 seeds** — 11 of 100
  failed that before, with no test able to see it. Mean walkable area from the arrival point 739 →
  818 tiles. Control: gate the arrival region by hand, require rejection.
- **The art bake costs 62,976 shipping bytes and did not weaken anything.** Re-**run**, not
  re-argued, after the seam landed: sprite, iso, font, fog, rng, move, land (30 + both controls),
  village (30 + both controls), region (30), reach (50 + control), gating (30), bridge (200/200),
  **play 50/50**.
- **`--play-test` is 50/50 with the Well unlocked and redeemed on every seed** — not just the
  portal crossed, the *whole* slice-5 loop exercised: shards collected, threshold crossed, the
  once-locked Soul walked to and restored, by the same autopilot that plays everything else.
  `attempts 1` in `--reach-test` on all 50 seeds: the shard/split quota is met first try, not by
  falling back to a retry.
- **`--shard-test`'s boundary IS its own control**, decision 36's shape applied a third time:
  `SHARD_REQUIRED - 1` shards leaves the Well's Soul locked, `SHARD_REQUIRED` unlocks her, measured
  through `entity_in_reach`/`try_restore` — the real interact path, not by inspecting `shards_held`
  directly. A separate control starves a real world's shards by hand and requires
  `shards_sufficient` to reject it.
- **All placed shards sit in the dream sector on every seed measured** (30/30), and the Well and
  its Soul are always co-located.
- **Slice 5 cost +6,144 bytes** (768,000 → 774,144). Render went 0.974–1.089 → **1.14 ms mean** —
  reported as a real increase rather than claimed as "no regression"; still 58.9 fps against the
  60 Hz target, comfortably inside the 21.333 ms frame budget.
- **The Well's two extreme stages are visually distinct, seen on screen, deliberately positioned
  rather than found by luck.** Dormant (0 shards fed) is a small dim basin; fed (6+ shards) is a
  bright vertical burst. `--shards N` and `--shard-at N` (self-test only) exist because nothing
  else could reliably put a camera at either — both are reached by reservoir sampling, so no seed
  or vantage shows one without positioning the player directly.
- **The shard pickup regression passes through the EXACT function the E key runs**, after the
  branch was proven dead and unreachable by the whole suite: `--shard-test` plants a shard at the
  player's feet at the Well, drives `try_interact`, and asserts the pickup (return 3, held count,
  shard consumed). Full suite re-run green after the restructure; release size unchanged at
  786,432 (+0).
- **The softsynth is deterministic and inside the deadline.** Two fresh states → bit-identical
  96,000 samples; worst case 0.325 ms of the 21.333 ms deadline; peak 0.9151, NaN 0, out-of-range
  0, partial writes 0; fragment layers and the Voice of Souls wired to restore counts. Measured,
  not heard — see NOT verified.
- **The shipping exe's import table lists only OS DLLs** — kernel32, user32, gdi32, winmm,
  imm32, ole32/oleaut32, version, advapi32, setupapi, shell32, msvcrt. No SDL DLL, no dev-tool
  dependency; "runs clean without dev tools" is discharged in code terms (the physical
  second-machine run is still the human item).
- **The toast shows AND expires** — the expired check refills the surface between draws to
  simulate the world redraw, mirroring the real frame loop (decision 61).

### NOT verified — be honest about these

- **NOTHING IN THE DREAM REALM HAS BEEN SEEN IN MOTION.** The vortex is 8 frames at 8 fps driven by
  `Game.clock`; no run diffs two frames to prove it advances, and nobody has watched it turn.
- **The dream realm's fogged periphery is the overworld's cool grey haze.** `fog_lerp` is one global
  blend and knows nothing about sectors, so unrevealed dream ground reads grey rather than violet.
  That is the core mechanic working as designed — the overworld looks the same — but a violet haze
  on the dream side is a real option nobody has looked at on screen.
- **Rocks stand in the void.** `prop_at` puts boulders on any solid tile, ocean included, so the
  starfield carries scattered rocks. Over water they read as rocks in the shallows; over a void they
  read as floating debris. May be on-concept for a floating-island biome, may be a fault. Unjudged.
- **4 of 100 seeds land the player in a 6-to-30 tile pocket** on arrival. Standable — that is
  asserted — but small. Legitimate gated design (the overworld spawn works the same way).
  `--portal-test` reports the count; nothing further was needed since task 9's entity quota
  already makes the verifier reject a landing that opens onto nothing.
- **SHARD PICKUPS WERE NOT VISUALLY DISTINGUISHABLE FROM AMBIENT DECORATION.** A dozen seeds were
  swept looking for one in a static screenshot; none was identifiable against the dream forest's
  own scattered `PROP_CRYSTAL` decorations, which share the same tapering silhouette and a similar
  cyan palette family. `draw_shard`'s bob would help in motion, but a still cannot show it. **This
  is an honest, unresolved risk, not a nice-to-have**: if a human cannot tell a shard from
  decoration by eye, the collect-8-find-6 loop reduces to wandering. The mechanism (placement,
  collection, the autopilot finding all 8) is fully proven; the legibility is not. Likely fix is a
  palette or size pass, decided by looking — the same way every other art judgement here has been.
- **The Well's animation has not been watched playing, only captured as single frames.** The
  "calm → stirring → full loop" progression is implemented and its extremes are visually distinct
  in stills; whether it reads as the Well coming alive over several seconds of real play is
  unjudged.
- **The render-time increase this slice (≈1.01 → 1.14 ms mean) was not attributed to a specific
  cause.** Candidates: the Well's per-band draw, the shard loop's per-band `shard_in_reach` scan,
  16 more sprite records in the bake. None investigated individually — none threatens the 60 fps
  budget.
- **The user's verdict on the dream realm's look was "all good for now"**, which discharges the
  slice 3 gate. That is not the same as "this is the final art direction".
- **The prompt has not been seen in MOTION** — whether a ±2 px bob at 1.6 Hz reads as inviting or as
  jitter is unjudged, the same gap the portal vortex has.
- **Nobody has played a seed through the portal by hand.** The autopilot crosses on all 50 seeds; a
  human has crossed once, in the session that found the arrival bug.
- **Whether 4 fragments and 2 Souls is the right split** is a pacing question, and pacing has not
  been measured since before the grid doubled.
- **The interact prompt has not been photographed over a dream-side entity**, only over overworld
  ones. One call, one function, so this is a gap in the picture rather than in the code.
- **The portal is still not deliberately placed in a `TERRAIN_DARK` region.** Regions do not exist
  when `place_portal` runs, so its overworld end is an arbitrary tile in the largest component —
  it lands in a gated region only by chance. What changed in slice 4 is that the gate now *works*
  when it does land in one (decision 50); choosing the region is still not done.
- **Nobody has judged whether the dream sector's shape is attractive.** `--sector-test` and
  `--land-test` bound its size and connectivity; neither has an opinion on whether a ragged
  archipelago at `DREAM_ROUGH 0.78` looks good. Seen only in the 12-seed `--grid` view.
- **Whether a ghosted prop looks RIGHT in motion.** The fade is proven to fire and was seen firing
  in a capture, but nobody has walked under a canopy and watched it blend in and out. The whole
  sprite ghosts, not just the overlapping pixels — deliberate, since masking the overlap alone
  cuts a hard-edged hole in the canopy — but whether that pops distractingly as you walk is a
  judgement no test makes.
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
- **The `rock_small_01` PROP is still pale**, and it is a different thing from the terrain outcrops
  decision 42 fixed. Its colour comes from the team's baked sprite, not from `stone_ramp`, so no
  palette constant reaches it — it would need re-authoring or a bake-time recolour. Noticed while
  verifying decision 42; not in scope for it.
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
| Asset pipeline for team-authored art | **RESOLVED AND BUILT, extended through Phase 13 Slice 2.** `tools/bake.ps1` → `src/art_data.h`, committed, compiled in, nothing loaded at runtime. **93 records now baked** (82 streams + 11 dream palette variants): the original village art, portal/Well frames, and the supplied `assets/dark_fantasy` wall/building/prop subset as `AETHER_*`. Dungeon/interior/environment source remains unbaked until called. See decisions 37–39, 46–47 and [[Phase 07 - Asset Seam]] |
| **Player occlusion behind props** | **RESOLVED AND SHIPPED 2026-08-05** — props fade over the player, `--fade-test`, +0 bytes, seen on screen. Carried as the top open item by four handovers. See decision 40 |
| **The bush's magenta base disc** | **RESOLVED AND SHIPPED 2026-08-05** — 163 px stripped at bake, real contact shadow drawn instead, guarded by `--sprite-test`. See decision 41 |
| **Rock outcrops as pale floating cubes** | **RESOLVED AND SHIPPED 2026-08-05** — stone tops out at luminance 74 against grass at 78, guarded by `--fog-test`. Outcrops kept, so no generator change and no re-argued proof. See decision 42 |
| **A second biome** | **RESOLVED, SPECCED AND SHIPPED, 2026-08-05/06.** A portal in the `TERRAIN_DARK` region to a Lumiara-style dream realm — all 5 slices, all 11 tasks built, tested and committed. `--play-test` 50/50 with the whole loop (travel, shards, the Well) exercised. See [[Phase 12 - Dream Realm]] |
| **Audio — the softsynth + SFX** | **RESOLVED AND SHIPPED 2026-08-06** — 5 layers (Base/Strings/Pad/Bells/Voice of Souls), deterministic by construction, 0.325 ms worst case; chime/shard/portal SFX. Wade-splash deferred per [[Cut List]] #5. Whether it *sounds good* is a human judgement nobody has made. See decisions 57–59 |
| **HUD + on-screen text** | **RESOLVED AND SHIPPED 2026-08-06** — counters, minimap, toasts, win banner on the 91-glyph font (0x20–0x7A); the font is un-gated because it now has a real caller. See decisions 61–62 |
| **The single interact key** | **RESOLVED AND SHIPPED 2026-08-06** — `try_interact` owns portal/restore/shard pickup in one function; `--shard-test` drives it. See decision 60 |
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
- **Authored against the tile.** Diamonds are 36×18 at `TILE 18` (was 48×24 at `TILE 24`); anything hand-drawn should be
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
mix-and-match buildings, the fog rewrite, a bitmap font, a tuning overlay, a 5-layer softsynth, a
HUD, six test harnesses with negative controls — still comes to about **23 KB**, a rounding error
next to SDL2's ~664 KB.

**Art is the first thing to cost real bytes: 62,976 for the first 37 sprites**, and the bake now
carries **93 records / 186,436 const-data bytes** (portal/Well frames, dream variants, and the
supplied dark-fantasy castle wall/building/prop subset). Even so it is ~13% of the free space. So
the conclusion is unchanged in substance —
**bytes are not the constraint** — but the *shape* is now worth knowing: if anything ever threatens
the limit it will be assets, not code, and the lever is which sprites get baked, not how the game
is written.

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

**PHASE 12 IS DONE. All 11 tasks, all 5 slices, built tested and committed.** The direction change
of 2026-08-05 ran its full course in one calendar day. This supersedes the old "start at Phase 08"
instruction that several handovers carried, and also supersedes this section's own former "slice 5
is next" line.

**Phase 12 progress, slice by slice — all done:**

| Slice | Tasks | State |
|---|---|---|
| 1 | Grid growth, dream sector, `--sector-test`, `--land-test` re-aimed | **DONE** — `cd8e9d3` |
| 2 | `tile_neighbours`, portal, `--portal-test`, travel | **DONE** — `bf66217`, `4f4cf64` |
| 3 | Dream palettes (tier-1 recolour) + baked FX. It looks like Lumiara | **DONE** — `b35f25a`, `c3e4d30`. Look-gate discharged by the user 2026-08-06 |
| 4 | Prompt indicator + fragments into the dream sector | **DONE** — `50e8427`, `3d03421`. `--play-test` crosses the portal on all 50 seeds |
| 5 | Shards + the Dream Well + `--shard-test` | **DONE** — tasks 10–11. `--play-test` 50/50 with the Well unlocked and redeemed on every seed |

**Slice 5 in one paragraph:** 8 dream shards, own array, feed a fixed landmark beside the portal's
dream end. Feeding `SHARD_REQUIRED` (6 of 8) unlocks the Well's own Found Soul —
`entity_in_reach` excludes her while `shards_held < SHARD_REQUIRED`, a pure function of a count
rather than a stored flag with sync to worry about. `shards_sufficient()` sits beside
`entities_split_ok` in the same generate-then-verify loop, same reasoning: kept out of
`world_solvable`, never re-checked by the ungating fallback. Cost **+6,144 bytes** (768,000 →
774,144); render went **0.974–1.089 → 1.14 ms mean** — a real increase, reported rather than
smoothed over, still comfortably inside the 60 fps budget. Two placement bugs found by looking, not
by a test: the Well's first ring-search radius (2 tiles) put it inside the portal arch's own
silhouette, and the first screenshot rig stood the player directly on the Well's tile, her own
sprite occluding it. Both fixed; see decisions 53–54 and §7.

**What is now honestly still open, from Phase 12 itself:** shard pickups were not visually
distinguishable from ambient `PROP_CRYSTAL` decoration across a dozen seeds swept looking for one —
the mechanism is fully proven (placement, collection, the autopilot finding all of them), the
*legibility* is not, and this is the kind of thing that needs a human's eye, not another test. See
[[Phase 12 - Dream Realm]] Evidence, slice 5, for the rest.

**All of it is done through ship-critical.** Phases 08 (save/load), 09 (restoration rebuild), 10
(motion) and 11 (audio + HUD) landed on schedule after Phase 12; every phase's DoD is ticked and the
full suite is green. The only outstanding engine bug found by play was the unreachable shard pickup,
fixed same-day (`7cd408a`) with a regression that drives the real key path. What remains before
submission is the **human checklist**: merge `feat/phase-11-ship-complete` into `main`, run the
[[QA Checklist]]'s second-machine smoke test, decide the repo's visibility, tag the submission
commit, push. **Phase 13 Slice 1–2 is now implemented, and relocated same-week (2026-08-07)**: the
island is a fixed **top-right** water-locked footprint (`110,2`, `42×42`), not the original
southeast placement; the causeway (`x94..107,y56`, unchanged) opens as a side effect of restoring
the Dream Well's Soul rather than a separate mainland-key pickup — the `castle_key`-at-watchtower
item described in earlier handovers was removed. Supplied `assets/dark_fantasy` walls/buildings/
props still provide the outer courtyard composition, baked as `AETHER_*` records — trimmed the same
day to the **14 files actually referenced (78 records / 67 streams)**; a separate 108-PNG
`assets/castle/` pack was supplied and briefly baked for the relocated island but then reverted,
and sits committed but unbaked. Dungeon, interior, and deeper keep remain deferred per
`Phase 13 - Aetherhold Castle Plan.md`. The full suite plus `--aether-test` is green (re-verified
live 2026-08-07); current release is `846,848` bytes with `593,152` headroom on a `164×157` world.
See the phase file and plan for the remaining five-slice approach and the `tile_blocked`-pure
gating rule.

> **Do not re-raise the schedule.** It was put to the user on 2026-08-05 with the full arithmetic;
> they considered it and said there is time. That is their call and it has been made.

**Schedule reality, as of this handover:** today is **2026-08-07**; the contest deadline is
**2026-09-04** — development through Aetherhold Slice 2 is complete, with **593,152 bytes of headroom**. The
buffer week is real: submit early, not at the deadline. **Judging order is finished → under size →
fun.** The only remaining *fun* risk is the unjudged stuff in §8 — nothing else needs to give;
[[Cut List]] stays pre-committed but nothing is expected to be cut.
