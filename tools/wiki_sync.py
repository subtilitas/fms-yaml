#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the repository's Markdown into a GitHub wiki checkout.

A wiki is a flat git repository: every page is a top-level `.md` file and the
page name is the file name.  So the docs cannot simply be copied - the paths
that work in the repository (`docs/schema.md`) are all wrong once the directory
structure is gone.  This rewrites them.

Usage:
    python3 tools/wiki_sync.py --source . --wiki /path/to/checked-out.wiki
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# repository path -> wiki page name.  Order matters only for the sidebar.
PAGES: dict[str, str] = {
    "README.md": "Home",
    "docs/architecture.md": "Architecture",
    "docs/schema.md": "Schema",
    "docs/stability.md": "Stability",
    "docs/testing.md": "Testing",
    "CONTRIBUTING.md": "Contributing",
    "SECURITY.md": "Security",
    "CHANGELOG.md": "Changelog",
}

BANNER = (
    "<!-- Generated from {source} by tools/wiki_sync.py. "
    "Edits made here are overwritten on the next push to the default branch. -->\n\n"
)


# Any number of leading ./ or ../ segments.  A page under docs/ reaches the
# README as ../README.md and its neighbour as schema.md, and both have to be
# recognised or the wiki gets a link to a file that is not there.
RELATIVE_PREFIX = r"(?:\.{1,2}/)*"


def spellings(repo_path: str) -> str:
    """How one Markdown file may refer to another: by repository path, or - for
    two files in the same directory - by file name alone."""
    names = {repo_path, pathlib.PurePosixPath(repo_path).name}
    longest_first = sorted(names, key=len, reverse=True)
    return "(?:" + "|".join(re.escape(name) for name in longest_first) + ")"


def rewrite_links(text: str) -> str:
    """Point in-repository Markdown links at the corresponding wiki pages."""
    for repo_path, page in PAGES.items():
        if repo_path == "README.md":
            continue
        path = RELATIVE_PREFIX + spellings(repo_path)
        # A link whose text is the path reads badly once the path is gone:
        # [docs/schema.md](Schema) -> [Schema](Schema).
        text = re.sub(
            r"\[" + path + r"\]\(" + path + r"(#[^)]*)?\)",
            lambda m, p=page: f"[{p}]({p}{m.group(1) or ''})",
            text,
        )
        # (docs/schema.md)  ->  (Schema)      (schema.md#guards) -> (Schema#guards)
        text = re.sub(
            r"\(" + path + r"(#[^)]*)?\)",
            lambda m, p=page: f"({p}{m.group(1) or ''})",
            text,
        )

    # A link back to the README is a link to Home.
    return re.sub(
        r"\(" + RELATIVE_PREFIX + r"README\.md(#[^)]*)?\)",
        lambda m: f"(Home{m.group(1) or ''})",
        text,
    )


def sidebar() -> str:
    lines = ["### fms-yaml", ""]
    lines += [f"- [[{page}]]" for page in PAGES.values()]
    lines += [
        "",
        "---",
        "",
        "<sub>Generated from the repository. Edit the files under `docs/` instead.</sub>",
        "",
    ]
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=pathlib.Path, default=pathlib.Path())
    ap.add_argument("--wiki", type=pathlib.Path, required=True)
    args = ap.parse_args(argv)

    if not args.wiki.is_dir():
        print(f"{args.wiki}: not a directory", file=sys.stderr)
        return 1

    written: list[str] = []

    for repo_path, page in PAGES.items():
        src = args.source / repo_path
        if not src.exists():
            print(f"skipping {repo_path}: not found")
            continue
        body = rewrite_links(src.read_text(encoding="utf-8"))
        target = args.wiki / f"{page}.md"
        target.write_text(BANNER.format(source=repo_path) + body, encoding="utf-8", newline="\n")
        written.append(target.name)

    (args.wiki / "_Sidebar.md").write_text(sidebar(), encoding="utf-8", newline="\n")
    written.append("_Sidebar.md")

    print("wrote: " + ", ".join(written))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
