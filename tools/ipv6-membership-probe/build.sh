#!/usr/bin/env bash
set -euo pipefail
: "${DEVKITPRO:?Point DEVKITPRO to an existing devkitPro installation}"
root="$(cd -- "$(dirname -- "$0")" && pwd)"
build="$root/build"
mkdir -p -- "$build"
compiler="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc"
"$compiler" -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft \
  -fPIE -D__SWITCH__ -O2 -Wall -Wextra -std=gnu11 \
  "-I$DEVKITPRO/libnx/include" "-specs=$DEVKITPRO/libnx/switch.specs" \
  "$root/inspector.c" "-L$DEVKITPRO/libnx/lib" -lnx \
  -o "$build/UU-IPv6-Inspector.elf"
"$DEVKITPRO/tools/bin/nacptool" --create 'UU IPv6 Inspector' \
  'UU Remote contributors' '0.16.0' "$build/inspector.nacp"
"$DEVKITPRO/tools/bin/elf2nro" "$build/UU-IPv6-Inspector.elf" \
  "$build/UU-IPv6-Inspector.nro" "--nacp=$build/inspector.nacp"
