# Wayfarer (Top-Down) — Development & Build Guidelines

## Commands
- Build Shipping: `powershell -ExecutionPolicy Bypass -File .\build.ps1`
- Build Self-Test: `powershell -ExecutionPolicy Bypass -File .\build.ps1 -SelfTest`
- Run All Self-Tests: `powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1`
- Run Single Test: `.\build\wayfarer-selftest.exe --<test-name> [--seeds N] [--seed N]`
- Look at the map screen: `--map` (grant + open it), `--standon map` (stand on the fragment)
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
  areas already claim thirty bits. It lives in `Game.maps` (three bits, one per area) and in
  `World.map_tile`, which is `-1` once taken — so "is it still lying there" has exactly one home,
  and a load replays finding it as the delta of clearing that field.
- The map screen's trails are routed by `bfs_gated`, which asks `tile_blocked` — the same function
  that stops her. A trail must never be drawn through a gate she has no ability for; something
  behind one gets **no trail**, and the legend says so.

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

## Save format
- Loading **regenerates from the seed and replays deltas**. Never add a second construction path.
- Validate everything *before* touching the live game: regenerate into scratch, check the position
  against the regenerated `solid` map, commit only then. A failed load must leave the game
  bit-for-bit unchanged, and `--save-test` asserts exactly that for every control.
- Use `!(x >= 0)` rather than `(x < 0)` on floats from disk — it also rejects NaN.
- Byte 20 (abilities) is redundant with the restored mask, deliberately: it is a checksum on it.
- Byte 21 is the map-fragment mask (three bits) as of `SAVE_VERSION` 2; bytes 22–23 are still
  reserved and must be zero. Like the restored mask, it cannot carry bits for an area past the one
  the file says she is in.

## Art pipeline
- `src/art_data.h` is **generated** by `tools/bake.ps1` and committed. Never edit by hand.
- One **global** 65-entry palette; index 0 is transparent in every sprite.
- Fog is applied through a startup-built `fogpal[32][66]` LUT, not per draw call.
- Anchors: decorations use bottom-centre of the trimmed box. **Character sheet frames use a
  cell-relative anchor** (cell centre x, one row below the lowest foot row) — bottom-centre of
  a per-frame trim would make the walk cycle skate. Changing a convention means re-baking.
- Bake only what a caller in `main.c` actually draws.

## Conventions
- When adding or modifying self-tests, **always include a negative control** — a deliberately
  broken case the check must reject. A checker that has never rejected anything proves nothing.
- Look at the screen. Every visual bug of consequence in this project's predecessor was found by
  a screenshot, never by a passing test — and so were three in phase 8 alone (ponds invisible on
  the minimap, a mix pinned against the clamp, unearned abilities lost in the terrain). Use
  `--lit` and `--dev` to reach states a fresh `--frames` run never shows.
- Claims that cannot be verified (how audio *sounds*, how motion *feels*) are recorded as
  unverified rather than asserted.
