# Wayfarer (Top-Down) — Development & Build Guidelines

## Commands
- Build Shipping: `powershell -ExecutionPolicy Bypass -File .\build.ps1`
- Build Self-Test: `powershell -ExecutionPolicy Bypass -File .\build.ps1 -SelfTest`
- Run All Self-Tests: `powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1`
- Run Single Test: `.\build\wayfarer-selftest.exe --<test-name> [--seeds N] [--seed N]`
- Look at the map screen: `--map` (grant + open it), `--standon map` (stand on the fragment)
- Look at the menu: `--menu` (open it), `--paused` (the pause form),
  `--menupage settings|controls|load|replace|confirm`
- Look at the cast: `--standon npc` (a person, standing on a soul), `--standon orb` (a memory
  mote), `--standon king` (the Dungeon's king), `--area2` / `--area3` / `--area4` for the other
  biomes
- Look at the story: `--talk N` (everybody here has had N conversations), `--memory N`,
  `--soulev N` (1-9), `--endbeat N` (jump the ending). All self-test-only.
- Look at the Dungeon: `--area4 --standon king` (the throne room), `--area4 --endbeat 11`
  (the king as the beast), `--area4 --map` (the map screen, which is clear)
- Re-bake art: `powershell -File tools\bake.ps1` — only when `assets\` changes

## Core Invariants & Rules
- Language: C99, MinGW-w64 GCC (`-std=c99 -Os -Wall -Wextra -Werror`). **Zero warnings allowed**,
  and `-Werror` is what enforces it — the rule lived here alone for several phases while an
  ungated helper warned in the shipping build and nothing failed. An uncalled `static` is a
  warning *and* dead shipped bytes — add code when it has a caller.
- Architecture: single translation unit (`src/main.c`). Keep helpers `static` and localized.
- **Collision vs Render**: `tile_blocked` inspects **only** `solid[][]`, `regions[].terrain` and
  the ability mask. Ground type, stamps, canopy, trails, sprite identity and `reveal` are
  render-only. This is what makes completability proofs a re-run, not a re-argument.
- **No world-sized statics.** `-fdata-sections` on PE/COFF puts zero-initialised statics in
  `.data`, which *ships*. Use stack locals (guarded by `wayfarer_stack_guard`) or the heap.
  Verify with `objdump -h`: large `.data` + small `.bss` means this regressed.
- Size Gate: `build\wayfarer.exe` must stay strictly under **1,440,000 bytes**.
- All fatal startup errors must report via `SDL_ShowSimpleMessageBox` **with** the Win32
  `MessageBoxA` fallback — SDL's own box returns -1 showing nothing when *video* is what failed.
- Multi-byte persistence stays hand-packed little-endian. No struct writes to disk.
- Restoration state is one `Uint32` bitmask, so total collectibles across **all areas** is ≤ 32.
  Area 1 owns bits 0–9. That ceiling is why the **map fragment is not an eleventh entity**: three
  areas already claim thirty bits. It lives in `Game.maps` (four bits, one per area) and in
  `World.map_tile`, which is `-1` once taken — so "is it still lying there" has exactly one home,
  and a load replays finding it as the delta of clearing that field. The **Dungeon holds no
  collectibles at all** — same ceiling, one step further: a fourth area of ten would not fit the
  mask, so the king is tracked by `World.king_tile` instead, derived from the area seed exactly
  the way `portal_tile` is, and never stored.
- The map screen's trails are routed by `bfs_gated`, which asks `tile_blocked` — the same function
  that stops her. A trail must never be drawn through a gate she has no ability for; something
  behind one gets **no trail**, and the legend says so.
- The map fragment lies **near** spawn, not far: `MAP_MIN_SPAWN_DIST` 3 to `MAP_MAX_SPAWN_DIST` 12
  Chebyshev tiles. It is the tool for reading an unfamiliar biome, so putting it at the end of the
  hunt put it behind the exploring it exists to help with. Both bounds live in the one droppable
  `near` clause of `map_tile_ok`; the lower bound only stops her spawning on top of it.
- **A finished area lights every region**, through `world_light_all` called from `apply_restore`.
  Ten collectibles against sixteen-odd regions meant regions holding none of them were lit by
  nothing and stayed dark for good — a fully remembered area still read as half forgotten on both
  maps. Derived from the counts, never stored, and reached by a load for free because the replay
  comes through `apply_restore` too. Restoration is render-only, so this cannot touch
  `tile_blocked` or any completability proof.
- **A gate wears its own realm's colour**: the animated green swirl (`ART_UW_PORTAL_*`) is the
  Forest's, the still violet ring (`ART_LUM_PORTAL`) is Lumiara's and the Underworld's final
  chamber. They ran the other way round on a "dress the gate as where it leads" theory, which just
  read as the two portals having been swapped.
- The portal blip is on the minimap and the map screen in **Areas 1–3**, where every
  legend row says **the way onward** — all three gates lead somewhere now. The Dungeon's
  `portal_tile` is the stairs she came down, and a violet blip over dead stairs
  would promise a second trip the game does not have — so it carries no blip on either map.
  The Dungeon map screen shows **geography and the king's gold blip only**: no counts, no trails
  (`hud.bm_closed` stays zero), and the legend is two rows (`the king`, `you are here`).
- The completion banner is keyed on **biome**, via `biome_for_area` — never on the area number.
  Keyed on the area it was a third place that had to know the order, and it was the place that had
  it backwards: finishing Lumiara congratulated her on the underworld.

## Areas and the story
- **Area 2 is Lumiara, Area 3 is the Underworld** — the reverse of the order the biomes were built
  in. `biome_for_area` and `area_for_biome` are the only two places that know this; the expression
  used to be inlined at five call sites, which is how the music could end up playing over the wrong
  biome while the world generated correctly. Area 1 is the Mainland, Area 2 the realm between,
  Area 3 the castle. Seed salts stay attached to the AREA number, not the biome.
- **Area 4 is the Dungeon below the castle**, reached through Area 3's portal once Area 3 is
  finished. It is authored rooms, not noise (`dungeon_gen`): fixed topology with seed-varying
  dressing, no entities, no abilities, fully walkable with `ABIL_NONE`. The ending moved with it —
  `try_final_chamber` on Area 3's portal became `try_king_audience` at the king, and the story-test
  proves the portal travels where the chamber used to end.
- **The Dungeon generates fully revealed** (`reveal` 255 in `dungeon_gen`), and it is the only
  world that does. Fog has two channels and the Dungeon has neither: sight alone caps at
  `SIGHT_MAX`, which is half, and the other half is restoration, which is a function of
  collectibles - and the Dungeon holds none. Every tile down there sat at level 15 of 31 for
  good, on the world, the minimap and the map screen. Set at generation because that is the one
  place a Dungeon world is built (a load regenerates through it, so it replays for free) and
  because `reveal_around` only ever RAISES a tile. Render-only, so no completability proof moves.
- **There is no door in the Dungeon.** A pair of gold doors used to be pushed at a fixed tile in
  the king's doorway, and since nothing in the Dungeon is solid because it was *drawn*, they read
  as a locked door she then walked straight through. `DUN_DOOR` went out of the bake with the
  draw - art nothing draws is shipped bytes. `--map-test` measures the doorway as "no draw-list
  entry is anchored on that tile", with the grate (drawn at a fixed tile the same way) as the
  control that the measurement can see anything at all.
- **The king turns into the beast at `END_BEAST_BEAT`**, and `dun_king_art` is the ONE answer to
  what is standing on `king_tile` - `dun_dressing` draws from it and `prompt_draw` sizes the
  keycap from it, exactly as `entity_npc_art` serves the cast. It is keyed on `SF_BEAST`, which
  the ending already sets under the line *something underneath the castle turns over*, so the
  transformation lands on the sentence that describes it and costs no new state: `SF_BEAST` is
  stored, so a game saved in that room reloads with the beast still in it. Render-only, like the
  king - neither is ever written into `solid[][]`, so a seventy-pixel beast cannot seal the room.
- **Nothing the story can derive is stored.** `castle_state` (three states, off Area 1's memory
  count), `castle_key` (Area 2 finished) and `area_complete` are FUNCTIONS. A stored castle key
  lasted exactly as long as it took `--save-test` to reject a legitimate Area 3 save whose mask and
  whose flag disagreed. Only `SF_KING` and `SF_BEAST` are stored, because nothing else implies them.
- `game_live_mask` is the ONE answer to "what is restored right now": `g->restored` carries the
  inactive areas, `ents[]` carries the active one, and anything asking about the whole game has to
  reconcile the two. It was written out by hand in two places before the story needed a third.

- **God mode (`ctrl+g`) unlocks every lock for testing**: portals run onward (1 to 2 when shut,
  2 to 3, 3 to 4 when unfinished, and the Dungeon stairs back to 1), ability gates walk as
  `ABIL_ALL` (lent for the step, never written to `p.abilities`), and the king grants his
  audience unfinished. It is NOT a change to `area_complete` and NOT a
  hand that fills `restored`: the banner, the music, the region lighting, the castle states
  and the whole dialogue gate read those, and a cheat that lied to them would congratulate
  her on an area she never walked. Because a skipped run's area is ahead of its mask - the
  live invariant `save_header_ok` is derived from - such a run **cannot be saved**: `god.cheated`
  is set at the ungated step (portal, gate tile, or audience) and refuses F5 and the save row, because writing the file would
  say `saved` now and `no save to load` later. `game_reseed` and a committed `game_load` clear
  it; nothing persists the toggle. `--save-test` proves both halves, with the toggle off as
  the negative control.

## The cast
- **The three souls of an area are held by people; the seven memories are motes.** This replaced
  "everything but the orb nearest spawn", and `entity_orb_index` went with it: the orb existed so
  the first find taught the interact key without introducing a person, and seven motes an area do
  that in all three areas instead of once per world.
- **Four lines each, one per press of E, gated on memories found.** A person will not say their
  nth line until she has found n memories in the area they stand in. That one rule is the whole
  "controlled sequence" — no quest graph, and it cannot deadlock (`--story-test` simulates every
  seed to prove it). It also makes Mira's second line *true* when she says it. Somebody not ready
  repeats their last line; silence is indistinguishable from a broken key.
- **Disappearance is not a removal.** After the fourth line `entity_npc_art` stops drawing them and
  the soul that was always underneath is a mote. No second list, no save byte beyond `story.talk[]`.
- **`story` is file-scope**, on the same terms `hud` and `menu` are, and is NOT in `Game`: `Game.w`,
  `Game.ents` and `Game.p` are all replaced at a portal, and who she has met must survive that. It
  is a named type so `game_load` can hold a copy across its scratch regeneration — a refused load
  must leave a conversation still on screen.
- **Memory text is keyed on how many she has found, not on which entity she walked into.** Placement
  is procedural; the story is not, and must not arrive shuffled. That is the whole answer to
  "collected in an unexpected order", and it needs no ordering constraint in `place_entities`.
- Historic, and still true of the art: **an NPC is a different picture of the same entity.** The exception is the orb nearest spawn, which
  stays the bare mote the game shipped with: the first thing she finds has to teach the interact
  key without also introducing a person to talk to. `entity_orb_index` is that rule, and it
  measures over **all** entities, not the unrestored ones - tracking what is *left* would promote
  the next-nearest entity the moment she took the orb and the person standing on it would vanish
  in front of her.
- An NPC is a **different picture of the same entity**, not a new kind of thing. `apply_restore`,
  `entity_in_reach`, `INTERACT_RADIUS` and the restored bitmask are all untouched, which is why
  the CAST itself costs no save byte. Only `story.talk[]` is persisted, because a conversation is
  something the player did rather than something the seed decided.
- `npc_kind_for` hashes the entity's **tile**, and never draws from the world `Rng`. A draw here
  would advance the generator between placement and whatever asks it next, changing the terrain of
  every seed in the game - every recorded screenshot and every gating proof invalidated, for a
  choice that is purely cosmetic. The tile is already a pure function of the seed and a load
  replays it, so the cast survives save/load for free.
- **The king is not cast.** He is one baked sprite (`DUN_KING`, south facing only — the eight
  facings ship one picture, not a rotation system), drawn by `dun_dressing` at `World.king_tile`
  and answered by `try_king_audience` on the same `INTERACT_RADIUS` everything uses. NPCs are
  never written into `solid[][]`, and neither is he.
- **The Citizen_F cast (Peasant, Tavern) may only stand in the Forest.** The flag lives in
  `ART_NPC_FOREST_ONLY`, which the *bake* emits from the same list that names the sheets, so the
  rule and the art it is about cannot drift. `npc_kind_for` enforces it **by construction** - a
  forest-only kind is never a candidate elsewhere, rather than being picked and then filtered.
- NPCs are **render-only**. They are never written into `solid[][]`, so `tile_blocked` cannot see
  them and a person can never seal a world. `--npc-test` asserts placement and solvability are
  unchanged; `--reach-test` and `--play-test` are what would catch it if they were not.
- `entity_npc_art` is the ONE answer to "what is standing on entity i", so the renderer, the
  interact prompt and the tests cannot disagree - the same rule that keeps `World.map_tile` the
  single home for "is the chart still lying there".
- NPC frames bake **before** the character block, because `sprite_selftest` treats everything from
  `ART_CH_IDLE_DOWN_0` onward as a character frame. That means they are checked as *decorations*
  and must be **bottom-centre** anchored. Safe only because every frame of a sheet trims to the
  same x, width and bottom row - the bake **asserts** that rather than assuming it, since a sheet
  that breathed sideways would start skating silently.

## Audio
- The callback is a **hard real-time deadline** (21.3 ms at 48 kHz / 1024 frames). Inside
  `audio_cb`: no allocation, no lock, no syscall, no unbounded loop. A fault here reaches the
  speakers directly and is a release blocker, not a glitch.
- The game thread talks to the callback through `SDL_atomic_t` **only**. Every other byte of
  `Audio` is callback-owned. Payload is written **before** the flag that points at it.
- Music is a pure function of the sample counter, so two runs from one state are bit-identical.
  Only SFX may draw from the callback's `Rng`; a music layer must never.
- `MIX_GAIN` exists so the output clamp can never *engage*. "No sample left [-1,1]" cannot detect
  a hot mix — the clamp is what keeps it in range. Check samples reaching the clamp instead.
- `SDL_InitSubSystem(SDL_INIT_AUDIO)` is separate from `SDL_Init(VIDEO)` and its failure is
  **never fatal**: a machine with no sound device must still play.

## The menu
- `--frames` **suppresses the title menu** and plays straight through. Every screenshot recipe
  here and in the README means "photograph the world", and a menu in front of that would change
  what all of them capture while they carried on passing. `--menu` is how the menu itself is
  photographed. Any new flag that forces a state must decide this question too.
- The menu **pauses** where the map screen only holds her still: `acc` is held at zero while it
  is up. The map is read while standing in the world, so fog and easing keep running under it;
  the menu is not in the world at all.
- **What rows exist lives only in `menu_build`.** Drawing, moving the selection and activating
  all index that one list. `menu_act` is pure and returns a `MA_*`; `main` performs it, because
  `main` is what owns the window, the device, the world and the running flag.
- Labels may only use characters `FONT_5X7` actually fills. `(` `)` `[` `]` `+` `*` `%` `_` `#`
  `&` `"` are all-zero rows and ship as holes — `--menu-test` checks every label against
  `font_bits`, with a control that a label containing `(` is caught.
- Escape opens the menu; it no longer ends the process. Quitting is a row you choose.
- **Every path that opens a page goes through `menu_open`**, which puts the caret on the first
  row that can actually be picked. `--menu` once set the selection by hand and photographed a
  caret resting on a dim `continue` that no real launch produces — a flag that paints over the
  thing it was pointed at is worse than no flag.
- `menu_build` and `menu_draw` are pure functions of a `MenuCtx`, so the tests drive every page
  with no window, no world and no save files.

## Saves and slots
- **Six slots**, `wayfarer1.sav` … `wayfarer6.sav`, probed by name. SDL2 has no directory-listing
  API, so a fixed set of filenames is the only way to enumerate saves without platform code.
- `save_scan` reads all six headers into a `SaveSlot[]`. Called when the menu opens and after
  anything that writes a save — **never per frame**: rows are rebuilt at 60 Hz while the menu is
  up, and six reads a frame would be a syscall storm in the render path.
- A slot is listed as usable exactly when `save_header_ok` accepts it, which is the same
  judgement `game_load` makes. A row lit by mere file existence would offer a corrupt or
  previous-version file and then fail on it, which reads as the game being broken.
- **`continue` is `save_newest`** — the largest timestamp, not the lowest slot. The list marks
  that same slot `newest`, from the same function, so the two cannot disagree.
- **New game writes its save immediately.** That keeps "used" meaning "there is a file"
  everywhere, and makes a confirmed replacement real at the moment it is confirmed instead of
  leaving the old save readable by `continue` until the player happens to press F5.
- New game takes the **lowest free slot** without asking. Only when every slot is full is the
  player asked, and then it is "replace which save" with a confirm whose caret starts on **no**.
  Nothing overwrites a save that was not named.
- A new game keeps the **session's seed** (`--seed`, default 1) rather than reseeding. A new game
  that reseeded would make `--seed` mean nothing the moment the menu was used, and every test and
  screenshot recipe is anchored to it. Worlds are still changed with `[` and `]`.
- F5 writes the slot she is playing and F9 re-reads it — a pair on one slot. A quick-load that
  jumped to whichever save was newest would be a different game arriving under one keypress.

## Settings format
- Its own file (`wayfarer.cfg`), **never** spare save bytes. Bytes 22–23 are reserved-must-be-zero,
  so spending them would force a `SAVE_VERSION` bump, and a bump rejects every save on disk — a
  volume slider is not worth deleting someone's game for. Settings must also be readable *before*
  a world exists, to size the window.
- Same discipline as the save: flat, fixed-size, versioned, hand-packed little-endian, validated
  into a local and copied out only once every field passes. A missing file is a first run, not an
  error. `--menu-test` asserts every field's illegal values are refused with the live settings
  left untouched.
- The file is written only if something actually changed this session (leaving the settings page,
  or on exit for an F11 taken during play). A first run that never touches settings must not drop
  a `wayfarer.cfg` — it would record whatever scale `pick_scale` happened to choose on whatever
  display was attached, locking in a window size nobody asked for.
- Volumes are in `[0,1]` and may only **attenuate**. They multiply the mix before `MIX_GAIN`, so
  0.93 stays the worst case and the clamp stays unreachable without re-deriving anything. Full
  volume is bit-identical to what shipped before there were volumes.
- `SDL_zero(Audio)` leaves both volume atomics at **0, which is silence**. Every construction
  that will render must set them — `main` via `cfg_apply_audio` before the device opens,
  `audio_selftest` to `VOL_MAX` because the clamp bound is a claim about the worst case.

## Save format
- Loading **regenerates from the seed and replays deltas**. Never add a second construction path.
- Validate everything *before* touching the live game: regenerate into scratch, check the position
  against the regenerated `solid` map, commit only then. A failed load must leave the game
  bit-for-bit unchanged, and `--save-test` asserts exactly that for every control.
- Use `!(x >= 0)` rather than `(x < 0)` on floats from disk — it also rejects NaN.
- Byte 20 (abilities) is redundant with the restored mask, deliberately: it is a checksum on it.
- Byte 21 is the map-fragment mask (three bits) as of `SAVE_VERSION` 2; bytes 22–23 are still
  reserved and must be zero. **Bytes 36–44 are the nine conversation counts and byte 45 the story
  flags, at `SAVE_VERSION` 4** (`SAVE_SIZE` 48); 46–47 are the new reserved-must-be-zero pair. Nine
  plain bytes rather than the 27 bits they pack into — the file has never been tight, and a bitfield
  would be the one part of it unreadable in a hex dump. The bump rejects every v3 save on disk,
  deliberately: a v3 file has no record of who she has spoken to, and there is no honest value to
  invent for it. Like the restored mask, it cannot carry bits for an area past the one
  the file says she is in. The Dungeon added a fourth maps bit and a fourth area byte **without**
  a version bump: the layout is unchanged (30-bit mask, nine talks, one flags byte), only
  `save_header_ok` widened — Area 4 requires Areas 1+2 complete and Area 3 finished, and
  `SF_KING`/`SF_BEAST` are allowed in Areas 3 *or* 4 — so every v4 save still loads.
- Bytes 24–27 are the restored mask. **Bytes 28–35 are the save's timestamp** (`time()`), added at
  `SAVE_VERSION` 3 so `continue` can mean the most recent save — nothing else in the file can order
  two saves against each other. It is only ever compared, never displayed. A **zero** timestamp is
  rejected: it is what a failed `time()` writes, and it would sort as older than every other save
  forever, so `continue` could never reach it however recently it was made.
- v3 saves are **rejected, not migrated**, the same as v1 and v2 — one construction path from a file to a
  game, and a migration would be a second.

## Art pipeline
- `src/art_data.h` is **generated** by `tools/bake.ps1` and committed. Never edit by hand.
- One **global** 65-entry palette; index 0 is transparent in every sprite.
- Fog is applied through a startup-built `fogpal[32][66]` LUT, not per draw call.
- Anchors: decorations use bottom-centre of the trimmed box. **Character sheet frames use a
  cell-relative anchor** (cell centre x, one row below the lowest foot row) — bottom-centre of
  a per-frame trim would make the walk cycle skate. Changing a convention means re-baking.
- Bake only what a caller in `main.c` actually draws.
- The palette caps at **254**. NPC colours within `NPC_SNAP_D2` (100, squared RGB) of an existing
  entry are snapped onto it: without that the NPC art alone adds 57 colours to a 202-entry palette
  and **the bake fails**. It snaps only toward colours that already exist and never merges two NPC
  colours together.
- **`tools/bake.py` is what generates the committed header**, not `bake.ps1`. They read different
  Underworld packs (`Underworld2/Tiled_files` vs `Underworld/PNG`) and `bake.ps1` is stale - baking
  with it changes the Underworld rubble tiles and fails `--tile-test`. `bake.py` needs Pillow.
- **`tools/bake_dungeon_fix.py` is the second incremental baker**, and it EDITS where
  `bake_dungeon.py` appends (which is why that one refuses to run on a header that already holds
  `DUN_` sprites). It drops `DUN_DOOR`, resamples the torch frames to 65%, and splices the eight
  `DUN_MONSTER_*` frames in after `DUN_KING`. It imports `bake_dungeon` for the parser and all
  four section emitters, so the two cannot drift. Two rules make it safe to re-run the header
  through: it adds **no palette colour** (the palette stands at 253 of 254, so there is nothing
  to spend - the torch is resampled in INDEX space, making it a strict subset of the pixels that
  shipped, and the monster's 17 colours are snapped onto entries that exist), and it rebuilds
  `ART_DATA` from the retained records and then DECODES every sprite out of the new blob to
  compare against its decode from the old one. A wrong offset is caught there, not by a
  screenshot. It refuses to run twice.
- **Dungeon art bakes incrementally via `tools/bake_dungeon.py`**, because the Lumiara sources
  `bake.py` names are not all in the repo and a full re-bake would drop shipped Lumiara art. It
  parses the committed header (refusing on format drift), bakes `assets/Dungeon/` against its
  palette with the same snap/quantize discipline, and splices the 27 sprites in before the
  character block. It refuses to run twice; to re-bake, restore `src/art_data.h` from git first.

## Conventions
- `--story-test` covers the cast, the dialogue gate, the castle states, the nine soul events, the
  ending and the save round-trip, with seven negative controls. Its first section sweeps **every
  string the story can draw** against `font_bits` — the end card shipped four per-cent signs as
  holes because that array was a local the sweep could not reach, and a screenshot found it after
  the test had passed.
- When adding or modifying self-tests, **always include a negative control** — a deliberately
  broken case the check must reject. A checker that has never rejected anything proves nothing.
- Look at the screen. Every visual bug of consequence in this project's predecessor was found by
  a screenshot, never by a passing test — and so were three in phase 8 alone (ponds invisible on
  the minimap, a mix pinned against the clamp, unearned abilities lost in the terrain). Use
  `--lit` and `--dev` to reach states a fresh `--frames` run never shows.
- Claims that cannot be verified (how audio *sounds*, how motion *feels*) are recorded as
  unverified rather than asserted.
