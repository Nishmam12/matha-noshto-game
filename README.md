# Wayfarer (Top-Down)

An exploration and memory-restoration game that fits on a floppy disk — rebuilt with a top-down
camera and authored pixel art.

Written in C99 against a custom, stripped-down static build of SDL2 — no engine, no runtime, no
shipped asset files, no shipped DLLs. Every pixel that reaches the screen is decoded from data
compiled into the executable.

| | |
|---|---|
| **Release build** | **741,376 bytes** |
| Ship target | 1,440,000 bytes |
| Hard limit (floppy standard) | 1,474,560 bytes |
| **Headroom** | **698,624 bytes** |
| Test suite | 17/17 green via `.\tools\run-tests.ps1` |
| Progress | Phases 0–8 of 9 complete |

> "1.44 MB" has three definitions in common use. We build against the smallest (1,474,560 bytes)
> and gate the build at 1,440,000.

The predecessor — a complete isometric version of the same game — is kept at
[`isometric-old/`](isometric-old/) for reference. It is never built and never linked; its
*mechanics* are being ported, its projection and art are not.

---

## What this is

The loop: explore fog-shrouded forest → find a memory fragment or Found Soul → restore it → that
region's colour permanently returns and a synth layer joins the mix → sometimes an ability comes
back that opens terrain you couldn't cross before → explore further.

Four pillars: **Explore** (fog-of-war reveal over procedural terrain), **Restore** (fragments and
Found Souls permanently return colour and sound to a region), **Awaken** (restoring some fragments
grants an ability — Wade, Climb, Kindle — that opens previously inaccessible terrain), **Remember**
(a fully restored world).

Area 1 is the **Fantasy Forest**, and is the first chunk of a larger world: every mechanic is
present, and its exit is gated toward Area 2 rather than ending the game.

## Quick start

Requires Windows and PowerShell. The toolchain is **not** committed — it is fetched, and `$TOOLS`
can live anywhere. `build.ps1` and `build-sdl2.ps1` default it to `G:\tools`; set
`$env:WAYFARER_TOOLS` to override. Full fetch instructions for w64devkit, CMake 3.x (pinned — 4.x
cannot configure SDL2 2.32), Ninja and the SDL2 source are in
[`isometric-old/README.md`](isometric-old/README.md#quick-start); the toolchain is shared verbatim.

```powershell
.\build-sdl2.ps1        # once, ~4 minutes: the cut-down static SDL2
.\build.ps1             # the game
```

| Command | Result |
|---|---|
| `.\build.ps1` | Shipping build → `build\wayfarer.exe` |
| `.\build.ps1 -Map` | Also emits `build\wayfarer.map` for size forensics |
| `.\build.ps1 -SelfTest` | **Separate** `build\wayfarer-selftest.exe` with the harness + console |
| `.\tools\run-tests.ps1` | Build what is stale, run every test, then the size assertion |
| `.\tools\bake.ps1` | Regenerate `src\art_data.h` from `assets\` — only when art changes |
| `.\tools\av-exclusion.ps1` | One-off, elevated: stop Defender quarantining the build (see below) |

`build.ps1` prints the exact byte size, the delta since the last build, and remaining headroom, and
**exits non-zero over budget** (2 = over the hard limit, 3 = over the ship target), so the size
limit is enforced by the build rather than by remembering to check.

`run-tests.ps1`'s **exit code is the number of failing tests**; `99` means the build failed so
nothing ran. That makes the green claim above checkable against the working tree.

#### If the build keeps disappearing

Windows Defender detects a freshly linked `build\wayfarer.exe` as
`Trojan:Win32/Wacatac.B!ml` and quarantines it a minute or two later. The `!ml` suffix is a
machine-learning verdict rather than a signature match, and it is a false positive — measured,
not assumed: a statically linked SDL2 hello-world (669,184 bytes) is clean, so is the same
binary plus the entire 138 KB art blob (807,424 bytes), and only the game itself (877,056
bytes) trips it. Adding a `VERSIONINFO` resource, keeping the symbol table, and dropping
`--dynamicbase`/`--nxcompat`/`--high-entropy-va` were all tried and all still detected, so
there is no build flag to reach for — and un-stripping puts the binary at 1,459,442 bytes,
over the ship target anyway.

It does not present as an antivirus warning. It presents as `ld.exe: cannot open output file
...: Permission denied` when the scanner holds the file mid-link, or as twenty green suites
followed by `Get-Item : Cannot find path ...\build\wayfarer.exe` — which reads like a broken
harness. `build.ps1` and `run-tests.ps1` now both check for the missing output and name the
antivirus instead. Run `.\tools\av-exclusion.ps1` once from an elevated shell to exclude the
two build outputs by full path; `-Report` prints the hashes and engine versions Microsoft's
false-positive form asks for, which is the fix that also helps players.

### Playing it

```powershell
.\build\wayfarer.exe --seed 3
```

| | |
|---|---|
| `WASD` / arrows | walk |
| `E` / `Space` | restore the fragment or Soul in reach |
| `F5` / `F9` | save / reload the slot being played (written beside the working directory) |
| `N` / `P` or `[` / `]` | step to the next / previous world (a new seed) |
| `F11` | fullscreen |
| `Ctrl`+`G` | god mode: the portal opens on an unfinished area |
| `Esc` | the menu |

God mode suspends one rule and no others: the gate still has to be reached and pressed, but
it no longer asks whether the area is finished. Nothing is granted - the restored mask, the
banner, the region lighting and the dialogue gate all still describe what she actually did.
The price is that a run which has skipped a portal cannot be saved: its area would be ahead
of its mask, which is exactly what the loader refuses, so `F5` says so instead of writing a
file that would read back as corrupt. Starting or loading a game clears it.

`--seed N` picks the world, `--scale N` the window size, `--mute` skips audio entirely. The
self-test binary adds `--dev` (all abilities), `--lit` (reveal the whole map — the only way to
*look at* the minimap and the restored palette in a fresh run), `--shot FILE` with `--frames N`,
and `--atlas` / `Tab` for the baked-art inspector.

Three more exist purely so a `--frames` run can be *pointed at* something:
`--standon pond|edge|map|orb|ent` stands her beside the nearest pond bank, the map's border ring,
the map fragment, the one loose orb, or a collectible, and
`--leanx N` / `--leany N` hold a walk direction with no hands on the keyboard. The refusal toasts
only exist *while* she is pushing into a wall, so without these a screenshot run — which never
presses a key, and which spawns her in the middle of the largest clearing — could never
photograph one.

> **Compiler version moves the byte count.** Any size figure here is only comparable against
> another build from the same toolchain (currently GCC 16.1.0).

## Current status

**Phase 0 — presentation skeleton.** Complete and verified. 480×270 logical framebuffer,
integer nearest-neighbour upscale (×4 → 1920×1080 exactly), 60 Hz fixed-timestep accumulator with a
separate hand-rolled frame cap, and all four fatal startup paths reporting through
`SDL_ShowSimpleMessageBox` with a Win32 `MessageBoxA` fallback. Verified by screenshot: complete
1 px border (nothing cropped), correct 30 × 16.875 tile grid, 55.9 fps measured over 120 frames.

**Phase 1 — art pipeline.** Complete and verified.

- `tools\bake.ps1` turns `assets\` into `src\art_data.h`: **190 sprites, 48,246 bytes** of const
  data — 92 tiles, 34 decorations, 64 character frames.
- **One global 65-colour palette** (195 bytes). The entire delivered art set is 65 colours, so
  per-sprite palettes would be pure overhead.
- **Fog is a lookup table, not arithmetic.** Because the palette is fixed, the fogged colour is a
  pure function of (index, reveal level), so `fogpal[32][66]` is built once at startup — 8,448
  bytes of runtime RAM, **zero** shipped bytes — and every blit is an index read with no per-draw
  fog work at all.
- RLE over palette indices, 1.60× against raw. Measured: a per-sprite "raw if smaller" fallback
  would save **1 byte** across all 190 records, so there isn't one.
- Verified on screen as well as by test: the tileset reproduces its authored 8×15 layout, every
  decoration sits on its ground line, all 64 character frames keep their feet pinned, and the fog
  ramp preserves shape and shading at reveal 0 while resolving smoothly to full colour.

**Phase 2 — tile renderer.** Complete. Three passes over a clamped rectangular tile range:
ground, then blob-autotiled edge overlays (grass over dirt, olive over grass, rock rings, pond
rims), with the camera clamped to the world. Ground coverage is asserted exactly — every visible
pixel written once, at tile-aligned and non-aligned camera offsets alike.

**Phase 3 — sprite pass.** Complete. Props, collectibles and the player go into one list keyed on
ground-contact y and sorted, so a prop level with the player resolves correctly within a tile row —
which a tile-ordered walk cannot do. Anything sorting in front of the player and overlapping her
draws at half weight, so she is never lost behind a trunk. Four-facing character with separate idle
and walk cycles.

**Phase 4 — generator.** Complete, tuned against the mockups. Two-octave noise for ground type
(the fine octave is what stops pond banks quantising into dead-straight multi-tile runs), a
low-frequency canopy density field so trees gather into stands with clearings, and walked — not
thresholded — dirt trails, because the mockups show connected winding paths and no threshold
produces those.

**Phase 5 — collision and the region graph.** Complete. Swept-AABB movement in sub-pixel steps,
resolved per axis so walls slide; the world partitioned by farthest-point sampling on path
distance; ability gates assigned by depth, never on the spawn or its neighbours. `--gating-test`
proves an actual walk and the region graph agree at all four ability tiers.

**Phase 6 — the loop.** Complete. 7 fragments and 3 Found Souls, the three abilities, fog-of-war
reveal, and permanent region restoration. Every generated world is proven finishable before it
ships, and 20/20 seeds complete a headless playthrough driving the real simulation.

**Phase 7 — audio.** Complete. A five-layer deterministic softsynth over a 16-step C-minor
progression, layers unlocking at 1 / 3 / 5 fragments and the Voice of Souls at the first Soul, so
the music is a progress bar you cannot look away from. Three SFX: a chime for a fragment, a lower
ring for a Soul, and a dull thud when she walks into an ability gate she has no key for.

The whole engine runs on SDL's real-time audio thread against a 21.3 ms deadline and touches it
with **no allocation, no lock, no syscall and no unbounded loop**. The game thread's only channel
to the callback is `SDL_atomic_t` counters; every other byte of `Audio` is callback-owned. Where a
payload accompanies a flag, the payload is written first. `--audio-test --layers` measures the
worst case at **0.44 ms of a 21.3 ms deadline (2.1%)** while firing all three SFX every 40 ms.

Audio init is a **separate** `SDL_InitSubSystem(SDL_INIT_AUDIO)` and its failure is not fatal: a
machine with no sound device plays the game silently rather than refusing to start. `--mute` skips
it entirely.

> **Recorded as unverified**: how any of this actually *sounds*. What is measured is that it is
> deterministic, in range, off the clamp, and inside its deadline.

**Phase 8 — HUD, minimap, save and load.** Complete. A 5×7 bit-packed font (no SDL_ttf, ever),
two counters, the three ability names with unearned ones dimmed, fading toasts on every restore
and every refused gate, a completion banner, the seed, and a cached minimap at one pixel per two
world tiles. `F5` saves, `F9` loads.

The save is 36 bytes: a seed plus the deltas play has made on it, and the moment it was written.
Loading **regenerates** the world from the seed and replays the deltas — there is no second
construction path that could drift from what generation produces. It is the first place outside
input reaches this program, and every malformed file is rejected *before* the live game is
touched: the world is regenerated into scratch, the position checked against the regenerated solid
map, and only then committed.

**Phase 9 — the menu, settings and six save slots.** Complete. A title screen that is also the
pause screen, with `continue`, `load game`, `new game`, `settings` and `quit`; music and sound
volumes, fullscreen and window scale, persisted to their own `wayfarer.cfg`; and six save slots in
`wayfarer1.sav` … `wayfarer6.sav`, listed with the world and the progress each one holds.
`continue` takes the most recently written of them, which is what the timestamp is for. A new game
takes the lowest free slot without asking; only when all six are full is the player asked which to
replace, and that question opens on **no**.

**Not yet built**: the Area 2 gate, ability-gate visuals, the cluster reveal wave, `--trail-test`
and `--reveal-test`, and the final polish pass (phase 9).

### Tests

| Test | Checks | Negative control |
|---|---|---|
| `--sprite-test` | RLE round-trip vs an independent encoder; all 190 records decode to exactly `w*h`; both anchor conventions | overlong run, lying width, overlong slice |
| `--decode-test` | the **shipping** decoder writes nothing outside a record's declared box | the validator must reject all 4 malformed streams; a valid sprite must still draw |
| `--fog-test` | LUT matches `fog_lerp` at all 32 levels; 65,888 separable colour pairs keep their luminance order | a hue-rotating blend inverts 134 pairs |
| `--autotile-test` | blob-slice truth table; all 9 slices reachable; every tile table names a real 16×16 tile | an axis-blind slicer misses 7 of 10 cases |
| `--tile-test` | every base-fill tile is **fully opaque**; ground pass covers every pixel at aligned and unaligned camera offsets | a one-tile-short range must leave a gap; a detail tile must measure as non-opaque |
| `--sort-test` | draw order and stability over 200 duplicate keys; the list never overflows across 5,760 camera positions | an order-blind cover predicate misses 3 of 9 cases |
| `--mockup-test` | rendered-pixel census over 54 frames against bands measured from the two mockups | a flat all-grass world misses 6 of 9 bands |
| `--font-test` | all 67 glyphs a HUD string can contain are non-blank; rendering lights exactly the pixel count the table declares; out-of-range characters draw nothing | a wrong glyph stride renders 157 px instead of 167 |
| `--hud-test` | toast and banner lifetimes in ticks; the banner fires once; the counters reach the screen; water and rock are distinguishable on the minimap; every HUD string fits 480 px | the naive top-left minimap sampler draws the wall colour where the rule draws the open one |
| `--save-test` | round trip is bit-identical to a snapshot; two loads of one file agree | **10** rejection controls — truncated, bad magic, wrong version, wrong area, nonzero reserved, illegal ability bits, abilities the restored mask does not account for, NaN position, missing file — each must be rejected *and* leave the game bit-for-bit untouched |
| `--audio-test` | callback deadline, partial writes, NaN, out of range, and that the output clamp never *engages*; the layer schedule only ever accumulates | two fresh synths must render identically, **and** a missing layer or a one-sample offset must not |
| `--move-test` | movement determinism; never ends inside a wall; the foot box lies in one tile at a tile centre | a teleporting solver crosses a 1-tile wall the sweeping one stops at |
| `--gating-test` | an actual walk equals the region graph at all 4 ability tiers; every region internally connected | a gate-blind graph walker disagrees with the real walk |
| `--reach-test` | every generated world is solvable; every entity placed on an open tile | a sealed world and a self-locked gate must both be rejected |
| `--play-test` | 20/20 headless playthroughs to completion, driving the real simulation | — (completion is itself the assertion) |

The fog threshold is **measured, not chosen**: across all levels the worst luminance gap that
inverts is 0.748, and nothing at 0.8 or above inverts, so the bar is one full 8-bit step. That
worst gap is printed on every run, so a future palette change moves a visible number before the
test flips red.

**`build.ps1` now passes `-Werror`.** "Zero warnings" had been a rule written down and enforced by
nobody: a self-test-only helper produced a `-Wunused-function` warning in the *shipping* build for
several phases without failing anything. An uncalled `static` is a warning and dead shipped bytes,
so it is worth an error.

Three findings on this pass came from **looking at the screen**, not from a test:

| Symptom on screen | Cause | Now caught by |
|---|---|---|
| The minimap showed no ponds at all | water *is* `solid`, so testing `solid` first made the water branch unreachable and every pond drew as rock | `--hud-test` asserts the two colours differ |
| Full-layer output peaked at exactly 1.0000 | the mix summed to 1.69 at maximum and lived against the output clamp — distortion the "no sample left [-1,1]" check can never see, because the clamp is what keeps it in range | `--audio-test --layers` counts samples reaching the clamp; `MIX_GAIN` caps the theoretical mix at 0.93 |
| Unearned ability names vanished into fogged terrain | the dim colour was too close to the ground | judged by eye; no test |

## Repository layout

```
src/main.c        the whole game (single translation unit, deliberately)
src/art_data.h    GENERATED by tools/bake.ps1 from assets/. Never edit by hand.
build.ps1         game build + size measurement + budget gate
build-sdl2.ps1    builds the cut-down static SDL2
tools/bake.ps1    PNG -> compiled-in sprite header
tools/run-tests.ps1
tools/av-exclusion.ps1  Defender false-positive workaround + FP report
assets/           source art (bake inputs only — nothing here ships)
isometric-old/    the predecessor, for reference only
```

## Architecture

**Everything is one translation unit on purpose** — the whole-program optimizer sees everything at
once, and the self-test harness lives in the same file, gated out of shipping builds by
`WAYFARER_SELFTEST`. The shipping binary contains **none** of the test code: it is a different
binary, not a runtime flag.

**The invariant that will thread through every system below**: collision (`tile_blocked`) reads
**only** `solid` and `regions[].terrain`. Ground type, stamps, canopy, trails, sprite identity and
`reveal` are render-only. This is what makes every completability proof a *re-run* rather than a
*re-argument* after a generation change.

### Presentation

480×270 → integer upscale → `SDL_UpdateWindowSurface`. There is no `SDL_Renderer` and no texture
anywhere: SDL's render subsystem is compiled out of our SDL2 build, which has two consequences
worth knowing before touching the main loop — there is no vsync to lean on, so the frame cap is
load-bearing; and every primitive writes `Uint32` words into `surface->pixels` directly.

### Asset pipeline

Team-authored PNGs are never shipped and never decoded at runtime. Two anchor conventions, on
purpose:

- **Decorations** use bottom-centre of the trimmed box — the ground-contact point.
- **Character sheet frames use a cell-relative anchor** (cell centre x, one row below the lowest
  foot row in any frame). Bottom-centre of a per-frame trim would be wrong: the frames' trimmed
  heights vary 22–26 px and their tops vary too, so a per-frame anchor makes the walk cycle skate.
  `--sprite-test` asserts this positively — under cell-relative anchoring `anchor_y - h` varies
  across frames (it takes 3 distinct values), where a bottom-centre anchor would make it identically
  zero.
- **Tiles** stay 16×16, anchored top-left, deliberately untrimmed so their grid position keeps
  meaning.

The decoration list is hand-curated (auto-segmentation cannot name things, and it mis-groups two
cases on this sheet) but **self-checking**: no two rects may overlap, and every opaque pixel on the
sheet must fall inside exactly one rect. Both halves of that have been confirmed to fire against a
shrunk rect and a moved one.

Two findings from this phase are worth carrying: the block at tileset cols 3–4 **reads as six more
grass fill variants and is not** — each carries 6–14 transparent pixels, so using them as a base
left ~1% of the screen unpainted; and the player's collision box must be **centred** on her ground
point, not hung above it, or she intrudes into the tile row above and freezes against tiles the
pathfinder correctly calls open (which stalled 20 of 20 playthroughs at 0 collectibles).

`ART_TILE_AT[15][8]` maps a tileset grid position to a sprite id. It exists because 28 of the 120
cells are empty and the enum skips them, so index ≠ `row*8+col` — deriving the cell from the index
compacted the sheet upward by two rows, which only the screen caught.

## Engineering process

Work proceeds in verifiable loops: **plan → implement → build → measure → verify → report.** A
feature is not done until it has been compiled, run, and checked against a stated expectation.

- Every self-test carries a negative control: a deliberately broken case it must reject. A checker
  that has never rejected anything is assumed to prove nothing until it has one.
- Render-only data never leaks into collision.
- **Look at the screen.** Every visual bug of consequence in this project's predecessor was found
  by a screenshot, never by a passing test — a checker can prove a value is in range, not that it
  looks right.
- Claims that cannot be verified (how audio *sounds*, how motion *feels*) are recorded as
  unverified rather than asserted.

## Third-party

[SDL2](https://github.com/libsdl-org/SDL) 2.32.10, zlib licence, statically linked, rebuilt from
source with unused subsystems (renderer, image/TTF/mixer loaders, controller mapping database, most
render backends) stripped out — the official prebuilt `libSDL2.a` alone cost 1,656,876 bytes, over
budget before a single line of game code. Our rebuild contributes 669,696 bytes.

Built by [w64devkit](https://github.com/skeeto/w64devkit) (MinGW-w64, GCC 16.1.0).
