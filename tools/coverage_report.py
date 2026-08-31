#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn a gcovr JSON summary into a checked-in badge and a README table.

The badge is an SVG committed next to the README, written by CI on every push
to the default branch, rather than a call to a badge service: a file in the
repository needs no third-party account, no token and no request to anywhere,
it is versioned alongside the numbers it describes - so an old commit shows the
coverage that commit had - and it keeps rendering when a service changes its
URL scheme or disappears.

Usage:
    gcovr --json-summary-pretty -o coverage.json
    python3 tools/coverage_report.py coverage.json --readme README.md \
        --badge docs/badges/coverage.svg [--fail-under 70]

Exit status is 0 unless --fail-under is given and line coverage is below it.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import xml.sax.saxutils as saxutils

BEGIN = "<!-- coverage:begin -->"
END = "<!-- coverage:end -->"

# Thresholds are the usual traffic light.  They only pick a colour; the gate is
# --fail-under, which is a separate decision made by the caller.
COLOURS = (
    (90.0, "#4c1"),      # bright green
    (80.0, "#97ca00"),   # green
    (70.0, "#a4a61d"),   # yellow-green
    (60.0, "#dfb317"),   # yellow
    (40.0, "#fe7d37"),   # orange
    (0.0, "#e05d44"),    # red
)


def colour_for(percent: float) -> str:
    for floor, colour in COLOURS:
        if percent >= floor:
            return colour
    return COLOURS[-1][1]


def text_width(text: str) -> int:
    """Approximate the rendered width of 11px DejaVu Sans, in pixels.

    Badges are generated, never hand-measured, so this only has to be close
    enough that the coloured box does not clip the digits.
    """
    narrow = sum(1 for c in text if c in "1iljI.,:;'|!")
    wide = sum(1 for c in text if c in "%mwMW@")
    return int(6.6 * len(text) + 2.4 * wide - 3.0 * narrow) + 1


def badge_svg(label: str, value: str, colour: str) -> str:
    pad = 10
    label_w = text_width(label) + pad * 2
    value_w = text_width(value) + pad * 2
    total = label_w + value_w
    label_mid = label_w * 5
    value_mid = (label_w * 2 + value_w) * 5
    label_e = saxutils.escape(label)
    value_e = saxutils.escape(value)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" \
xmlns:xlink="http://www.w3.org/1999/xlink" width="{total}" height="20" \
role="img" aria-label="{label_e}: {value_e}">
  <title>{label_e}: {value_e}</title>
  <linearGradient id="s" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <clipPath id="r"><rect width="{total}" height="20" rx="3" fill="#fff"/></clipPath>
  <g clip-path="url(#r)">
    <rect width="{label_w}" height="20" fill="#555"/>
    <rect x="{label_w}" width="{value_w}" height="20" fill="{colour}"/>
    <rect width="{total}" height="20" fill="url(#s)"/>
  </g>
  <g fill="#fff" text-anchor="middle" \
font-family="Verdana,Geneva,DejaVu Sans,sans-serif" \
text-rendering="geometricPrecision" font-size="110">
    <text aria-hidden="true" x="{label_mid}" y="150" fill="#010101" fill-opacity=".3" \
transform="scale(.1)" textLength="{(label_w - pad * 2) * 10}">{label_e}</text>
    <text x="{label_mid}" y="140" transform="scale(.1)" \
textLength="{(label_w - pad * 2) * 10}">{label_e}</text>
    <text aria-hidden="true" x="{value_mid}" y="150" fill="#010101" fill-opacity=".3" \
transform="scale(.1)" textLength="{(value_w - pad * 2) * 10}">{value_e}</text>
    <text x="{value_mid}" y="140" transform="scale(.1)" \
textLength="{(value_w - pad * 2) * 10}">{value_e}</text>
  </g>
</svg>
"""


def pct(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 100.0


def summarise(data: dict) -> dict:
    """gcovr's --json-summary, reduced to what the README shows.

    Totals are recomputed from the per-file counts rather than read from
    gcovr's own percentages, so the table and the badge can never disagree.
    """
    files = data.get("files", [])
    out = {"files": []}
    for key in ("line", "branch", "function"):
        covered = sum(int(f.get(f"{key}_covered", 0) or 0) for f in files)
        total = sum(int(f.get(f"{key}_total", 0) or 0) for f in files)
        out[key] = {"covered": covered, "total": total, "percent": pct(covered, total)}
    for f in files:
        out["files"].append(
            {
                "filename": f.get("filename", "?"),
                "covered": int(f.get("line_covered", 0) or 0),
                "total": int(f.get("line_total", 0) or 0),
                "percent": pct(
                    int(f.get("line_covered", 0) or 0), int(f.get("line_total", 0) or 0)
                ),
            }
        )
    out["files"].sort(key=lambda f: (f["percent"], f["filename"]))
    return out


def render_block(s: dict, badge_path: str, per_file: bool) -> str:
    rows = [
        f"![coverage]({badge_path})",
        "",
        "| Metric | Covered | Total | Coverage |",
        "| --- | ---: | ---: | ---: |",
    ]
    for key, label in (("line", "Lines"), ("branch", "Branches"), ("function", "Functions")):
        m = s[key]
        rows.append(f"| {label} | {m['covered']} | {m['total']} | {m['percent']:.1f}% |")

    if per_file:
        rows += [
            "",
            "<details><summary>Per file</summary>",
            "",
            "| File | Lines | Coverage |",
            "| --- | ---: | ---: |",
        ]
        for f in s["files"]:
            rows.append(
                f"| `{f['filename']}` | {f['covered']}/{f['total']} | {f['percent']:.1f}% |"
            )
        rows += ["", "</details>"]

    rows += ["", "<sub>Written by the coverage job in `.github/workflows/ci.yml`. "
                 "Do not edit by hand.</sub>"]
    return "\n".join(rows)


def splice(readme: pathlib.Path, block: str) -> bool:
    """Replace the marked region of the README.  Returns True if it changed."""
    text = readme.read_text(encoding="utf-8")
    start = text.find(BEGIN)
    end = text.find(END)
    if start == -1 or end == -1 or end < start:
        raise SystemExit(
            f"{readme}: could not find the '{BEGIN}' / '{END}' markers. "
            "Add them where the coverage section belongs."
        )
    updated = text[: start + len(BEGIN)] + "\n" + block + "\n" + text[end:]
    if updated == text:
        return False
    readme.write_text(updated, encoding="utf-8", newline="\n")
    return True


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("summary", type=pathlib.Path, help="gcovr --json-summary output")
    ap.add_argument("--readme", type=pathlib.Path, default=pathlib.Path("README.md"))
    ap.add_argument("--badge", type=pathlib.Path,
                    default=pathlib.Path("docs/badges/coverage.svg"))
    ap.add_argument("--badge-link", default=None,
                    help="path used inside the README (defaults to --badge)")
    ap.add_argument("--per-file", action="store_true", default=True)
    ap.add_argument("--no-per-file", dest="per_file", action="store_false")
    ap.add_argument("--fail-under", type=float, default=None,
                    help="exit non-zero if line coverage is below this")
    args = ap.parse_args(argv)

    data = json.loads(args.summary.read_text(encoding="utf-8"))
    s = summarise(data)
    line_pct = s["line"]["percent"]

    args.badge.parent.mkdir(parents=True, exist_ok=True)
    svg = badge_svg("coverage", f"{line_pct:.1f}%", colour_for(line_pct))
    previous = args.badge.read_text(encoding="utf-8") if args.badge.exists() else None
    if previous != svg:
        args.badge.write_text(svg, encoding="utf-8", newline="\n")

    link = args.badge_link or args.badge.as_posix()
    changed = splice(args.readme, render_block(s, link, args.per_file))

    print(f"lines     {s['line']['covered']}/{s['line']['total']}  {line_pct:.1f}%")
    print(f"branches  {s['branch']['covered']}/{s['branch']['total']}  "
          f"{s['branch']['percent']:.1f}%")
    print(f"functions {s['function']['covered']}/{s['function']['total']}  "
          f"{s['function']['percent']:.1f}%")
    print(f"README {'updated' if changed else 'unchanged'}; "
          f"badge {'updated' if previous != svg else 'unchanged'}")

    if args.fail_under is not None and line_pct + 1e-9 < args.fail_under:
        print(f"::error::line coverage {line_pct:.1f}% is below the "
              f"{args.fail_under:.1f}% floor", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
