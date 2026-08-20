---
tags: [design, phase, wayfarer]
phase: 8
status: done
updated: 2026-08-06
---

# Phase 08 — Save Load

**Status:** DONE — `feat/phase-08-save-load`, 2026-08-06, +1,536 bytes.
**Depends on:** Nothing in this phase list — it's independent of the art/traversal work and is
sequenced here mainly because it's cheap, high-value, and was originally Week 5 scope that's worth
pulling forward while the team is actively iterating in parallel.
**Blocks:** Nothing downstream, but is genuinely useful to have before [[Phase 09 - Placeholder
Art]] and [[Phase 10 - Motion]], since it lets a specific world state (a bug, an interesting layout,
a good-looking seed) be captured and handed to a teammate instead of described in words.

## Why this phase

The world is almost entirely a pure function of its seed — region layout, buildings, entities are
all deterministic given the seed and the generation code. That means "save the game" is unusually
cheap here: the save file doesn't need to store the world, only the seed plus what's changed since
generation (player position, which regions are restored, which abilities are held). This is also the
first concrete, practical benefit of the backbone/team framing: a save file is a reproducible way for
one person to say "look at this" to another, which this project has had no way to do until now short
of a screenshot and a seed number typed into chat.

## Definition of done

- [x] A save captures: world seed, player position, the restored-region bitmask (or per-region
      restoration floats, if partial restoration state matters to preserve — decide based on whether
      `Region.restoration` mid-ease is worth restoring exactly or whether snapping to `restore_to` on
      load is acceptable), and ability flags.
- [x] Loading a save regenerates the world from the seed (reusing existing generation) and then
      applies the saved deltas — not a from-scratch alternate load path that could drift from what
      generation actually produces.
- [x] The save format is versioned (a header byte or magic+version field) so a future format change
      fails loudly on load rather than silently loading garbage.
- [x] Save/load uses `SDL_RWops`, verified to actually link given `SDL_FILESYSTEM` is OFF in this
      project's cut-down SDL2 build (`build-sdl2.ps1`) — this is a real open question, not an
      assumption, and needs checking before the rest of the phase is built on top of it.
- [x] `--save-test` exists: save, mutate live state, load, assert the resulting state is bit-identical
      to the saved state (not just "close enough" — this project's standing rule against relative
      assertions applies here too).
- [x] `--save-test` has negative controls: a truncated file and a wrong-version file must both be
      rejected cleanly (no crash, no silent partial load), not just "happen to fail."

## Concrete tasks

1. **Resolve the `SDL_FILESYSTEM`/`SDL_RWFromFile` question first**, before writing anything else in
   this phase — check `build-sdl2.ps1`'s CMake flags (documented as OFF for `SDL_FILESYSTEM` in
   [[Toolchain Setup]]) against whether `SDL_RWFromFile` still links in the cut-down build. If it
   doesn't, the fallback is stdio directly (`fopen`/`fread`/`fwrite`), which is a smaller change than
   it sounds like given this project already avoids SDL abstractions it doesn't need
   (no `SDL_Renderer`, direct pixel writes) — but confirm rather than assume either way.
2. Define the save struct: seed (`Uint64`), player x/y (`float`×2), `restored`/ability bitmask, and
   whatever `Region.restoration` decision comes out of the Definition of Done. Keep it a flat,
   fixed-size struct for a first version — this project's whole design philosophy (see [[Handover]]
   §6's preference for simple, provably-correct primitives over flexible-but-unverified ones) argues
   against a self-describing or variable-length format for a first pass.
3. Add a version byte/magic number at the start of the file. Reject anything that doesn't match on
   load, with a clear "no save" fallback rather than a crash.
4. Wire save/load to specific keys (not yet assigned — check what's free; `F1`/`F2`/`F11`/`R`/`E`/
   `Space`/`WASD`/arrows are taken) and to the game loop in `main` (`src/main.c:4286`).
5. Write `--save-test` per the Definition of Done, including both negative controls, following this
   project's established pattern (`village_negative_test`, `solvable_negative_test`) for how a
   negative control should be structured — deliberately construct or corrupt a file and confirm
   rejection is clean.

## Verification gate

`--save-test` PASS including both negative controls (truncated file, wrong version). Manual
confirmation that a saved-and-reloaded game is indistinguishable from the original in play (not just
in the raw struct comparison the automated test does) — save mid-playthrough, reload, confirm
restoration state and position match what was expected.

## Traps specific to this phase

- **`SDL_FILESYSTEM` being OFF is a real risk to this phase's basic premise, not a footnote.** This
  is called out as task 1 specifically because if `SDL_RWFromFile` doesn't link, the whole approach
  needs to shift to stdio before any other work in this phase is worth doing — check it first,
  don't discover it after building the save struct.
- **A save file is the first place in this project where malformed external input reaches the
  program** (every other input — the seed, RNG streams, generation parameters — originates inside
  the program itself). Treat the load path with the scrutiny that implies: a corrupted or truncated
  file must not crash, overrun a buffer, or silently produce an inconsistent `Game` state. This is
  exactly the kind of boundary [[Handover]]'s general engineering principles call out for validation,
  where internal code elsewhere in this project deliberately doesn't bother.
- **Where does the save file live?** "Next to the exe" is the obvious answer but interacts with
  Windows' tendency to install executables into locations a standard user can't write to. Since this
  project ships as a standalone `.exe` with no installer, this is likely fine for a contest entry run
  from a folder the user extracted themselves — but don't assume it silently; note the assumption
  explicitly if it's made.

## Evidence

Committed on `feat/phase-08-save-load` (from `098d232`), 2026-08-06. Release **775,680 bytes**,
+1,536 over this branch's base (774,144).

- **The gate question, answered empirically:** `-DSDL_FILESYSTEM=OFF` at `build-sdl2.ps1:77` does
  NOT remove `SDL_RWFromFile` — it lives in `SDL_rwops.c`, which the cut-down build keeps. A
  standalone probe linked against `G:\tools\SDL2-min\lib` called `SDL_RWFromFile`/`SDL_RWwrite`/
  `SDL_RWclose` successfully before any save code was written.
- **Format:** 28 bytes, flat, little-endian by hand (no struct dump): magic `WF`, version 1,
  reserved byte, seed u64, player x/y f32×2, abilities u8, shard mask u8, reserved u16, restored
  mask u32. `Region.restoration` decision: mid-ease values are animation, not progress — load
  snaps `restoration = restore_to`. The per-tile fog `reveal` (45 KB of float) is rebuilt on load
  as one instant of standing at the saved position, the same taper `reveal_around` converges to.
- **Load is validation-first:** the file is fully parsed and checked (magic, version, reserved
  bytes, mask bounds, finite/in-bounds position) before the live game is touched; the world is then
  regenerated into a heap scratch `Game`, the saved position is checked against the regenerated
  solid map via `player_blocked`, and only then copied over the live game. A corrupt file cannot
  leave a half-loaded state.
- **`apply_restore`** was split out of `try_restore` so load replays the exact same transitions
  play uses — one source of truth for what "restored" means.
- **Keys:** F5 save, F9 load; feedback in the title bar (`[saved]`/`[loaded]`/`[no save]`) until
  the next keypress. The save file is `wayfarer.sav` in the working directory — the assumption the
  trap above asked to be stated: fine for a contest entry run from its own folder, not for
  Program Files.
- **Verification:** `--save-test` PASS at 10/10 seeds (1, 2, 13, 777, 2024, 4242, 9001, 65535,
  999983, 1234567): round-trip bit-identical to the snapshot, two loads of one file agree, and
  five negative controls (truncated, wrong version, bad magic, out-of-bounds position, missing
  file) all rejected with the live game bit-for-bit untouched. Full suite re-run green: rng, iso,
  fog, sprite, fade, font, land (20), village (30), sector, portal (30), shard (30), region (30),
  reach (50), gating (30), play (50/50).
- **Not verified:** no human has played a saved-and-reloaded game (the gate's manual clause) — the
  automated test compares raw state, not feel. F5/F9 during grid view loads the world but shows it
  only once F2 is pressed; left as-is rather than adding a mode switch nobody asked for.
