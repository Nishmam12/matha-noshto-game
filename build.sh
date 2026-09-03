#!/usr/bin/env bash
# Wayfarer build script for Linux & Windows cross-compilation
# Usage:
#   ./build.sh [linux|windows|all] [--selftest]
#
# Default: builds both shipping binaries into build/linux/ and build/windows/

set -euo pipefail

TARGET="${1:-all}"
SELFTEST=0

for arg in "$@"; do
    if [ "$arg" == "--selftest" ] || [ "$arg" == "-SelfTest" ]; then
        SELFTEST=1
    fi
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"

build_linux() {
    echo "=== Building for Linux ==="
    mkdir -p "$BUILD_DIR/linux"
    
    local cflags=("-std=c99" "-Os" "-Wall" "-Wextra" "-Werror" \
                  "-ffunction-sections" "-fdata-sections" \
                  "-fno-ident" "-fno-asynchronous-unwind-tables" \
                  "-Isrc" $(sdl2-config --cflags))
    local ldflags=("-s" "-Wl,--gc-sections" $(sdl2-config --libs) "-lm")
    
    if [ "$SELFTEST" -eq 1 ]; then
        local out="$BUILD_DIR/linux/wayfarer-selftest"
        echo "Compiling Linux selftest binary: $out"
        gcc "${cflags[@]}" -DWAYFARER_SELFTEST=1 "$ROOT/src/main.c" "${ldflags[@]}" -o "$out"
    else
        local out="$BUILD_DIR/linux/wayfarer"
        echo "Compiling Linux shipping binary: $out"
        gcc "${cflags[@]}" "$ROOT/src/main.c" "${ldflags[@]}" -o "$out"
        
        local out_test="$BUILD_DIR/linux/wayfarer-selftest"
        echo "Compiling Linux selftest binary: $out_test"
        gcc "${cflags[@]}" -DWAYFARER_SELFTEST=1 "$ROOT/src/main.c" "${ldflags[@]}" -o "$out_test"
    fi
    echo "Linux build complete in build/linux/"
}

build_windows() {
    echo "=== Building for Windows ==="
    mkdir -p "$BUILD_DIR/windows"
    
    local MINGW_PATH="/home/afnan/.local/opt/mingw-w64/usr/bin"
    local MINGW_GCC="$MINGW_PATH/x86_64-w64-mingw32-gcc"
    local SDL_DIR="/home/afnan/.local/opt/SDL2-mingw/x86_64-w64-mingw32"
    
    if [ ! -f "$MINGW_GCC" ]; then
        if which x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            MINGW_GCC="$(which x86_64-w64-mingw32-gcc)"
        else
            echo "Error: MinGW-w64 GCC not found."
            return 1
        fi
    fi
    
    local cflags=("-std=c99" "-Os" "-Wall" "-Wextra" "-Werror" \
                  "-ffunction-sections" "-fdata-sections" \
                  "-fno-ident" "-fno-asynchronous-unwind-tables" \
                  "-Isrc" "-I$SDL_DIR/include" "-I$SDL_DIR/include/SDL2")
    local ldflags=("-s" "-Wl,--gc-sections" \
                   "-L$SDL_DIR/lib" \
                   "-lmingw32" "-lSDL2main" "-lSDL2" \
                   "-Wl,--dynamicbase" "-Wl,--nxcompat" "-Wl,--high-entropy-va" \
                   "-lm" "-lkernel32" "-luser32" "-lgdi32" "-lwinmm" "-limm32" \
                   "-lole32" "-loleaut32" "-lversion" "-luuid" "-ladvapi32" "-lsetupapi" "-lshell32")
    
    export PATH="$MINGW_PATH:$PATH"
    
    if [ "$SELFTEST" -eq 1 ]; then
        local out="$BUILD_DIR/windows/wayfarer-selftest.exe"
        echo "Compiling Windows selftest binary: $out"
        "$MINGW_GCC" "${cflags[@]}" -DWAYFARER_SELFTEST=1 "$ROOT/src/main.c" -mconsole "${ldflags[@]}" -o "$out"
    else
        local out="$BUILD_DIR/windows/wayfarer.exe"
        echo "Compiling Windows shipping binary: $out"
        "$MINGW_GCC" "${cflags[@]}" "$ROOT/src/main.c" -mwindows "${ldflags[@]}" -o "$out"
        
        local out_test="$BUILD_DIR/windows/wayfarer-selftest.exe"
        echo "Compiling Windows selftest binary: $out_test"
        "$MINGW_GCC" "${cflags[@]}" -DWAYFARER_SELFTEST=1 "$ROOT/src/main.c" -mconsole "${ldflags[@]}" -o "$out_test"
    fi
    
    if [ -f "$SDL_DIR/bin/SDL2.dll" ]; then
        cp -f "$SDL_DIR/bin/SDL2.dll" "$BUILD_DIR/windows/"
    fi
    echo "Windows build complete in build/windows/"
}

case "$TARGET" in
    linux)
        build_linux
        ;;
    windows)
        build_windows
        ;;
    all)
        build_linux
        build_windows
        ;;
    *)
        echo "Unknown target: $TARGET. Choose linux, windows, or all."
        exit 1
        ;;
esac
