#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the type sizes the documentation quotes against the ones this tree makes.

    python3 tools/doc_figures_check.py [build-dir]     # default: build

docs/architecture.md quotes seven type sizes and then uses two of them again to
work through what an ODR mismatch costs; include/fms/abi.hpp and
tools/abi_guard_check.sh repeat that pair in their own comments.  All of them
are correct for the pinned ETL at the default capacities, and nothing checked
any of them.  ETL moved sizeof(etl::vector) by 8 bytes between its 20.40.0 and
20.40.1 tags, which takes sizeof(fms::Model) from 49 728 to 47 376, so a pin
that moved would leave every one of those pages describing a build nobody
makes.

The figures are measured, never listed here.  A gate holding its own copy of
the numbers is one more place for them to be wrong.

What this does not check: the sizes docs/testing.md and docs/stability.md quote
for a different ETL.  Those describe a build this one is not, and
tools/etl_range_check.sh reports them per version.

Reads build-dir/abi_probe.env, which the build writes, for the compiler and
ETL's include directory.  GCC and Clang only, and x86-64: the pages say x86-64
and the numbers are ABI-specific.
"""

from __future__ import annotations

import pathlib
import platform
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The configuration docs/architecture.md names when it works through a
# mismatch.  Written once here because it is the example's subject, not a
# figure: the sizes it produces are measured like every other.
TUNED = ["-DFMS_MAX_STATES=8", "-DFMS_MAX_TRIGGERS=12"]

PROBE = """
#include <cstddef>
#include <cstdio>

#include <fms/args.hpp>
#include <fms/model.hpp>
#include <fms/runtime.hpp>
#include <fms/setup.hpp>

int main() {
  std::printf("fms::Model %zu\\n", sizeof(fms::Model));
  std::printf("fms::StateNode %zu\\n", sizeof(fms::StateNode));
  std::printf("fms::Condition %zu\\n", sizeof(fms::Condition));
  std::printf("fms::Alternative %zu\\n", sizeof(fms::Alternative));
  std::printf("fms::Setup %zu\\n", sizeof(fms::Setup));
  std::printf("fms::Args %zu\\n", sizeof(fms::Args));
  std::printf("fms::Runtime %zu\\n", sizeof(fms::Runtime));
  return 0;
}
"""

# Every sentence outside the table that carries one of these numbers.  Each
# names the file, what it is saying, the pattern that captures the number, and
# which measurement it should equal.  A sentence that stops matching is a
# failure rather than a skip: silently checking nothing is how this rots.
SENTENCES = [
    ("docs/architecture.md", "sizeof(fms::Model) at the defaults",
     r"sizeof\(fms::Model\)\s+([\d ]+) B\s+defaults", "default"),
    ("docs/architecture.md", "sizeof(fms::Model) when tuned",
     r"sizeof\(fms::Model\)\s+([\d ]+) B\s+FMS_MAX_STATES=8", "tuned"),
    ("docs/architecture.md", "the bytes the library writes",
     r"library then writes ([\d ]+) bytes", "default"),
    ("docs/architecture.md", "the bytes the caller reserved",
     r"an object the caller reserved ([\d ]+)", "tuned"),
    ("include/fms/abi.hpp", "sizeof(fms::Model) at the defaults",
     r"with the defaults sizeof\(fms::Model\) is (\d+)", "default"),
    ("include/fms/abi.hpp", "sizeof(fms::Model) when tuned",
     r"FMS_MAX_TRIGGERS=12 it is (\d+)", "tuned"),
    ("include/fms/abi.hpp", "the bytes the library writes",
     r"library then writes (\d+) bytes", "default"),
    ("include/fms/abi.hpp", "the bytes the caller allocated",
     r"allocated (\d+) for", "tuned"),
    ("tools/abi_guard_check.sh", "sizeof(fms::Model) at the defaults",
     r"sizeof\(fms::Model\) is (\d+) with", "default"),
    ("tools/abi_guard_check.sh", "sizeof(fms::Model) when tuned",
     r"the defaults and (\d+) with FMS_MAX_STATES=8", "tuned"),
]

TABLE_ROW = re.compile(r"^\| `(fms::[A-Za-z]+)` \| ([\d ]+) B \|")


def grouped(value: int) -> str:
    """49728 -> '49 728', the way the prose spells it."""
    return f"{value:,}".replace(",", " ")


def read_env(path: pathlib.Path) -> dict[str, str]:
    """Read build-dir/abi_probe.env without executing it."""
    settings = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r'([A-Z_]+)="(.*)"', line)
        if match:
            settings[match.group(1)] = match.group(2)
    return settings


def measure(env: dict[str, str], work: pathlib.Path, flags: list[str]) -> dict[str, int]:
    source = work / "sizes.cpp"
    source.write_text(PROBE)
    binary = work / f"sizes{len(flags)}"
    includes = []
    for directory in env.get("ETL_INCLUDE", "").split(";"):
        if directory:
            includes += ["-isystem", directory]
    subprocess.run(
        [env["CXX"], "-std=c++17", "-O2", "-fno-exceptions", "-DETL_LOG_ERRORS",
         "-I", env["FMS_INCLUDE"], *includes, *flags, str(source), "-o", str(binary)],
        check=True, capture_output=True,
    )
    out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    return {name: int(size) for name, size in (line.split() for line in out.splitlines())}


def main() -> int:
    if platform.machine() != "x86_64":
        print(f"doc_figures: the documented sizes say x86-64 and this is "
              f"{platform.machine()}.", file=sys.stderr)
        print("             The numbers are ABI-specific, so comparing them",
              file=sys.stderr)
        print("             here would test the machine rather than the tree.",
              file=sys.stderr)
        return 1

    build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build")
    env_file = build / "abi_probe.env"
    if not env_file.is_file():
        print(f"doc_figures: no {env_file} - configure with -DFMS_BUILD_TESTS=ON first",
              file=sys.stderr)
        return 1
    env = read_env(env_file)
    for required in ("CXX", "FMS_INCLUDE"):
        if not env.get(required):
            print(f"doc_figures: {env_file} has no {required}", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        sizes = {
            "default": measure(env, work, []),
            "tuned": measure(env, work, TUNED),
        }

    failures = []

    # The table, every row of it.  A row nobody checks is the one that rots.
    architecture = (ROOT / "docs/architecture.md").read_text()
    rows = 0
    for line in architecture.splitlines():
        match = TABLE_ROW.match(line)
        if not match:
            continue
        name, documented = match.group(1), int(match.group(2).replace(" ", ""))
        if name not in sizes["default"]:
            failures.append(f"docs/architecture.md has a row for {name}, which this "
                            f"gate does not measure - add it to PROBE")
            continue
        rows += 1
        actual = sizes["default"][name]
        if documented != actual:
            failures.append(f"docs/architecture.md says {name} is "
                            f"{grouped(documented)} B, and this tree makes it "
                            f"{grouped(actual)} B")
    if rows == 0:
        print("doc_figures: no type-size rows found in docs/architecture.md - the "
              "table this gate reads has changed shape.", file=sys.stderr)
        return 1

    for filename, what, pattern, which in SENTENCES:
        text = (ROOT / filename).read_text()
        match = re.search(pattern, text)
        if not match:
            failures.append(f"{filename} no longer contains {what}; this gate reads "
                            f"that sentence and it has moved or changed")
            continue
        documented = int(match.group(1).replace(" ", ""))
        actual = sizes[which]["fms::Model"]
        if documented != actual:
            failures.append(f"{filename} says {what} is {grouped(documented)}, "
                            f"and this tree makes it {grouped(actual)}")

    if failures:
        for failure in failures:
            print(f"doc_figures: {failure}", file=sys.stderr)
        print("doc_figures: the pages describe a build this tree does not make.",
              file=sys.stderr)
        print("             ETL and the capacities decide these numbers; find "
              "which moved", file=sys.stderr)
        print("             before editing the pages.", file=sys.stderr)
        return 1

    print(f"doc_figures: ok - {rows} table rows and {len(SENTENCES)} sentences agree "
          f"with this tree (Model {grouped(sizes['default']['fms::Model'])} B, "
          f"tuned {grouped(sizes['tuned']['fms::Model'])} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
