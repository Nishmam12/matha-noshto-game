# Build a minimal static SDL2 for Wayfarer. Run once; build.ps1 links its output.
# Why we don't use the prebuilt libSDL2.a: see design/Toolchain Setup.md.
#
# The stock MinGW libSDL2.a costs 1,656,876 bytes linked because it is compiled
# without -ffunction-sections (so --gc-sections can only drop whole objects) and
# ships every subsystem. This build cuts what the game does not use and lets the
# linker discard at function granularity.

param([switch]$Clean)

$ErrorActionPreference = 'Stop'

# Toolchain lives outside the repo. Override the root with $env:WAYFARER_TOOLS.
$TOOLS  = if ($env:WAYFARER_TOOLS) { $env:WAYFARER_TOOLS } else { 'G:\tools' }
$DEVKIT = Join-Path $TOOLS 'w64devkit'
$SRC    = Join-Path $TOOLS 'sdl2-src\SDL2-2.32.10'
$BUILD  = Join-Path $TOOLS 'sdl2-build'
$PREFIX = Join-Path $TOOLS 'SDL2-min'

if (-not (Test-Path "$DEVKIT\bin\gcc.exe")) {
    Write-Host "w64devkit not found at $DEVKIT - see README.md for setup." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "$SRC\CMakeLists.txt")) {
    Write-Host "SDL2 source not found at $SRC - see README.md for setup." -ForegroundColor Red
    exit 1
}

if ($env:PATH -notlike "*$DEVKIT\bin*") { $env:PATH = "$DEVKIT\bin;$env:PATH" }
$CMAKE = "$DEVKIT\bin\cmake.exe"

if ($Clean -and (Test-Path $BUILD)) { Remove-Item $BUILD -Recurse -Force }

# --- patch: disable the dynamic API dispatch table -------------------------
# SDL_dynapi.o is 170,920 bytes of function-pointer dispatch that exists so a
# distro can swap in a newer SDL .dll at runtime. We are a statically linked
# single-file contest entry, so it can never do anything for us. SDL refuses
# -DSDL_DYNAMIC_API=0 on the command line ("you have to edit this file to force
# this off"), so editing the header is the sanctioned route. Idempotent.
$dynapi = "$SRC\src\dynapi\SDL_dynapi.h"
$text = [System.IO.File]::ReadAllText($dynapi)
if ($text -notmatch 'WAYFARER') {
    $guard = "#ifdef SDL_DYNAMIC_API /* Tried to force it on the command line? */`n#error Nope, you have to edit this file to force this off.`n#endif"
    $patched = $guard + "`n`n/* WAYFARER: statically linked single-binary contest entry -- the dynamic API`n   dispatch table is dead weight we can never use. SDL requires this be forced`n   here rather than on the command line. Applied by build-sdl2.ps1. */`n#define SDL_DYNAMIC_API 0"
    $new = $text.Replace($guard.Replace("`n","`r`n"), $patched.Replace("`n","`r`n"))
    if ($new -eq $text) { $new = $text.Replace($guard, $patched) }
    if ($new -eq $text) { Write-Host "PATCH FAILED: dynapi guard not found in $dynapi" -ForegroundColor Red; exit 1 }
    [System.IO.File]::WriteAllText($dynapi, $new, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "patched $dynapi (SDL_DYNAMIC_API forced to 0)" -ForegroundColor Yellow
} else {
    Write-Host "dynapi already patched" -ForegroundColor DarkGray
}

$cflags = '-Os -ffunction-sections -fdata-sections -fno-ident'

$opts = @(
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_C_FLAGS_RELEASE=$cflags -DNDEBUG",
    '-DCMAKE_C_COMPILER=gcc',
    "-DCMAKE_INSTALL_PREFIX=$PREFIX",

    '-DSDL_SHARED=OFF', '-DSDL_STATIC=ON', '-DSDL_TEST=OFF', '-DSDL_TESTS=OFF',
    '-DSDL_INSTALL_TESTS=OFF', '-DSDL_ASSERTIONS=disabled', '-DSDL_LIBC=ON',

    # --- subsystems we keep ---
    #   Atomic Audio Video Events Threads Timers  (audio needs threads+atomic)
    #   Loadso is forced: CMakeLists.txt:1902 makes SDL_VIDEO require it on
    #   Windows for IME/DX loading. It is the only dependency we cannot cut.

    # --- subsystems we cut ---
    '-DSDL_RENDER=OFF',      # rendering is hand-rolled onto a window surface
    '-DSDL_JOYSTICK=OFF',    # SDL_gamecontroller.o alone is 116,708 bytes
    '-DSDL_HAPTIC=OFF',
    '-DSDL_HIDAPI=OFF',
    '-DSDL_SENSOR=OFF',
    '-DSDL_POWER=OFF',
    '-DSDL_FILESYSTEM=OFF',
    '-DSDL_FILE=OFF',        # no asset files exist to load
    '-DSDL_LOCALE=OFF',
    '-DSDL_MISC=OFF',
    '-DSDL_CPUINFO=OFF',

    # --- backends we cut ---
    '-DSDL_OPENGL=OFF', '-DSDL_OPENGLES=OFF', '-DSDL_VULKAN=OFF',
    '-DSDL_RENDER_D3D=OFF', '-DSDL_DIRECTX=OFF', '-DSDL_XINPUT=OFF',
    '-DSDL_DISKAUDIO=OFF', '-DSDL_DUMMYAUDIO=OFF',
    '-DSDL_DUMMYVIDEO=OFF', '-DSDL_OFFSCREEN=OFF',
    '-DSDL_WASAPI=ON'        # keep one real audio backend
)

Write-Host "--- configure ---" -ForegroundColor Cyan
& $CMAKE -G Ninja -S $SRC -B $BUILD @opts
if ($LASTEXITCODE -ne 0) { Write-Host "CONFIGURE FAILED" -ForegroundColor Red; exit 1 }

Write-Host "--- build ---" -ForegroundColor Cyan
& $CMAKE --build $BUILD --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "SDL2 BUILD FAILED" -ForegroundColor Red; exit 1 }

Write-Host "--- install ---" -ForegroundColor Cyan
& $CMAKE --install $BUILD
if ($LASTEXITCODE -ne 0) { Write-Host "INSTALL FAILED" -ForegroundColor Red; exit 1 }

$lib = "$PREFIX\lib\libSDL2.a"
Write-Host ""
Write-Host ("minimal libSDL2.a: {0:N0} bytes archive (stock was {1:N0})" -f (Get-Item $lib).Length, 15679488) -ForegroundColor Green
Write-Host "installed to $PREFIX"
