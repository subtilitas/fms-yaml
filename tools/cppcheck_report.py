#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn a cppcheck XML report into a readable summary, and decide the exit code.

Why this exists rather than `cppcheck --error-exitcode=1`: that flag fails the
build on every finding, including style opinions.  Those change between
cppcheck releases, so a runner picking up a new patch version can turn the tree
red without a line of our code changing.  Severities that mean "this is a
defect" gate the build; the rest are reported and do not.

Usage:
    cppcheck ... --xml --xml-version=2 src include 2> cppcheck.xml
    python3 tools/cppcheck_report.py cppcheck.xml [--gate error,warning]
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import sys
import xml.etree.ElementTree as ET


def load(path: pathlib.Path) -> list[ET.Element] | None:
    """Findings, or None when there is no usable report at all."""
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8", errors="replace").strip()
    if not text:
        return None
    try:
        return ET.fromstring(text).findall(".//error")
    except ET.ParseError:
        return None


def where(finding: ET.Element) -> str:
    location = finding.find("location")
    if location is None:
        return "-"
    line = location.get("line", "0")
    return f"{location.get('file', '?')}:{line}"


def render(by_severity: dict[str, list[ET.Element]], gate: set[str]) -> str:
    out = ["### cppcheck", ""]
    out += ["| Severity | Count | Gates the build |", "| --- | ---: | --- |"]
    for severity in sorted(by_severity):
        gates = "yes" if severity in gate else "no"
        out.append(f"| {severity} | {len(by_severity[severity])} | {gates} |")
    out.append("")

    for severity in sorted(by_severity):
        findings = by_severity[severity]
        out.append(f"<details><summary>{severity} ({len(findings)})</summary>")
        out += ["", "```"]
        for f in findings:
            out.append(f"{where(f)}: [{f.get('id')}] {f.get('msg')}")
        out += ["```", "", "</details>", ""]
    return "\n".join(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report", type=pathlib.Path)
    ap.add_argument("--gate", default="error,warning",
                    help="comma-separated severities that fail the build")
    args = ap.parse_args(argv)
    gate = {s.strip() for s in args.gate.split(",") if s.strip()}

    findings = load(args.report)
    if findings is None:
        # No report is a broken run, not a clean one.  Failing here is the
        # difference between "cppcheck found nothing" and "cppcheck never ran".
        print(f"### cppcheck\n\nNo usable report at `{args.report}` - "
              "the run did not complete.")
        return 1

    if not findings:
        print("### cppcheck\n\nClean: no findings.")
        return 0

    by_severity: dict[str, list[ET.Element]] = collections.defaultdict(list)
    for f in findings:
        by_severity[f.get("severity", "unknown")].append(f)

    print(render(by_severity, gate))

    gating = sum(len(by_severity.get(s, [])) for s in gate)
    if gating:
        for severity in sorted(gate):
            for f in by_severity.get(severity, []):
                # Annotated so the finding shows up on the diff in the PR.
                location = f.find("location")
                if location is not None:
                    print(f"::error file={location.get('file')},"
                          f"line={location.get('line')}::"
                          f"[{f.get('id')}] {f.get('msg')}", file=sys.stderr)
        print(f"\n**Failing: {gating} finding(s) of severity "
              f"{'/'.join(sorted(gate))}.**")
        return 1

    print("\nNo gating findings; the rest are advisory.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
