#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Compile fms_core and fms_inspect for a bare-metal ARM target.
#
#   tools/cross_check.sh [build-dir]     # default: build-arm
#
# docs/architecture.md's porting section says fms_core needs only <cstdint>,
# <cstddef>, <cstring> and ETL, and that a firmware build links it and stops
# there.  Nothing tested that: every other job builds for the host, where a
# stray <iostream> or a call into the hosted runtime compiles fine.
#
# What this proves is that the two targets a firmware build would take compile
# for a Cortex-M4 with no operating system.  It does not link an image - there
# is no startup code, linker script or board here, and none of that belongs to
# the library.
#
# Not strictly freestanding: ETL's etl/limits.h includes <math.h>, which
# libstdc++ 13 refuses under -ffreestanding.  cmake/arm-none-eabi.cmake carries
# the detail.
#
# fms_config is off: it is yaml-cpp, which wants a filesystem and exceptions.
# Loading the configuration on the host and shipping the Model is the porting
# route docs/architecture.md describes.
#
# Needs arm-none-eabi-g++ (apt install gcc-arm-none-eabi) and cmake.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build-arm}"

if ! command -v arm-none-eabi-g++ > /dev/null 2>&1; then
  echo "cross_check: arm-none-eabi-g++ not found - a gate that cannot run has not passed" >&2
  echo "             apt install gcc-arm-none-eabi" >&2
  exit 1
fi

arm-none-eabi-g++ --version | head -1

# On Ubuntu the compiler, the C library and the C++ headers are three packages,
# and only the first is named gcc-arm-none-eabi.  Without the others every
# translation unit fails on <cstddef>, thirty times over, which reads as the
# library not compiling rather than the toolchain being half installed.
probe="$(mktemp -d)"
trap 'rm -rf "${probe}"' EXIT
printf '#include <cstddef>\n#include <type_traits>\n#include <math.h>\nint main() { return 0; }\n' \
  > "${probe}/probe.cpp"
if ! arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -fsyntax-only \
       "${probe}/probe.cpp" > "${probe}/probe.log" 2>&1; then
  echo "cross_check: the toolchain cannot compile <cstddef>, <type_traits> and <math.h>:" >&2
  head -3 "${probe}/probe.log" >&2
  echo "             apt install libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib" >&2
  exit 1
fi

cmake -S "${root}" -B "${build}" \
  -DCMAKE_TOOLCHAIN_FILE="${root}/cmake/arm-none-eabi.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFMS_WARNINGS_AS_ERRORS=ON \
  -DFMS_BUILD_CONFIG=OFF \
  -DFMS_BUILD_CONSOLE=OFF \
  -DFMS_BUILD_TESTS=OFF \
  -DFMS_BUILD_EXAMPLES=OFF

cmake --build "${build}" --parallel

# The same two constraints the host build checks, on the cross-built archives:
# a target is where they actually matter.
for archive in "${build}/libfms_core.a" "${build}/libfms_inspect.a"; do
  [ -f "${archive}" ] || { echo "cross_check: ${archive} was not built" >&2; exit 1; }

  # nm's exit status is checked rather than piped away: an archive it cannot
  # read would otherwise report zero throwers and pass.  free is in the pattern
  # for the same reason it is in tools/symbol_check.sh - referencing it is
  # referencing the heap.
  listing="${probe}/$(basename "${archive}").sym"
  if ! arm-none-eabi-nm -C "${archive}" > "${listing}" 2>&1; then
    echo "cross_check: arm-none-eabi-nm could not read ${archive}:" >&2
    head -2 "${listing}" >&2
    exit 1
  fi

  throwers="$(grep -cE '__cxa_throw|_Unwind_Resume' "${listing}" || true)"
  allocs="$(grep -E ' U (operator new|operator delete|malloc|calloc|realloc|free)' \
            "${listing}" || true)"

  if [ "${throwers}" -ne 0 ] || [ -n "${allocs}" ]; then
    echo "cross_check: $(basename "${archive}"): ${throwers} throw/unwind symbol(s)" >&2
    [ -n "${allocs}" ] && echo "${allocs}" >&2
    exit 1
  fi
  echo "  $(basename "${archive}"): $(arm-none-eabi-size -t "${archive}" | tail -1)"
done

echo "cross_check: ok - fms_core and fms_inspect compile for cortex-m4, no OS"
