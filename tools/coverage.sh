#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Local coverage run - the same sequence the CI coverage job uses, so a number
# you see here is the number that ends up in the README.
#
#   tools/coverage.sh              build, test, report to build-coverage/
#   tools/coverage.sh --write      also rewrite the README badge and table
#
# Needs gcovr (pip install gcovr) and a GCC or Clang toolchain.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-${root}/build-coverage}"
write=0
[[ "${1:-}" == "--write" ]] && write=1

cd "${root}"

cmake -S . -B "${build}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFMS_ENABLE_COVERAGE=ON \
  -DFMS_BUILD_TESTS=ON \
  -DFMS_BUILD_EXAMPLES=ON

cmake --build "${build}" -j"$(nproc 2>/dev/null || echo 4)"

# Counters accumulate across runs, so start from zero or a re-run inflates them.
find "${build}" -name '*.gcda' -delete

ctest --test-dir "${build}" --output-on-failure

# --gcov-object-directory keeps gcov's intermediate .gcov files inside the build
# tree.  Without it gcov runs with the source root as its working directory and
# litters it, which also fails outright on a filesystem that will not let the
# files be removed again.
mkdir -p "${build}/report"
gcovr --config "${root}/gcovr.cfg" \
  --root "${root}" \
  --gcov-object-directory "${build}" \
  "${build}" \
  --json-summary-pretty -o "${build}/coverage.json" \
  --html-details "${build}/report/index.html" \
  --txt

if [[ "${write}" == 1 ]]; then
  python3 tools/coverage_report.py "${build}/coverage.json" \
    --readme README.md --badge docs/badges/coverage.svg
fi

echo
echo "HTML report: ${build}/report/index.html"
