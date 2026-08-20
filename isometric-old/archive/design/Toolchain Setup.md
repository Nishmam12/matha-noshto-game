---
tags: [design, toolchain, wayfarer]
---

# Toolchain Setup

Concrete build environment behind [[Agent Prompt]]'s language decision. Hub: [[Wayfarer MOC]].
Established in [[2026-08-01-session-01]].

## Installed locations

Everything lives **outside the vault** so Obsidian's graph isn't flooded with thousands of
header files. Nothing here is in version control; `build-sdl2.ps1` reproduces it.

| What | Path | Version |
|---|---|---|
| MinGW-w64 (gcc, ld, as, make, ninja, cmake, gdb) | `G:\tools\w64devkit` | w64devkit 2.9.0, GCC 16.1.0, `x86_64-w64-mingw32` |
| SDL2 source | `G:\tools\sdl2-src\SDL2-2.32.10` | 2.32.10 |
| **Our minimal static SDL2** | `G:\tools\SDL2-min` | built by `build-sdl2.ps1` |
| Stock prebuilt SDL2 (reference only, not linked) | `G:\tools\SDL2` | official MinGW dev package |

`G:\tools` rather than `C:\` because C: has under 10 GB free.

## Scripts

- `build-sdl2.ps1` — builds the cut-down static SDL2. **Run once** before the first game build.
- `build.ps1` — builds `build\wayfarer.exe`, prints exact byte size, delta since last build,
  and headroom. Exits nonzero if over budget. `-Map` also emits `build\wayfarer.map`.

`build.ps1` prepends `G:\tools\w64devkit\bin` to `PATH` because gcc shells out to `as.exe`
and `ld.exe` by bare name; normally `w64devkit.exe` sets this up and we bypass that launcher.

## Why we don't use the prebuilt libSDL2.a

The official MinGW `libSDL2.a` contributed **1,656,876 bytes** to a do-nothing window —
over the hard limit before a single line of game code existed. Two causes:

1. It is compiled **without `-ffunction-sections`**, so `--gc-sections` can only discard whole
   object files, never individual unused functions.
2. It ships **every subsystem**: all five render backends, the controller-mapping database,
   HIDAPI, sensors, YUV conversion, a WAV loader.

Building it ourselves with `-Os -ffunction-sections -fdata-sections` and the unused subsystems
off took the executable from **1,714,176 → 669,696 bytes**. The MinGW runtime was never the
problem: a bare static `int main(){return 0;}` is 13,824 bytes.

Biggest single wins: `SDL_dynapi.o` (170,920 B) and `SDL_gamecontroller.o` (116,708 B).

## Two deviations from [[Agent Prompt]]'s recorded flag list

**`-flto` is not used.** w64devkit's GCC is built without LTO support (`cc1.exe: error: LTO
support has not been enabled in this configuration`). Low cost: we are a single translation
unit, so GCC already optimises across all of it, and `libSDL2.a` carries no GCC IR for LTO to
reach into anyway. If we outgrow one `.c` file, prefer a **unity build** (one `.c` that
`#include`s the others) over changing toolchain.

**`SDL_dynapi.h` is patched in the SDL source tree.** SDL hard-refuses `-DSDL_DYNAMIC_API=0`
on the command line (`#error Nope, you have to edit this file to force this off.`) — editing
the header is the sanctioned route. `build-sdl2.ps1` applies this idempotently and fails loudly
if the guard text ever stops matching after an SDL upgrade.

## SDL2 subsystems currently ON

`Atomic  Audio  Video  Events  Threads  Timers  Loadso`

`Loadso` is not optional: `CMakeLists.txt:1902` makes `SDL_VIDEO` require it on Windows for
IME/DX loading. It is the only dependency we could not cut.

Everything else is off — Render, Joystick, Haptic, HIDAPI, Sensor, Power, Filesystem, File,
Locale, Misc, CPUinfo, OpenGL, OpenGL ES, Vulkan, D3D, XInput, DirectX, disk/dummy audio,
dummy/offscreen video. One audio backend (WASAPI) is kept.

**Consequence for rendering:** `SDL_Renderer` does not exist in this build. Drawing goes
straight into the window surface from `SDL_GetWindowSurface` / `SDL_UpdateWindowSurface`.
This matches [[Agent Prompt]]'s "all rendering is hand-rolled" constraint.

**Consequence for input:** no gamepad support. If the finalised design in
[[Pending Team Discussion]] wants controllers, re-enable `SDL_JOYSTICK` and re-measure —
budget roughly 120–150 KB for it, and take that number from a real build, not this estimate.

## Size reserve — not yet reclaimed

Still linked despite being unreachable for a game with no video playback and no asset files.
Left alone deliberately: we have 770 KB of headroom and [[Agent Prompt]] forbids speculative
work. Come back here first if the budget ever tightens.

| Object | Bytes | Why it should be removable |
|---|---:|---|
| `yuv_rgb_sse` + `yuv_rgb_std` + `SDL_yuv` | ~83,000 | YUV conversion; we decode no video |
| `SDL_blit_auto` + `SDL_blit_N` + `SDL_blit_A` + `SDL_RLEaccel` | ~140,000 | generic surface blitters; we write pixels directly |
| `SDL_audiocvt` | ~42,000 | audio format conversion; avoidable by opening the device in its native format |
| `SDL_render` | ~34,000 | still compiled in despite `SDL_RENDER=OFF` — worth chasing down |

Roughly 250 KB recoverable if needed.

## Audio architecture (established session 02)

Device is opened with `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` **only**. Format stays `AUDIO_F32SYS`
and channels stay 2. Rationale: F32 is WASAPI's native format on Windows, so in practice the
device matches our request exactly and no conversion runs — confirmed at runtime at both 48000
and 44100 Hz. Allowing a frequency change costs one multiply in the phase increment; letting SDL
resample would add latency and drag in its converter.

Buffer is 1024 frames — a 21.3 ms deadline at 48 kHz. Measured worst-case callback is 0.079 ms,
so there is roughly 270× headroom for real synthesis work.

**Video and audio are initialised separately, and this is not stylistic.** `SDL_Init` fails if
*any* requested subsystem fails, so `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)` refuses to start
the game on a machine with no working sound device. That was a real launch-blocking bug, found
and fixed in session 02. Video is fatal; audio is optional and silence is an acceptable degraded
mode. **Do not merge these two calls back together.**

## Self-test builds

`build.ps1 -SelfTest` produces a **separate** `build\wayfarer-selftest.exe` with
`-DWAYFARER_SELFTEST=1` and a console attached. It is never budget-tracked and never shipped;
`wayfarer.exe` contains none of its code, which is checked by string-scanning the release binary.

This structure exists to satisfy [[Agent Prompt]]'s "no debug-only code paths left enabled by
default" rule *structurally* rather than by remembering to turn a flag off before submission.

## Trap: zero-initialised statics land in `.data`, not `.bss`

On PE/COFF, `-fdata-sections` makes GCC emit zero-initialised statics as `.data$name` COMDAT
sections rather than `.bss`. `.data` is stored in the executable file; `.bss` is not. Measured
2026-08-02: four world-sized `static` arrays put **39,648 bytes of literal zeros** into
`wayfarer.exe`.

Moving them to stack locals recovered **39,424 bytes** at no runtime cost. Peak stack use is about
36 KB against a 2 MB default, so this is free.

**Rule for this project: never declare a world-sized buffer `static`.** Use a stack local, or heap
memory if it outgrows the stack. Verify with `objdump -h` — if `.data` is large and `.bss` is
small, this is happening again. It gets worse as the world grows, so check after any change to
world dimensions.

## Verified toolchain facts

- Zero shipped DLLs. Imports are OS-provided only: `KERNEL32 USER32 GDI32 ADVAPI32 SHELL32
  OLE32 OLEAUT32 IMM32 WINMM VERSION msvcrt`. `msvcrt.dll` ships with Windows itself.
- Builds clean under `-Wall -Wextra` with zero warnings.
