#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Say whether ETL has published a version newer than the one this project pins.
#
#   tools/etl_latest_check.sh
#
# The pin is in CMakeLists.txt and is what every gate but etl-range builds
# against.  Nothing here notices when it falls behind, and a pin nobody revisits
# is how a project ends up several years back with no decision ever having been
# made.  This is the noticing; moving the pin stays a decision.
#
# Exit status is the answer, so a caller can act on it:
#
#   0   the pin is the newest tag
#   1   something went wrong - no pin found, or the tags could not be read
#   2   ETL has newer tags, which are listed
#
# Compared by tag, because GIT_TAG is what CMakeLists.txt pins and what a bump
# would edit.  The tags and the versions inside them disagree on exactly one
# release - 20.40.1 ships an etl/version.h reading 20.41.1 - so the two axes are
# not interchangeable; tools/etl_range_check.sh reports both per version.
#
# Needs git.  Reads the network.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

pin="$(sed -n '/FetchContent_Declare(etl$/,/)/s/^ *GIT_TAG *\([0-9][0-9.]*\).*/\1/p' \
       "${root}/CMakeLists.txt" | head -1)"
if [ -z "${pin}" ]; then
  echo "etl_latest: no GIT_TAG found in the etl FetchContent_Declare." >&2
  exit 1
fi

# Only three-component tags: ETL also carries other refs, and a release is the
# only thing this can sensibly compare against.
tags="$(git ls-remote --tags --refs https://github.com/ETLCPP/etl.git 2>/dev/null \
        | sed 's|.*refs/tags/||' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' || true)"
if [ -z "${tags}" ]; then
  echo "etl_latest: could not read tags from ETLCPP/etl." >&2
  exit 1
fi

if ! grep -qFx "${pin}" <<< "${tags}"; then
  echo "etl_latest: CMakeLists.txt pins ETL ${pin}, which is not a tag of" >&2
  echo "            ETLCPP/etl.  A pin that names nothing cannot be compared." >&2
  exit 1
fi

newest="$(sort -V <<< "${tags}" | tail -1)"

if [ "${pin}" = "${newest}" ]; then
  echo "etl_latest: ETL ${pin} is pinned and is the newest tag"
  exit 0
fi

# How far behind, not only that it is behind: a pin one patch back and a pin two
# years back are different decisions.
newer="$(sort -V <<< "${tags}" | sed -n "/^${pin}\$/,\$p" | tail -n +2)"
count="$(wc -l <<< "${newer}")"

echo "etl_latest: ETL ${pin} is pinned and ${newest} is the newest tag"
echo "etl_latest: ${count} newer, most recent first:"
sort -rV <<< "${newer}" | head -10 | sed 's/^/  /'
if [ "${count}" -gt 10 ]; then
  echo "  ... and $((count - 10)) older than those, back to the pin"
fi
exit 2
