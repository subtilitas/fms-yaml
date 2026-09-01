#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarise a gcovr JSON report and enforce this repository's coverage floor.

Reporting coverage is codecov.io's job: it renders the badge, keeps the history
and annotates pull requests.  What it does not do is put the *threshold* in the
tree.  A floor configured in a service is one a reader cannot find, a fork does
not inherit and nobody reviews the change to; this one is an argument in
.github/workflows/ci.yml and a comparison in this file, so moving it is a diff
like any other.

The totals are recomputed here from gcovr's per-file counts rather than read
from its own percentages, so the number this gate tests is the number the
per-file listing adds up to.

Usage:
    gcovr --json-summary-pretty -o coverage.json
    python3 tools/coverage_report.py coverage.json [--fail-under 90]

Exit status is 0 unless --fail-under is given and line coverage is below it.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def pct(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 100.0


def summarise(data: dict) -> dict:
    """gcovr's --json-summary, reduced to the totals and a per-file listing."""
    files = data.get("files", [])
    out: dict = {"files": []}
    for key in ("line", "branch", "function"):
        covered = sum(int(f.get(f"{key}_covered", 0) or 0) for f in files)
        total = sum(int(f.get(f"{key}_total", 0) or 0) for f in files)
        out[key] = {"covered": covered, "total": total, "percent": pct(covered, total)}
    for f in files:
        covered = int(f.get("line_covered", 0) or 0)
        total = int(f.get("line_total", 0) or 0)
        out["files"].append(
            {
                "filename": f.get("filename", "?"),
                "covered": covered,
                "total": total,
                "percent": pct(covered, total),
            }
        )
    out["files"].sort(key=lambda f: (f["percent"], f["filename"]))
    return out


def report(s: dict) -> None:
    """Least-covered file first, which is the one worth a test."""
    for f in s["files"]:
        print(f"  {f['percent']:5.1f}%  {f['covered']:>4}/{f['total']:<4}  {f['filename']}")
    print()
    print(f"lines     {s['line']['covered']}/{s['line']['total']}  "
          f"{s['line']['percent']:.1f}%")
    print(f"branches  {s['branch']['covered']}/{s['branch']['total']}  "
          f"{s['branch']['percent']:.1f}%")
    print(f"functions {s['function']['covered']}/{s['function']['total']}  "
          f"{s['function']['percent']:.1f}%")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("summary", type=pathlib.Path, help="gcovr --json-summary output")
    ap.add_argument("--fail-under", type=float, default=None,
                    help="exit non-zero if line coverage is below this")
    args = ap.parse_args(argv)

    s = summarise(json.loads(args.summary.read_text(encoding="utf-8")))
    report(s)

    line_pct = s["line"]["percent"]
    if args.fail_under is not None and line_pct + 1e-9 < args.fail_under:
        print(f"::error::line coverage {line_pct:.1f}% is below the "
              f"{args.fail_under:.1f}% floor", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
