#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Local static analysis - and the same script the CI jobs call, so there is one
# copy of the flags and a finding you see here is the finding that fails the
# build.
#
#   tools/analyze.sh                 everything below
#   tools/analyze.sh clang-tidy      the compiler's own analyser, over our TUs
#   tools/analyze.sh cppcheck        a second opinion from a different engine
#   tools/analyze.sh lint            the tooling: ruff, shellcheck, actionlint
#
# Needs cppcheck and clang-tidy with run-clang-tidy; `lint` additionally wants
# ruff, shellcheck and actionlint.  A missing tool is reported and fails the
# run rather than being skipped quietly: a gate that cannot run has not passed.
#
# Overrides, for CI or for a distribution that suffixes its binaries:
#   BUILD_DIR           compile-database tree           (default build-tidy)
#   RUN_CLANG_TIDY      run-clang-tidy-18, say          (default run-clang-tidy)
#   CLANG_TIDY_BINARY   clang-tidy-18, say              (default clang-tidy)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-${root}/build-tidy}"
run_clang_tidy="${RUN_CLANG_TIDY:-run-clang-tidy}"
clang_tidy="${CLANG_TIDY_BINARY:-clang-tidy}"

# The gate.  These are the checks that find defects rather than debate style;
# everything else in .clang-tidy is printed for the log.  Tighten as findings
# are cleared.
gate='bugprone-*,cert-*,clang-analyzer-*,concurrency-*,performance-*,portability-*'

# Our own translation units, all three kinds.  Tests and examples are code too,
# and include/fms/port/memory_port.hpp is reached from nowhere else.
#
# The checkout path becomes part of a regex, so a clone under "fms-yaml (2)"
# matches nothing at all - and run-clang-tidy exits 0 when its pattern matches
# nothing, which is a green tick over a tree no analyser read.  Escape the
# path here; count what was actually matched below.
root_re="$(printf '%s' "${root}" | sed 's/[][\^$.*+?(){}|]/\\&/g')"
sources="^${root_re}/(src|tests|examples)/.*\.cpp\$"

cd "${root}"
missing=()

have() {
  if command -v "$1" >/dev/null 2>&1; then
    return 0
  fi
  missing+=("$1 ($2)")
  return 1
}

# Report to the terminal, and into the job summary when there is one.
summarise() {
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    tee -a "${GITHUB_STEP_SUMMARY}"
  else
    cat
  fi
}

# clang-tidy needs the exact compile line for every file, which is what
# compile_commands.json is.  Nothing is compiled here beyond yaml-cpp, which
# generates headers the loader TU cannot parse without.
configure() {
  if [[ ! -f "${build}/compile_commands.json" ]]; then
    echo "==> configuring ${build}"
    cmake -S "${root}" -B "${build}" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      ${CXX:+-DCMAKE_CXX_COMPILER="${CXX}"} || return 1
  fi
  # Building any target regenerates the build system first, so a source added
  # to CMakeLists.txt since the last run lands in the database rather than
  # being quietly left out of the analysis.
  cmake --build "${build}" --target yaml-cpp --parallel >/dev/null || return 1
}

# How many of the compile database's entries the pattern actually selects.
# Mirrors run-clang-tidy's own filtering, which is re.search over "file".
matched_files() {
  python3 -c '
import json, re, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    entries = json.load(handle)
pattern = re.compile(sys.argv[2])
print(sum(1 for entry in entries if pattern.search(entry["file"])))' \
    "${build}/compile_commands.json" "${sources}"
}

analyse_clang_tidy() {
  have "${run_clang_tidy}" "apt install clang-tools" || return 0
  have "${clang_tidy}" "apt install clang-tidy" || return 0
  have python3 "apt install python3" || return 0
  configure || return 1

  local matched present
  matched="$(matched_files)"
  present="$(find src tests examples -name '*.cpp' 2>/dev/null | wc -l)"
  echo "==> clang-tidy (${matched} of ${present} .cpp files)"
  if [[ "${matched}" -eq 0 ]]; then
    echo "clang-tidy selected no files: the pattern does not match this" >&2
    echo "checkout path, so nothing would have been analysed." >&2
    return 1
  fi
  # The pipe is why this line lives in a script with `set -o pipefail` rather
  # than in a workflow `run:` block, where the default shell has it off and the
  # step would report tee's exit code - always zero - instead of the analyser's.
  "${run_clang_tidy}" \
    -p "${build}" \
    -clang-tidy-binary "${clang_tidy}" \
    -quiet \
    -warnings-as-errors="${gate}" \
    "${sources}" \
    | tee "${root}/clang-tidy.log"
}

analyse_cppcheck() {
  have cppcheck "apt install cppcheck" || return 0
  have python3 "apt install python3" || return 0

  echo "==> cppcheck"
  # No compile database on purpose: cppcheck is here as an independent second
  # reader of the source, and pointing it at src/ and include/ keeps it away
  # from the dependency tree entirely.
  #
  # No --error-exitcode either.  cppcheck_report.py decides what fails, by
  # severity, because cppcheck's style opinions change between releases and a
  # new patch version inventing a new style check should not be able to turn
  # the tree red.  Defects still do.
  #
  # Its exit code matters as much as its findings: a suppressions file it
  # cannot parse makes it die before writing a report, and "no findings" and
  # "never ran" must not look alike.  The report runs either way, so the job
  # summary says which of the two happened.
  local engine=0
  cppcheck \
    --enable=warning,style,performance,portability \
    --std=c++17 \
    --language=c++ \
    --inline-suppr \
    --suppressions-list=.cppcheck-suppressions \
    --suppress=missingIncludeSystem \
    --check-level=exhaustive \
    -I include \
    --xml --xml-version=2 \
    src include 2> "${root}/cppcheck.xml" || engine=1

  python3 tools/cppcheck_report.py "${root}/cppcheck.xml" | summarise || return 1
  return "${engine}"
}

# The scripts that decide whether the other gates pass are code as well, and
# until now nothing read them.  No formatter runs here: the Python is aligned
# by hand in places and ruff format would fight it.
analyse_lint() {
  local status=0

  if have ruff "pip install ruff"; then
    echo "==> ruff"
    ruff check tools || status=1
  fi
  if have shellcheck "apt install shellcheck"; then
    echo "==> shellcheck"
    shellcheck tools/*.sh || status=1
  fi
  if have actionlint "https://github.com/rhysd/actionlint/releases"; then
    echo "==> actionlint"
    actionlint .github/workflows/*.yml || status=1
  fi

  return "${status}"
}

# `|| status=1` rather than letting errexit stop at the first failure: hearing
# about cppcheck only after clang-tidy is clean costs a round trip each time,
# and the missing-tool report below would never print.  Errexit is suppressed
# inside a function called this way, which is why every step that matters above
# ends in an explicit `|| return 1`.
status=0

case "${1:-all}" in
  clang-tidy) analyse_clang_tidy || status=1 ;;
  cppcheck)   analyse_cppcheck   || status=1 ;;
  lint)       analyse_lint       || status=1 ;;
  all)
    analyse_clang_tidy || status=1
    analyse_cppcheck   || status=1
    analyse_lint       || status=1
    ;;
  *)
    echo "usage: tools/analyze.sh [clang-tidy|cppcheck|lint|all]" >&2
    exit 2
    ;;
esac

if [[ ${#missing[@]} -gt 0 ]]; then
  echo >&2
  echo "not installed, so nothing was checked with it:" >&2
  printf '  %s\n' "${missing[@]}" >&2
  status=1
fi

exit "${status}"
