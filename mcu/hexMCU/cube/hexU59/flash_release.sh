#!/usr/bin/env bash
# flash_release.sh -- build the final board (hexU59) as a Release build and
# flash it over SWD, without the debugger: the board starts running as soon
# as it's written, and the ST-LINK can be unplugged after.
#
#   ./flash_release.sh        log level 0: no log messages compiled in
#   ./flash_release.sh 3      log level 3 (info), like the Debug build
#
# Each log level gets its own build folder (build/Release-log<N>), so this
# never changes the normal Debug / Release builds VS Code uses.
#
# The compiler: the same arm-none-eabi-gcc as the Debug build VS Code made
# (read from build/Debug/CMakeCache.txt), since the one on a plain shell's
# PATH may be a system one without a C library (no stdint.h). Override with
# ARM_GCC=/path/to/arm-none-eabi-gcc.
#
# Needs STM32_Programmer_CLI (STM32CubeProgrammer, also in STM32CubeCLT). It
# is looked for on PATH and in the usual install folders; if it's somewhere
# else, point STM32_PROGRAMMER_CLI at it.
set -euo pipefail
cd "$(dirname "$0")"

level="${1:-0}"
case "$level" in [0-4]) ;; *) echo "log level must be 0..4, not '$level'" >&2; exit 1 ;; esac
build="build/Release-log$level"

# ---- the compiler: VS Code's (the Debug build's), else ST's usual folders
cc="${ARM_GCC:-}"
if [ -z "$cc" ] && [ -f build/Debug/CMakeCache.txt ]; then
    cc="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' build/Debug/CMakeCache.txt | head -n 1)"
    case "$cc" in /*) ;; *) cc="" ;; esac           # only a full path helps
fi
if [ -z "$cc" ]; then
    for c in "$HOME"/.local/share/stm32cube/bundles/gnu-tools-for-stm32/*/bin/arm-none-eabi-gcc \
             /opt/st/stm32cubeclt*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc \
             "$HOME"/st/stm32cubeclt*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc; do
        [ -x "$c" ] && cc="$c"                      # the last match: usually the newest
    done
fi
[ -n "$cc" ] || cc="$(command -v arm-none-eabi-gcc || true)"
[ -n "$cc" ] || { echo "no arm-none-eabi-gcc found: build once with F5 (Debug) first, or set ARM_GCC" >&2; exit 1; }
if ! echo '#include <stdint.h>' | "$cc" -x c -E - >/dev/null 2>&1; then
    echo "$cc has no C library (no stdint.h)." >&2
    echo "Build once with F5 (Debug) so this script can use VS Code's compiler, set" >&2
    echo "ARM_GCC to ST's arm-none-eabi-gcc, or install newlib for this one" >&2
    echo "(Arch: sudo pacman -S arm-none-eabi-newlib)." >&2
    exit 1
fi
export PATH="$(dirname "$cc"):$PATH"                # the toolchain file finds it by name
# a build folder set up with another compiler has to start over
if [ -f "$build/CMakeCache.txt" ] && ! grep -qx "CMAKE_C_COMPILER:[A-Z]*=$cc" "$build/CMakeCache.txt"; then
    echo "$build was set up with another compiler: starting it over"
    rm -rf "$build"
fi
echo "compiler: $cc"

# ---- build: the Release preset, but in its own folder with the log level set
cmake --preset Release -B "$build" -DHEXMCU_LOG_LEVEL="$level" -DCMAKE_C_COMPILER="$cc"
cmake --build "$build"

elf="$(ls -t "$build"/*.elf 2>/dev/null | head -n 1 || true)"
[ -n "$elf" ] || { echo "no .elf in $build" >&2; exit 1; }

# ---- find the programmer
cli="${STM32_PROGRAMMER_CLI:-}"
if [ -z "$cli" ]; then
    cli="$(command -v STM32_Programmer_CLI || true)"
fi
if [ -z "$cli" ]; then
    for c in /opt/st/stm32cubeclt*/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
             "$HOME"/st/stm32cubeclt*/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
             "$HOME"/.local/share/stm32cube/bundles/programmer/*/bin/STM32_Programmer_CLI \
             /opt/st/stm32cubeide*/plugins/*cubeprogrammer*/tools/bin/STM32_Programmer_CLI \
             "$HOME"/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI; do
        [ -x "$c" ] && cli="$c"            # the last match: usually the newest
    done
fi
[ -n "$cli" ] || { echo "STM32_Programmer_CLI not found: set STM32_PROGRAMMER_CLI to its path" >&2; exit 1; }

# ---- flash: SWD under reset, write, verify, then reset and run
echo "flashing $elf (log level $level) with $cli"
"$cli" -c port=SWD mode=UR -w "$elf" -v -rst