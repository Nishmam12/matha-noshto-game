# Wayfarer — Development & Build Guidelines

## Commands
- Build Shipping: `powershell -ExecutionPolicy Bypass -File .\build.ps1`
- Build Self-Test: `powershell -ExecutionPolicy Bypass -File .\build.ps1 -SelfTest`
- Run All Self-Tests: `powershell -ExecutionPolicy Bypass -File .\tools\run-tests.ps1`
- Run Single Test: `.\build\wayfarer-selftest.exe --<test-name> [--seeds N] [--seed N]`

## Core Invariants & Rules
- Language: C99, compiled with MinGW-w64 GCC (`-std=c99 -Os -Wall -Wextra`). Zero warnings allowed.
- Architecture: Single translation unit (`src/main.c`). Keep helpers localized and static.
- Collision vs Render: Movement/Reachability only inspects `solid[][]` and `regions[].terrain`.
- No Static Structs: Never allocate `World` or `Scratch` as static file-scope variables.
- Size Gate: Output binary `build/wayfarer.exe` must strictly stay under 1,440,000 bytes.
- All fatal startup errors must provide user diagnostics via `SDL_ShowSimpleMessageBox`.

## Conventions
- Multi-byte persistence must remain hand-packed little-endian.
- When adding or modifying self-tests, always include a negative control.