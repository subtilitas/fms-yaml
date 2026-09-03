#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install the library, then build something against the installed package.
#
#   tools/install_check.sh [prefix]      # default: build-install
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
# Needs git, cmake and a compiler.  Nothing is written outside the prefix.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${1:-${root}/build-install}"
prefix="${work}/prefix"

ETL_VERSION='20.39.4'
YAML_CPP_VERSION='0.8.0'

mkdir -p "${work}"

# --- the dependencies, at the versions CMakeLists.txt pins -------------------
install_dependency() {   # install_dependency <name> <url> <tag> [cmake args...]
  local name="$1" url="$2" tag="$3"; shift 3
  local src="${work}/${name}-src"

  if [ ! -d "${src}" ]; then
    git clone --quiet --depth 1 --branch "${tag}" "${url}" "${src}"
  fi

  cmake -S "${src}" -B "${work}/${name}-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${prefix}" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "$@" > "${work}/${name}-configure.log" 2>&1
  cmake --build "${work}/${name}-build" --parallel > "${work}/${name}-build.log" 2>&1
  cmake --install "${work}/${name}-build" > "${work}/${name}-install.log" 2>&1
}

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

"${work}/consumer-build/consumer" \
  "${root}/examples/car/car.setup.yaml" \
  "${root}/examples/car/car.machine.yaml"

echo "install_check: ok"
