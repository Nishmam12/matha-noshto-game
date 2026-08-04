---
tags: [design, phase, wayfarer]
phase: 8
status: planned
updated: 2026-08-04
---

# Phase 08 — Save Load

**Status:** Planned.
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

- [ ] A save captures: world seed, player position, the restored-region bitmask (or per-region
      restoration floats, if partial restoration state matters to preserve — decide based on whether
      `Region.restoration` mid-ease is worth restoring exactly or whether snapping to `restore_to` on
      load is acceptable), and ability flags.
- [ ] Loading a save regenerates the world from the seed (reusing existing generation) and then
      applies the saved deltas — not a from-scratch alternate load path that could drift from what
      generation actually produces.
- [ ] The save format is versioned (a header byte or magic+version field) so a future format change
      fails loudly on load rather than silently loading garbage.
- [ ] Save/load uses `SDL_RWops`, verified to actually link given `SDL_FILESYSTEM` is OFF in this
      project's cut-down SDL2 build (`build-sdl2.ps1`) — this is a real open question, not an
      assumption, and needs checking before the rest of the phase is built on top of it.
- [ ] `--save-test` exists: save, mutate live state, load, assert the resulting state is bit-identical
      to the saved state (not just "close enough" — this project's standing rule against relative
      assertions applies here too).
- [ ] `--save-test` has negative controls: a truncated file and a wrong-version file must both be
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

Not yet started.
