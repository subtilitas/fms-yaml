#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build and test against one ETL version, through find_package.
#
#   tools/etl_range_check.sh <etl-version> [work-dir]
#   tools/etl_range_check.sh --check                   # the matrix lists the pin
#
# ETL is pinned, and every other gate builds against that one version.  This is
# the gate that says what happens on any other, because a consumer supplying its
# own ETL takes the find_package(etl REQUIRED) branch and may bring a version
# this project has never compiled against.
#
# What it does not do is assert a size.  docs/stability.md promises no binary
# compatibility in either direction, so a layout that moves between ETL versions
# is not a broken promise - but it is worth knowing about rather than
# discovering from a consumer, so the sizes are printed and, under GitHub
# Actions, written to the step summary.  ETL moved sizeof(etl::vector) by 8
# bytes between the 20.40.0 and 20.40.1 tags with no interface change: every
# version above and below compiles clean and passes, and only the numbers move.
#
# Needs git, cmake and a compiler.  FMS_FETCH_DEPS is one switch for every
# dependency, so supplying ETL means supplying yaml-cpp and doctest too; all
# three are installed at the versions CMakeLists.txt pins, and only ETL varies.
# Everything it produces stays in the work directory, which is emptied for the
# version under test first.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The pins, read from where they are declared rather than written out again.
pinned() {   # pinned <FetchContent name>
  sed -n "/FetchContent_Declare($1\$/,/)/s/^ *GIT_TAG *\([^ ]*\).*/\1/p" \
    "${root}/CMakeLists.txt" | head -1
}
pinned_etl() { pinned etl; }

# --- the matrix lists the pin ------------------------------------------------
# A range the project does not itself build against is a claim about versions
# nothing here compiles.  The pin is in CMakeLists.txt and the matrix is in
# ci.yml, so nothing makes them agree except this.
if [ "${1:-}" = "--check" ]; then
  pin="$(pinned_etl)"
  workflow="${root}/.github/workflows/ci.yml"

  if [ -z "${pin}" ]; then
    echo "etl_range_check: no GIT_TAG found in the etl FetchContent_Declare." >&2
    exit 1
  fi

  # The matrix entries, read out of the etl-range job's etl list.
  matrix="$(sed -n '/^  etl-range:/,/^  [a-z]/p' "${workflow}" \
            | sed -n '/etl: \[/,/\]/p' | tr -d ' \n' | tr ',' '\n' \
            | sed "s/.*\[//;s/\].*//;s/'//g" | grep -Fxc "${pin}" || true)"

  if [ "${matrix}" -ne 1 ]; then
    echo "etl_range_check: CMakeLists.txt pins ETL ${pin}, and the etl-range" >&2
    echo "                 matrix in ci.yml does not list it." >&2
    echo "                 The range would be a claim about versions this" >&2
    echo "                 project never builds against." >&2
    exit 1
  fi
  echo "etl_range_check: the etl-range matrix lists the pinned ETL ${pin}"
  exit 0
fi

version="${1:?usage: tools/etl_range_check.sh <etl-version> [work-dir]}"

# The version becomes a path component that is deleted, so it is checked before
# it is used rather than trusted: a "version" holding a slash or a .. would
# empty a directory this script never meant to touch.
if ! [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "etl_range_check: ETL version ${version} is not MAJOR.MINOR.PATCH." >&2
  echo "                 It names a git tag and a directory this script" >&2
  echo "                 removes, so it is required to be one." >&2
  exit 1
fi

work="${2:-${root}/build-etl-range}/${version}"

rm -rf "${work}"
mkdir -p "${work}"

run() {   # run <log> <command...>
  local log="$1"; shift
  if ! "$@" > "${log}" 2>&1; then
    echo "etl_range_check: failed: $*" >&2
    cat "${log}" >&2
    return 1
  fi
}

# --- ETL at the requested version --------------------------------------------
echo "etl_range_check: ETL ${version}"
git clone --quiet --depth 1 --branch "${version}" \
  https://github.com/ETLCPP/etl.git "${work}/etl-src"

run "${work}/etl-configure.log" \
  cmake -S "${work}/etl-src" -B "${work}/etl-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${work}/prefix" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
run "${work}/etl-install.log" cmake --install "${work}/etl-build"

# Older ETL tags install a different tree, and cmake --install reports success
# having written no headers.  Said here, because the next line would otherwise
# fail on a missing file and name awk rather than the version that has no
# prefix to read.
if [ ! -f "${work}/prefix/include/etl/version.h" ]; then
  echo "etl_range_check: ETL ${version} installed no include/etl/version.h." >&2
  echo "                 This gate supplies ETL through find_package, which" >&2
  echo "                 needs the installed layout that 20.39.0 and later" >&2
  echo "                 write.  See ${work}/etl-install.log." >&2
  exit 1
fi

# The tag and the header disagree on at least one ETL tag: 20.40.1 ships a
# version.h reading 20.41.1.  find_package compares the header, so report both
# rather than assuming the tag is the version a consumer's constraint will see.
header_version="$(awk '
  /define ETL_VERSION_MAJOR/ {M=$3} /define ETL_VERSION_MINOR/ {m=$3}
  /define ETL_VERSION_PATCH/ {p=$3} END {print M"."m"."p}
' "${work}/prefix/include/etl/version.h")"

if [ "${header_version}" != "${version}" ]; then
  echo "etl_range_check: tag ${version} ships etl/version.h reading ${header_version}"
fi

# --- the other dependencies, at the versions CMakeLists.txt pins -------------
# FMS_FETCH_DEPS=OFF is what puts find_package(etl REQUIRED) under test, and it
# turns off fetching for every dependency rather than only ETL.  These two are
# installed at their pins so that ETL is the one thing that varies.
yaml_cpp_version="$(pinned yaml-cpp)"
doctest_version="$(pinned doctest)"
if [ -z "${yaml_cpp_version}" ] || [ -z "${doctest_version}" ]; then
  echo "etl_range_check: no GIT_TAG for yaml-cpp or doctest in CMakeLists.txt." >&2
  exit 1
fi

install_pinned() {   # install_pinned <name> <url> <tag> [cmake args...]
  local name="$1" url="$2" tag="$3"; shift 3
  git clone --quiet --depth 1 --branch "${tag}" "${url}" "${work}/${name}-src"
  run "${work}/${name}-configure.log" \
    cmake -S "${work}/${name}-src" -B "${work}/${name}-build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${work}/prefix" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      "$@"
  run "${work}/${name}-build.log" cmake --build "${work}/${name}-build" --parallel
  run "${work}/${name}-install.log" cmake --install "${work}/${name}-build"
}

echo "etl_range_check: yaml-cpp ${yaml_cpp_version} and doctest ${doctest_version}, both pinned"
install_pinned yaml-cpp https://github.com/jbeder/yaml-cpp.git "${yaml_cpp_version}" \
  -DYAML_CPP_BUILD_TESTS=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_BUILD_SHARED_LIBS=OFF
install_pinned doctest https://github.com/doctest/doctest.git "${doctest_version}" \
  -DDOCTEST_WITH_TESTS=OFF

# --- the library, taking the supplied ETL ------------------------------------
run "${work}/fms-configure.log" \
  cmake -S "${root}" -B "${work}/fms-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFMS_WARNINGS_AS_ERRORS=ON \
    -DFMS_FETCH_DEPS=OFF \
    -DCMAKE_PREFIX_PATH="${work}/prefix"

# Passing the flag is not the same as the flag having worked.  Without this the
# gate ran the whole matrix against the pinned ETL - FetchContent supplied it,
# every leg passed, and only the sizes program below saw the version under test,
# so the table looked right while the build and the suite tested one version
# eight times.  The resolved package directory is what says which ETL the
# library was actually compiled against.
resolved="$(sed -n 's/^etl_DIR:PATH=//p' "${work}/fms-build/CMakeCache.txt" | head -1)"
case "${resolved}" in
  "${work}/prefix"/*) ;;
  *)
    echo "etl_range_check: the build did not use the ETL supplied to it." >&2
    echo "                 asked for ${version} at ${work}/prefix," >&2
    echo "                 find_package resolved ${resolved:-nothing}." >&2
    echo "                 A leg that falls back to the pin tests the pin." >&2
    exit 1 ;;
esac

run "${work}/fms-build.log" cmake --build "${work}/fms-build" --parallel

ctest --test-dir "${work}/fms-build" --output-on-failure

# --- what moved ---------------------------------------------------------------
# Reported, never asserted.  The sizes are read out of a program built against
# the ETL under test rather than computed here.
cat > "${work}/sizes.cpp" <<'CPP'
#include <cstdio>
#include <etl/version.h>
#include <etl/vector.h>
#include <fms/args.hpp>
#include <fms/model.hpp>
#include <fms/runtime.hpp>
#include <fms/setup.hpp>
int main() {
  std::printf("| %d.%d.%d | %zu | %zu | %zu | %zu | %zu |\n",
              ETL_VERSION_MAJOR, ETL_VERSION_MINOR, ETL_VERSION_PATCH,
              sizeof(etl::vector<int, 8>), sizeof(fms::Model),
              sizeof(fms::Setup), sizeof(fms::Args), sizeof(fms::Runtime));
  return 0;
}
CPP

run "${work}/sizes-build.log" \
  "${CXX:-c++}" -std=c++17 -O2 \
    -I "${root}/include" -I "${work}/prefix/include" \
    "${work}/sizes.cpp" -o "${work}/sizes"

# Both labels.  The sizes program can only report the version its headers
# declare, and on one tag that is not the tag: 20.40.1 ships an etl/version.h
# reading 20.41.1.  A row carrying the header alone leaves the table with no
# 20.40.1 in it while the matrix leg is named 20.40.1, and a reader of the table
# concludes the layout moved somewhere it did not.
heading="| tag | etl | vector<int,8> | Model | Setup | Args | Runtime |"
rule="|---|---|---:|---:|---:|---:|---:|"
row="| ${version} $("${work}/sizes")"

echo "etl_range_check: ${heading}"
echo "etl_range_check: ${row}"

# GITHUB_STEP_SUMMARY is per job and every matrix leg is its own job, so this is
# one complete one-row table per leg rather than rows accumulating into one -
# hence the heading, without which the row renders as a line of pipes.
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "${heading}"
    echo "${rule}"
    echo "${row}"
  } >> "${GITHUB_STEP_SUMMARY}"
fi

echo "etl_range_check: ok - ETL ${version} builds, tests and reports its sizes"
