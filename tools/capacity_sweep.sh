#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build and test the tree at more than one capacity configuration.
#
#   tools/capacity_sweep.sh [work-dir]      # default: build-capacities
#
# The FMS_MAX_* values in include/fms/limits.hpp are template arguments of the
# containers inside Model, Setup, Args, Runtime and lint::Report, so each one is
# a different instantiation of the library.  Everything else builds at the
# defaults, which is one of them.  A test that writes out "a=1 b=2 c=3 d=4" or a
# 400-character line or a whole diagnostic sentence is then an assertion about
# the default configuration rather than about the code, and passes for a reason
# nobody chose.
#
# Three configurations:
#
#   default   whatever limits.hpp says
#   tuned     the tuning docs/architecture.md gives for the car example, with
#             the axes it does not mention brought down too.  Every value is at
#             or above what examples/car needs, so the example-driven tests run
#             here as well - which is the point: this is the configuration the
#             documentation claims works
#   wide      well above the defaults, where a capacity-shaped test stops
#             failing and starts passing vacuously
#
# Needs cmake and a compiler.  Nothing is written outside the work directory.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${1:-${root}/build-capacities}"

# One function rather than indirect variable names: the flags are read straight
# out of it, and nothing has to be evaluated to find them.
capacities_for() {   # capacities_for <name>  -> flags on stdout
  case "$1" in
    default)
      ;;
    tuned)
      # docs/architecture.md's tuning for examples/car is the first five; the
      # rest are the axes it is silent about, at the smallest values the example
      # still fits.
      echo "-DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12 -DFMS_MAX_TRANSITIONS_PER_STATE=5"
      echo "-DFMS_MAX_CHANNEL_LENGTH=31 -DFMS_MAX_CONDITIONS=16 -DFMS_MAX_ALTERNATIVES=2"
      echo "-DFMS_MAX_CONDITIONS_PER_GUARD=2 -DFMS_MAX_ARGUMENTS=2"
      echo "-DFMS_MAX_NAME_LENGTH=23 -DFMS_MAX_MESSAGE_LENGTH=71"
      ;;
    wide)
      echo "-DFMS_MAX_STATES=64 -DFMS_MAX_TRIGGERS=64 -DFMS_MAX_TRANSITIONS_PER_STATE=16"
      echo "-DFMS_MAX_CHANNEL_LENGTH=200 -DFMS_MAX_CONDITIONS=128 -DFMS_MAX_ALTERNATIVES=8"
      echo "-DFMS_MAX_CONDITIONS_PER_GUARD=6 -DFMS_MAX_ARGUMENTS=8"
      echo "-DFMS_MAX_NAME_LENGTH=63 -DFMS_MAX_MESSAGE_LENGTH=255"
      ;;
    *)
      echo "capacity_sweep: no such configuration: $1" >&2
      return 1
      ;;
  esac
}

names=(default tuned wide)

mkdir -p "${work}"
summary=()
failed=0

for name in "${names[@]}"; do
  build="${work}/${name}"
  log="${work}/${name}.log"

  echo "capacity_sweep: ${name}"
  # The default configuration has no flags at all, and an empty array is what
  # that has to be: one empty string would reach cmake as an argument.
  flags="$(capacities_for "${name}" | tr '\n' ' ')"
  flag_list=()
  if [ -n "${flags// /}" ]; then
    read -r -a flag_list <<< "${flags}" || true
  fi

  if ! cmake -S "${root}" -B "${build}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DFMS_WARNINGS_AS_ERRORS=ON \
        ${flag_list[@]+"${flag_list[@]}"} > "${log}" 2>&1; then
    echo "capacity_sweep: ${name}: configure failed" >&2
    tail -20 "${log}" >&2
    summary+=("${name}: configure failed")
    failed=$((failed + 1))
    continue
  fi

  if ! cmake --build "${build}" --parallel >> "${log}" 2>&1; then
    echo "capacity_sweep: ${name}: build failed" >&2
    tail -30 "${log}" >&2
    summary+=("${name}: build failed")
    failed=$((failed + 1))
    continue
  fi

  # ctest, not just the unit tests: the example-driven cases are what the tuned
  # configuration is chosen to keep runnable.
  if ! ctest --test-dir "${build}" --output-on-failure >> "${log}" 2>&1; then
    echo "capacity_sweep: ${name}: tests failed" >&2
    sed -n '/The following tests FAILED/,$p' "${log}" >&2
    grep -E "ERROR|FATAL ERROR" "${log}" | head -20 >&2
    summary+=("${name}: tests failed")
    failed=$((failed + 1))
    continue
  fi

  # `|| true` on both: a grep that matches nothing exits 1, and under set -e
  # that would end the run at the configuration that passed.
  passed="$(grep -oE '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "${log}" \
            | tail -1 || true)"
  summary+=("$(printf '%-8s %s' "${name}" "${passed:-completed}")")
done

echo
for line in "${summary[@]}"; do
  echo "  ${line}"
done

if [ "${failed}" -ne 0 ]; then
  echo "capacity_sweep: ${failed} of ${#names[@]} configurations failed" >&2
  exit 1
fi

echo "capacity_sweep: ok - ${#names[@]} configurations"
