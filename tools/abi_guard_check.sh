#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# The capacities in include/fms/limits.hpp are template arguments of the
# containers inside Model, Setup, Args, Runtime and lint::Report, so they decide
# the layout of those types.  A translation unit compiled with different values
# used to link against fms_core without a word: sizeof(fms::Model) is 49728 with
# the defaults and 21120 with FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12.
# include/fms/abi.hpp turns that into an undefined reference.  This is the gate
# that says it still does.
#
#   tools/abi_guard_check.sh [build-dir]      # default: build
#
# Three checks:
#   1. every FMS_MAX_* in limits.hpp is named on a line that builds the tag
#   2. a probe built with the library's capacities links, and runs
#   3. the tag that probe reports has one field per capacity - being named is
#      not the same as being reached, and an FMS_ABI_ID_nn line left out of the
#      join chain satisfies check 1 while carrying nothing into the symbol
#   4. a probe built with one capacity changed does not link, and says so by
#      naming the symbol it could not resolve
#
# The changed capacity is derived from what the probe reports, not written down
# here, so the gate holds for a tree configured with -DFMS_MAX_STATES=8 as well
# as for the defaults.
#
# Reads build-dir/abi_probe.env, which the build writes.  GCC and Clang only:
# this is a statement about linking, and one toolchain proving it is enough.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build}"
env_file="${build}/abi_probe.env"

if [ ! -f "${env_file}" ]; then
  echo "abi_guard: no ${env_file} - configure with -DFMS_BUILD_TESTS=ON first" >&2
  exit 1
fi

# Read rather than source: the file comes out of a build directory, and a gate
# has no business executing whatever is in one.
setting() {   # setting <key>  -> the value, or empty
  sed -n "s/^$1=\"\(.*\)\"$/\1/p" "${env_file}"
}

cxx="$(setting CXX)"
fms_include="$(setting FMS_INCLUDE)"
etl_include="$(setting ETL_INCLUDE)"
fms_core_lib="$(setting FMS_CORE_LIB)"
capacity_flags="$(setting FMS_CAPACITY_FLAGS)"

for required in cxx fms_include fms_core_lib; do
  if [ -z "${!required}" ]; then
    echo "abi_guard: ${env_file} has no ${required^^}" >&2
    exit 1
  fi
done

# --- 1. the tag covers every capacity ---------------------------------------
# A directive may be indented and may have space after the #, so neither scan
# anchors on column 1: missing a line here is a capacity nobody checks.
directives='^[[:space:]]*#[[:space:]]*define[[:space:]]+'

declared="$(grep -E "${directives}FMS_MAX_[A-Z_]+" "${root}/include/fms/limits.hpp" \
            | grep -oE 'FMS_MAX_[A-Z_]+' | sort -u)"
# Only the lines that build the tag count.  A capacity named in a comment and
# nowhere else is exactly the hole this check is for, so reading the whole file
# would let it through.
tagged="$(grep -E "${directives}FMS_ABI_ID" "${root}/include/fms/abi.hpp" \
          | grep -oE 'FMS_MAX_[A-Z_]+' | sort -u)"
untagged="$(comm -23 <(echo "${declared}") <(echo "${tagged}"))"
if [ -n "${untagged}" ]; then
  echo "abi_guard: capacity in limits.hpp but not in the abi tag:" >&2
  while IFS= read -r name; do
    echo "  ${name}" >&2
  done <<< "${untagged}"
  echo "  a translation unit could differ in it and still link." >&2
  exit 1
fi

# --- the probe ---------------------------------------------------------------
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

cat > "${work}/probe.cpp" <<'CPP'
#include <cstdio>

#include <fms/abi.hpp>
#include <fms/limits.hpp>
#include <fms/model.hpp>
#include <fms/setup.hpp>

// Constructing them is the point: their constructors carry the capacity guard,
// so this program links only against an fms_core that agrees with it.
int main() {
  fms::Model model;
  fms::Setup setup;
  if (model.state_count() != 0 || setup.name().size() != 0) {
    return 1;
  }
  std::printf("%zu %s\n", fms::limits::kMaxStates, fms::abi::tag());
  return 0;
}
CPP

isystem=()
if [ -n "${etl_include}" ]; then
  IFS=';' read -r -a etl_dirs <<< "${etl_include}"
  for dir in "${etl_dirs[@]}"; do
    if [ -n "${dir}" ]; then
      isystem+=(-isystem "${dir}")
    fi
  done
fi

# What this tree was configured with.  Empty means the defaults in limits.hpp,
# which is also what the probe would compile with on its own.
read -r -a capacities <<< "${capacity_flags}"

build_probe() {   # build_probe <log> [extra flags...]
  local log="$1"; shift
  "${cxx}" -std=c++17 -O2 -fno-exceptions -DETL_LOG_ERRORS \
    -I"${fms_include}" "${isystem[@]}" "$@" \
    "${work}/probe.cpp" "${fms_core_lib}" -o "${work}/probe" > "${log}" 2>&1
}

# --- 2. the library's own capacities must link, and run ----------------------
if ! build_probe "${work}/match.log" "${capacities[@]}"; then
  echo "abi_guard: a probe built with the library's own capacities did not link" >&2
  cat "${work}/match.log" >&2
  exit 1
fi
read -r states tag < <("${work}/probe")

# --- 3. the tag carries one field per capacity -------------------------------
# Check 1 reads the source; this reads the symbol the compiler actually built.
# A capacity named on an FMS_ABI_ID_nn line that the join chain never reaches
# passes check 1 and shows up here as a missing field.
declared_count="$(echo "${declared}" | wc -l)"
IFS='_' read -r -a fields <<< "${tag}"
if [ "${#fields[@]}" -ne "${declared_count}" ]; then
  echo "abi_guard: limits.hpp declares ${declared_count} capacities, but the tag" >&2
  echo "           carries ${#fields[@]}: ${tag}" >&2
  echo "           A capacity is named in abi.hpp without reaching FMS_ABI_ID." >&2
  exit 1
fi

# --- 4. one capacity changed must not link -----------------------------------
other=$((states + 1))
# Everything the tree configured except the one being changed, so the compiler
# is never handed two values for the same macro.
changed=()
for flag in "${capacities[@]}"; do
  case "${flag}" in
    -DFMS_MAX_STATES=*) ;;
    *) changed+=("${flag}") ;;
  esac
done
changed+=("-DFMS_MAX_STATES=${other}")

if build_probe "${work}/mismatch.log" "${changed[@]}"; then
  echo "abi_guard: a probe compiled with FMS_MAX_STATES=${other} linked against" >&2
  echo "           a library built with FMS_MAX_STATES=${states}." >&2
  exit 1
fi
if ! grep -q "fms_abi_${other}_" "${work}/mismatch.log"; then
  echo "abi_guard: the mismatched probe failed, but not on the capacity guard." >&2
  echo "           Expected an unresolved fms_abi_${other}_... symbol:" >&2
  cat "${work}/mismatch.log" >&2
  exit 1
fi

echo "abi_guard: ok - ${declared_count} capacities, all in tag ${tag};" \
     "matching links, FMS_MAX_STATES=${other} does not"
