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
# Five checks:
#   1. every FMS_MAX_* in limits.hpp is named on a line that builds the tag
#   2. a probe built with the library's capacities links, and runs
#   3. the tag that probe reports has one field per capacity - being named is
#      not the same as being reached, and an FMS_ABI_ID_nn line left out of the
#      join chain satisfies check 1 while carrying nothing into the symbol
#   4. a probe built with one capacity changed does not link, and says so by
#      naming the symbol it could not resolve
#   5. the types a caller allocates really reference the ETL layout guard, and
#      a probe naming a different layout does not link.
#      The capacities are not the only thing that decides these layouts: ETL
#      moved sizeof(etl::vector) by 8 bytes between its 20.40.0 and 20.40.1
#      tags, and a consumer whose ETL differs from the library's is the same
#      class of mismatch as a consumer differing in a capacity
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
  std::printf("%zu %s %zu %zu %zu\n", fms::limits::kMaxStates, fms::abi::tag(),
              fms::abi::etl_layout::vector, fms::abi::etl_layout::flat_map,
              fms::abi::etl_layout::string);
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
read -r states tag layout_vector layout_flat_map layout_string < <("${work}/probe")

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

# --- 5a. constructing the types must reference the ETL guard -----------------
# Checks 4 and 5b prove the mechanism; neither proves it is reached.  With the
# etl_pin_here::pin() call taken out of fms::abi::pin(), a mismatched ETL links
# silently again and both of those still pass - the capacity half is covered by
# check 4, which fails if FMS_ABI_SYMBOL is not called, and this is the ETL
# half's equivalent.  The probe constructs Model and Setup, so its object must
# carry the guard as an undefined symbol.
if ! "${cxx}" -std=c++17 -O2 -fno-exceptions -DETL_LOG_ERRORS \
     -I"${fms_include}" "${isystem[@]}" "${capacities[@]}" \
     -c "${work}/probe.cpp" -o "${work}/probe.o" > "${work}/probe-object.log" 2>&1; then
  echo "abi_guard: the probe did not compile to an object" >&2
  cat "${work}/probe-object.log" >&2
  exit 1
fi
# nm reads the object; without it this check cannot answer either way, and a
# missing tool must not read as a missing symbol.
if ! command -v nm > /dev/null 2>&1; then
  echo "abi_guard: nm is not installed, and this check reads the probe's" >&2
  echo "           symbol table.  A gate that could not run has not passed." >&2
  exit 1
fi
if ! nm -C "${work}/probe.o" > "${work}/probe-symbols.txt" 2> "${work}/nm.log"; then
  echo "abi_guard: nm could not read ${work}/probe.o" >&2
  cat "${work}/nm.log" >&2
  exit 1
fi
if ! grep -q 'U .*fms::abi::etl_pin<' "${work}/probe-symbols.txt"; then
  echo "abi_guard: constructing Model and Setup does not reference the ETL" >&2
  echo "           layout guard.  fms::abi::pin() is where it is called from;" >&2
  echo "           without that call an ETL whose containers are laid out" >&2
  echo "           differently links silently." >&2
  exit 1
fi

# --- 5b. a different ETL container layout must not link ----------------------
# The numbers come from the probe that just ran, so this is the layout the
# library was actually built with rather than one written down here.  One byte
# different in the vector is a stand-in for the real case, which is an ETL whose
# containers are laid out differently - the same undefined reference either way.
other_vector=$((layout_vector + 1))
cat > "${work}/etl_probe.cpp" <<CPP
#include <fms/abi.hpp>

// Naming the specialisation is the point.  fms_core defines exactly one, for
// the layout its own ETL produced, so any other set of numbers is unresolved.
int main() {
  fms::abi::etl_pin<${other_vector}, ${layout_flat_map}, ${layout_string}>::pin();
  return 0;
}
CPP

if "${cxx}" -std=c++17 -O2 -fno-exceptions -DETL_LOG_ERRORS \
     -I"${fms_include}" "${isystem[@]}" "${capacities[@]}" \
     "${work}/etl_probe.cpp" "${fms_core_lib}" -o "${work}/etl_probe" \
     > "${work}/etl_mismatch.log" 2>&1; then
  echo "abi_guard: a probe naming ETL container layout ${other_vector} linked" >&2
  echo "           against a library built with ${layout_vector}." >&2
  echo "           An ETL whose containers differ would link the same way." >&2
  exit 1
fi
if ! grep -q "etl_pin<${other_vector}" "${work}/etl_mismatch.log"; then
  echo "abi_guard: the ETL-layout probe failed, but not on the layout guard." >&2
  echo "           Expected an unresolved fms::abi::etl_pin<${other_vector}, ...>:" >&2
  cat "${work}/etl_mismatch.log" >&2
  exit 1
fi

echo "abi_guard: ok - ${declared_count} capacities, all in tag ${tag};" \
     "matching links, FMS_MAX_STATES=${other} does not;" \
     "ETL layout ${layout_vector}/${layout_flat_map}/${layout_string} pinned"
