#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# The two hard constraints, checked against the built archives rather than
# against the compiler flags that are supposed to produce them.
#
#   tools/symbol_check.sh [build-dir]      # default: build
#
# docs/architecture.md has always shown these nm invocations; running them was
# left to whoever remembered.  A -fno-exceptions that stopped being applied, a
# container that started calling operator new, or a firewall that quietly lost
# its try/catch would all keep the suite green.
#
#   fms_core        no throw or unwind symbols, and no reference to an allocator
#   fms_inspect     the same: lint and diagram are documented as safe to run on
#                   a target, which means neither may allocate or throw
#   fms_console     no throw or unwind symbols.  It may reference an allocator:
#                   it pulls in <iostream>, whose internals are not ours
#   fms_alloc_guard no throw or unwind symbols.  It references malloc and free
#                   because implementing operator new is what it is for
#   fms_config      MUST have throw or unwind symbols - it is the firewall, and
#                   one that stopped catching would look like an improvement here
#
# Reads build-dir/symbol_check.env, which the build writes.  Needs nm; GCC and
# Clang only, since the symbol names are the platform's.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build}"
env_file="${build}/symbol_check.env"

if [ ! -f "${env_file}" ]; then
  echo "symbol_check: no ${env_file} - configure with -DFMS_BUILD_TESTS=ON first" >&2
  exit 1
fi

# Read rather than source: the file comes out of a build directory, and a gate
# has no business executing whatever is in one.
setting() {   # setting <key>  -> the value, or empty
  sed -n "s/^$1=\"\(.*\)\"$/\1/p" "${env_file}"
}

nm_tool="$(setting NM)"
[ -n "${nm_tool}" ] || nm_tool="nm"
if ! command -v "${nm_tool}" > /dev/null 2>&1; then
  echo "symbol_check: ${nm_tool} not found - a gate that cannot run has not passed" >&2
  exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

throwing='__cxa_throw|_Unwind_Resume'
# The array forms need no entry of their own: "operator new[](unsigned long)"
# contains "operator new".  free does, though - fms_alloc_guard references it
# and the table above says so, and a check that let it through would not match
# what it claims to enforce.
allocating=' U (operator new|operator delete|malloc|calloc|realloc|free)'
failures=0

report() {   # report <archive> <what was wrong> [detail...]
  echo "symbol_check: $(basename "$1"): $2" >&2
  shift 2
  for line in "$@"; do
    echo "    ${line}" >&2
  done
  failures=$((failures + 1))
}

# nm's output, once, with its exit status checked.  Piping nm straight into
# grep would turn "nm could not read this archive" into "grep matched nothing",
# which is the same thing as a pass.
symbols_of() {   # symbols_of <archive> -> path to the symbol listing, or fails
  local archive="$1"
  local listing
  listing="${work}/$(basename "${archive}").sym"

  if [ -s "${listing}" ]; then
    echo "${listing}"
    return 0
  fi
  if ! "${nm_tool}" -C "${archive}" > "${listing}" 2> "${listing}.err"; then
    return 1
  fi
  echo "${listing}"
}

count_throwing() {   # count_throwing <listing>
  grep -cE "${throwing}" "$1" || true
}

allocator_refs() {   # allocator_refs <listing>
  grep -E "${allocating}" "$1" | sed 's/^ *U //' | sort -u || true
}

check_archive() {   # check_archive <key> <allow-allocator: yes|no>
  local key="$1" allow_alloc="$2"
  local archive before="${failures}"
  archive="$(setting "${key}")"

  if [ -z "${archive}" ]; then
    return 0        # the target was not built; its claims are not in this build
  fi
  if [ ! -f "${archive}" ]; then
    report "${archive}" "listed in ${env_file} but not on disk"
    return 0
  fi

  local listing
  if ! listing="$(symbols_of "${archive}")"; then
    report "${archive}" "${nm_tool} could not read it" \
      "$(head -2 "${work}/$(basename "${archive}").sym.err" 2> /dev/null)"
    return 0
  fi

  local throwers
  throwers="$(count_throwing "${listing}")"
  if [ "${throwers}" -ne 0 ]; then
    report "${archive}" "${throwers} throw/unwind symbol(s); it is built -fno-exceptions"
  fi

  if [ "${allow_alloc}" = "no" ]; then
    local refs
    refs="$(allocator_refs "${listing}")"
    if [ -n "${refs}" ]; then
      local lines=()
      while IFS= read -r line; do lines+=("${line}"); done <<< "${refs}"
      report "${archive}" "references an allocator" "${lines[@]}"
    fi
  fi

  if [ "${failures}" -eq "${before}" ]; then
    echo "  $(basename "${archive}"): 0 throw/unwind$([ "${allow_alloc}" = no ] && echo ", no allocator")"
  fi
}

echo "symbol_check: reading the archives with ${nm_tool}"
check_archive FMS_CORE_LIB        no
check_archive FMS_INSPECT_LIB     no
check_archive FMS_CONSOLE_LIB     yes
check_archive FMS_ALLOC_GUARD_LIB yes

# The firewall, checked the other way round.  fms_config is the one translation
# unit compiled -fexceptions, and it is only a firewall while it still has the
# machinery to catch with.
# Absent from the file means the loader was not built (FMS_BUILD_CONFIG=OFF) and
# there is no firewall to check.  Named but missing, or unreadable, is a broken
# gate rather than an absent target, and is reported as one.
config="$(setting FMS_CONFIG_LIB)"
if [ -n "${config}" ]; then
  if [ ! -f "${config}" ]; then
    report "${config}" "listed in ${env_file} but not on disk"
  elif ! config_listing="$(symbols_of "${config}")"; then
    report "${config}" "${nm_tool} could not read it"
  elif [ "$(count_throwing "${config_listing}")" -eq 0 ]; then
    report "${config}" "no throw/unwind symbols: the yaml-cpp firewall is gone"
  else
    echo "  $(basename "${config}"): has the unwind machinery, as the firewall must"
  fi
fi

if [ "${failures}" -ne 0 ]; then
  echo "symbol_check: ${failures} archive(s) do not match what the docs claim" >&2
  exit 1
fi

echo "symbol_check: ok"
