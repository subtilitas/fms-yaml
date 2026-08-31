#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep the README's state diagram equal to the one the machine file describes.

A diagram drawn by hand beside a configuration is a second description of the
same behaviour, and the moment the YAML changes it becomes a wrong one that
still looks authoritative.  So the README's diagram is generated: `car_console
--export mermaid` renders the loaded Model, and this script splices the result
into the marked block.

The default is --check, not --write.  A file that rewrites itself is one nobody
reads the diff of - and the failure this guards against is a diagram that no
longer matches the machine, which is exactly the diff worth reading.  CI runs
the check (as the `car_diagram_check` test); a person who changed the machine
runs --write and commits the picture along with the change that caused it.

Usage:
    python3 tools/diagram_sync.py                     # check, against build/car_console
    python3 tools/diagram_sync.py --write             # regenerate the block
    python3 tools/diagram_sync.py --binary path/to/car_console --check

Exit status is 0 when the README agrees with the binary, 1 when it does not.
"""

from __future__ import annotations

import argparse
import difflib
import pathlib
import subprocess
import sys

BEGIN = "<!-- diagram:begin -->"
END = "<!-- diagram:end -->"

FOOTER = (
    "<sub>Generated from the machine file by `tools/diagram_sync.py`, and checked by the\n"
    "`car_diagram_check` test.  Change the YAML, then run "
    "`python3 tools/diagram_sync.py --write`.</sub>"
)


def generate(binary: pathlib.Path, setup: pathlib.Path, machine: pathlib.Path) -> str:
    """Ask the binary what the machine looks like."""
    if not binary.exists():
        raise SystemExit(
            f"{binary}: not built.  Build the tree first, or pass --binary."
        )
    command = [str(binary), str(setup), str(machine), "--export", "mermaid"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"{binary} --export failed ({result.returncode}):\n{result.stderr}"
        )
    # The binary is the only thing that renders a diagram; a build on Windows
    # hands back CRLF, and the README is committed with LF.
    return result.stdout.replace("\r\n", "\n").strip("\n")


def render_block(diagram: str) -> str:
    return f"```mermaid\n{diagram}\n```\n\n{FOOTER}"


def current_block(readme: pathlib.Path) -> tuple[str, str, str]:
    """The README split into what comes before the block, the block, and after."""
    text = readme.read_text(encoding="utf-8").replace("\r\n", "\n")
    start = text.find(BEGIN)
    end = text.find(END)
    if start == -1 or end == -1 or end < start:
        raise SystemExit(
            f"{readme}: could not find the '{BEGIN}' / '{END}' markers. "
            "Add them where the diagram belongs."
        )
    head = text[: start + len(BEGIN)] + "\n"
    return head, text[start + len(BEGIN) + 1 : end], text[end:]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=pathlib.Path, default=pathlib.Path("build/car_console"))
    ap.add_argument("--setup", type=pathlib.Path,
                    default=pathlib.Path("examples/car/car.setup.yaml"))
    ap.add_argument("--machine", type=pathlib.Path,
                    default=pathlib.Path("examples/car/car.machine.yaml"))
    ap.add_argument("--readme", type=pathlib.Path, default=pathlib.Path("README.md"))
    ap.add_argument("--write", action="store_true",
                    help="rewrite the block instead of reporting on it")
    ap.add_argument("--check", action="store_true",
                    help="the default; kept so the intent can be spelled out")
    args = ap.parse_args(argv)

    wanted = render_block(generate(args.binary, args.setup, args.machine)) + "\n"
    head, block, tail = current_block(args.readme)

    if block == wanted:
        print(f"{args.readme}: the diagram matches the machine")
        return 0

    if not args.write:
        diff = difflib.unified_diff(
            block.splitlines(keepends=True), wanted.splitlines(keepends=True),
            fromfile=f"{args.readme} (committed)", tofile="generated from the machine file",
        )
        sys.stdout.writelines(diff)
        print(
            f"\n{args.readme}: the diagram no longer matches the machine.\n"
            "Run: python3 tools/diagram_sync.py --write",
            file=sys.stderr,
        )
        return 1

    args.readme.write_text(head + wanted + tail, encoding="utf-8", newline="\n")
    print(f"{args.readme}: diagram updated")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
