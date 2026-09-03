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
# <work-dir>/prefix and <work-dir>/prefix-tuned - stays inside the work
# directory.  The prefixes are emptied first: a header left behind by an
# earlier run would satisfy the consumer's #include after the install rules
# stopped shipping it.
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

rm -rf "${prefix}" "${work}/prefix-tuned"

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
