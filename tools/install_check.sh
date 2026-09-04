#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install the library, then build something against the installed package.
#
#   tools/install_check.sh [work-dir]    # default: build-install
#
# find_package(fms_yaml) is the supported way to consume this project, so it
# needs a gate of its own: the unit tests all run inside the build tree, where
# a missing install rule, an unexported target or a dependency the package
# forgets to ask for cannot show up.
#
# Installing requires -DFMS_FETCH_DEPS=OFF, because a dependency FetchContent
# built inside the build tree cannot be exported with it.  So this also builds
# and installs ETL and yaml-cpp at the pinned versions, into the same prefix,
# and is the only place the FMS_FETCH_DEPS=OFF path is exercised.
#
# Needs git, cmake and a compiler.  Everything it produces - the dependency
# sources, the build trees, their logs, and the install prefixes at
# <work-dir>/prefix, <work-dir>/prefix-tuned and <work-dir>/prefix-relabelled -
# stays inside the work directory.  The prefixes are emptied first: a header
# left behind by an earlier run would satisfy the consumer's #include after the
# install rules stopped shipping it.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${1:-${root}/build-install}"
prefix="${work}/prefix"

ETL_VERSION='20.39.4'
YAML_CPP_VERSION='0.8.0'

mkdir -p "${work}"

# Run a step, and show its output when it fails.  Without this the logs stay in
# files that a CI runner discards, so the job reports a banner line and nothing
# else - the failure cannot be read without reproducing it.
run() {   # run <log> <command...>
  local log="$1"; shift
  if ! "$@" > "${log}" 2>&1; then
    echo "install_check: failed: $*" >&2
    cat "${log}" >&2
    return 1
  fi
}

# --- the dependencies, at the versions CMakeLists.txt pins -------------------
install_dependency() {   # install_dependency <name> <url> <tag> [cmake args...]
  local name="$1" url="$2" tag="$3"; shift 3
  local src="${work}/${name}-src"
  local stamp="${src}/.pinned-version"

  # The stamp records which tag the checkout holds, and is written only after
  # the clone succeeds.  Reusing a directory on existence alone would build the
  # old version after a pin was bumped - reporting ok for a version never
  # tested - and would keep reusing a clone that was interrupted.
  if [ ! -f "${stamp}" ] || [ "$(cat "${stamp}")" != "${tag}" ]; then
    rm -rf "${src}"
    git clone --quiet --depth 1 --branch "${tag}" "${url}" "${src}"
    echo "${tag}" > "${stamp}"
  fi

  run "${work}/${name}-configure.log" \
    cmake -S "${src}" -B "${work}/${name}-build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${prefix}" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      "$@"
  run "${work}/${name}-build.log" cmake --build "${work}/${name}-build" --parallel
  run "${work}/${name}-install.log" cmake --install "${work}/${name}-build"
}

rm -rf "${prefix}" "${work}/prefix-tuned" "${work}/prefix-relabelled"

echo "install_check: ETL ${ETL_VERSION} and yaml-cpp ${YAML_CPP_VERSION} -> ${prefix}"
install_dependency etl https://github.com/ETLCPP/etl.git "${ETL_VERSION}"
install_dependency yaml-cpp https://github.com/jbeder/yaml-cpp.git "${YAML_CPP_VERSION}" \
  -DYAML_CPP_BUILD_TESTS=OFF \
  -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_BUILD_CONTRIB=OFF \
  -DYAML_BUILD_SHARED_LIBS=OFF

# --- the library --------------------------------------------------------------
echo "install_check: fms-yaml -> ${prefix}"
cmake -S "${root}" -B "${work}/fms-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_PREFIX_PATH="${prefix}" \
  -DFMS_FETCH_DEPS=OFF \
  -DFMS_INSTALL=ON \
  -DFMS_BUILD_TESTS=OFF \
  -DFMS_BUILD_EXAMPLES=OFF
cmake --build "${work}/fms-build" --parallel
cmake --install "${work}/fms-build"

# --- something that knows the library only through find_package ---------------
echo "install_check: building tests/consumer against the installed package"
cmake -S "${root}/tests/consumer" -B "${work}/consumer-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${prefix}"
cmake --build "${work}/consumer-build" --parallel

report="$("${work}/consumer-build/consumer" \
  "${root}/examples/car/car.setup.yaml" \
  "${root}/examples/car/car.machine.yaml")"
echo "${report}"

# --- and again with a capacity changed ----------------------------------------
# The package is supposed to carry the capacities the library was built with, so
# that a consumer inherits them rather than having to know they exist.  Nothing
# proves that unless one of them is not the default: install a second prefix
# with FMS_MAX_ARGUMENTS=6 and read the tag back out of the consumer.
tuned="${work}/prefix-tuned"
echo "install_check: installing again with FMS_MAX_ARGUMENTS=6"
run "${work}/fms-tuned-configure.log" \
  cmake -S "${root}" -B "${work}/fms-tuned-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${tuned}" \
    -DCMAKE_PREFIX_PATH="${prefix}" \
    -DFMS_FETCH_DEPS=OFF \
    -DFMS_INSTALL=ON \
    -DFMS_BUILD_TESTS=OFF \
    -DFMS_BUILD_EXAMPLES=OFF \
    -DFMS_MAX_ARGUMENTS=6
run "${work}/fms-tuned-build.log" cmake --build "${work}/fms-tuned-build" --parallel
run "${work}/fms-tuned-install.log" cmake --install "${work}/fms-tuned-build"

run "${work}/consumer-tuned-configure.log" \
  cmake -S "${root}/tests/consumer" -B "${work}/consumer-tuned-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${tuned};${prefix}"
run "${work}/consumer-tuned-build.log" \
  cmake --build "${work}/consumer-tuned-build" --parallel

tuned_report="$("${work}/consumer-tuned-build/consumer" \
  "${root}/examples/car/car.setup.yaml" \
  "${root}/examples/car/car.machine.yaml")"
echo "${tuned_report}"

# --- what find_package refuses ---------------------------------------------
# The compatibility mode is a promise docs/stability.md makes in prose: which
# versions a consumer may ask for and be given this install.  tests/consumer
# asks for no version at all, so nothing here tested it until this was added -
# changing the mode would have left the page quietly false.
#
# Which rule applies depends on the version project() declares, because the
# meaning of the numbers changes at 1.0.  Both are asserted here rather than
# one being written out, so the release that crosses over is checked by the same
# gate on both sides of it.
version="$(sed -n 's/^project(fms_yaml VERSION \([0-9.]*\).*/\1/p' "${root}/CMakeLists.txt")"

# project() accepts up to four components, and every rule below reads exactly
# three.  A tweak component reaches $((patch + 1)) as "0.1" and stops the run
# forty lines later on an unset variable, naming a bash operator rather than the
# version.  The shape is checked where it is read instead.
if ! [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "install_check: CMakeLists.txt declares VERSION ${version:-<none>}." >&2
  echo "               This gate derives the compatibility rule from" >&2
  echo "               MAJOR.MINOR.PATCH and has no meaning for a version" >&2
  echo "               of another shape." >&2
  exit 1
fi

major="${version%%.*}"
rest="${version#*.}"
minor="${rest%%.*}"
patch="${rest#*.}"

mkdir -p "${work}/version-probe"
cat > "${work}/version-probe/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(fms_version_probe LANGUAGES NONE)
find_package(fms_yaml ${FMS_REQUESTED} REQUIRED)
CMAKE

if [ "${major}" -eq 0 ]; then
  # SameMinorVersion, which is what 0.x is configured with: the minor is the
  # breaking number, so the previous and next one are both refused.
  rule="SameMinorVersion"
  accepted="${version} ${major}.${minor}"
  refused="${major}.$((minor + 1)) $((major + 1)).0 ${major}.${minor}.$((patch + 1))"
  # Only when there is a previous minor: 0.0.x would otherwise ask for "0.-1",
  # which tests the parser rather than the compatibility rule.
  if [ "${minor}" -gt 0 ]; then
    refused="${refused} ${major}.$((minor - 1)) ${major}.$((minor - 1)).9"
  fi
else
  # SameMajorVersion: an older minor within this major is compatible and is
  # accepted, which is the whole difference between the two rules.
  rule="SameMajorVersion"
  accepted="${version} ${major}.${minor}"
  refused="$((major + 1)).0 ${major}.$((minor + 1)) ${major}.${minor}.$((patch + 1))"
  # The first minor of this major, and the one below the current minor.  They
  # are the same string at minor 1, so the second is added above it only; asking
  # twice costs a configure and reads as two checks where there is one.
  if [ "${minor}" -gt 0 ]; then
    accepted="${accepted} ${major}.0"
  fi
  if [ "${minor}" -gt 1 ]; then
    accepted="${accepted} ${major}.$((minor - 1))"
  fi
fi

# probe_prefix and probe_version name the install under test.  They default to
# the one just built; the relabelled probe below sets both, so that a failure
# says which of the two prefixes it asked and what that one reports as its
# version, and so that the two runs do not overwrite each other's logs.
check_find_package() {   # check_find_package <requested> <accepted|refused>
  local requested="$1" want="$2" outcome
  local against="${probe_prefix:-${prefix}}"
  local installed="${probe_version:-${version}}"
  local log="${work}/version-${requested}${probe_version:+-as-${probe_version}}.log"

  if cmake -S "${work}/version-probe" -B "${work}/version-probe/build" \
       -DCMAKE_PREFIX_PATH="${against}" \
       -DFMS_REQUESTED="${requested}" > "${log}" 2>&1; then
    outcome=accepted
  else
    outcome=refused
  fi
  rm -rf "${work}/version-probe/build"

  if [ "${outcome}" != "${want}" ]; then
    echo "install_check: find_package(fms_yaml ${requested}) was ${outcome}," >&2
    echo "               and ${installed} is installed - expected ${want}." >&2
    echo "               COMPATIBILITY is what decides this; ${installed} means" >&2
    echo "               ${rule}, which is what docs/stability.md describes." >&2
    return 1
  fi
  printf '  find_package(fms_yaml %-9s -> %s\n' "${requested})" "${outcome}"
}

# What find_package does cannot separate the two rules at a .0 version: they
# differ only over an older minor within the same major, and 1.0.0 has none.  So
# the declared mode is compared with the one the version means, which is a check
# that says something at 1.0.0 as well as after it.
declared_rule="$(sed -n 's/^ *COMPATIBILITY \([A-Za-z]*\)) *$/\1/p' "${root}/CMakeLists.txt")"
if [ "${declared_rule}" != "${rule}" ]; then
  echo "install_check: CMakeLists.txt declares COMPATIBILITY ${declared_rule}," >&2
  echo "               and ${version} means ${rule}." >&2
  echo "               docs/stability.md describes the second." >&2
  exit 1
fi
echo "install_check: COMPATIBILITY ${declared_rule}, which is what ${version} means"

echo "install_check: what find_package accepts against ${version} (${rule})"
for requested in ${accepted}; do
  check_find_package "${requested}" accepted
done
for requested in ${refused}; do
  check_find_package "${requested}" refused
done

# At a .0 version the probes above cannot separate the two rules - they differ
# only over an older minor within the same major, and a .0 version has none - so
# what decides the answer, fms_yamlConfigVersion.cmake, is never observed and
# the promise rests on the declared_rule comparison above.  A copy of that
# installed file relabelled to the next minor supplies the missing case: the
# rule logic is the file this build generated, and only the version it reports
# changes.  The first request is the discriminating one, accepted under
# SameMajorVersion and refused under SameMinorVersion.
if [ "${major}" -gt 0 ] && [ "${minor}" -eq 0 ]; then
  relabelled="${work}/prefix-relabelled"
  rm -rf "${relabelled}"
  cp -a "${prefix}" "${relabelled}"

  mapfile -t version_files < <(find "${relabelled}" -name fms_yamlConfigVersion.cmake)
  if [ "${#version_files[@]}" -ne 1 ]; then
    echo "install_check: expected one fms_yamlConfigVersion.cmake under" >&2
    echo "               ${relabelled}, found ${#version_files[@]}." >&2
    exit 1
  fi
  sed -i "s/${major}\.${minor}\.${patch}/${major}.1.0/g" "${version_files[0]}"

  echo "install_check: the same file relabelled ${major}.1.0, where the rules differ"
  probe_prefix="${relabelled}" probe_version="${major}.1.0" \
    check_find_package "${major}.${minor}" accepted
  probe_prefix="${relabelled}" probe_version="${major}.1.0" \
    check_find_package "${major}.1" accepted
  probe_prefix="${relabelled}" probe_version="${major}.1.0" \
    check_find_package "$((major + 1)).0" refused
fi

tag_of() { echo "$1" | sed -n 's/.*(capacities \([0-9_]*\)).*/\1/p'; }
default_tag="$(tag_of "${report}")"
tuned_tag="$(tag_of "${tuned_report}")"
arguments_field="$(echo "${tuned_tag}" | cut -d_ -f7)"

if [ "${default_tag}" = "${tuned_tag}" ] || [ "${arguments_field}" != "6" ]; then
  echo "install_check: the package did not carry its capacities to the consumer." >&2
  echo "               default ${default_tag}, tuned ${tuned_tag}," >&2
  echo "               expected the 7th field (arguments) to be 6." >&2
  exit 1
fi

echo "install_check: ok - capacities reached the consumer (${default_tag} -> ${tuned_tag})"
