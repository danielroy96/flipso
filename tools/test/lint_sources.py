#!/usr/bin/env python3
"""
Check that application.fam's source patterns take app files and nothing else.

fbt matches each pattern from every directory in the tree rather than from the
project root alone, so "format/*.c" also takes tools/test/format/*.c when there
is such a directory - which once put the screen tests into the .fap, where they
broke the firmware build. This walks the tree the way fbt does and fails on any
match that is not an app source, or on an app source no pattern takes.
"""

import ast
import fnmatch
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Not app sources, though fbt's walk goes through them: only names starting
# with "." are hidden from it, as from SCons's Glob (SCons/Node/FS.py, _glob1).
NOT_APP = {"tools", "build", "dist", "__pycache__"}


def fam_sources() -> list[str]:
    """The sources= list of application.fam, read without running it."""
    tree = ast.parse((ROOT / "application.fam").read_text())
    for node in ast.walk(tree):
        if isinstance(node, ast.keyword) and node.arg == "sources":
            return [ast.literal_eval(e) for e in node.value.elts]
    raise SystemExit("lint_sources: no sources= in application.fam")


def matches(pattern: str) -> set[Path]:
    """What fbt's GlobRecursive takes for one pattern: the pattern tried from every directory."""
    found = set()
    for here, dirs, _ in os.walk(ROOT):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        base = Path(here)
        prefix, _, name = pattern.rpartition("/")
        directory = base / prefix if prefix else base
        if directory.is_dir():
            found |= {p for p in directory.iterdir()
                      if p.is_file() and not p.name.startswith(".") and fnmatch.fnmatch(p.name, name)}
    return found


def main() -> int:
    taken = set()
    for pattern in fam_sources():
        taken |= matches(pattern)
    app = {p for p in ROOT.rglob("*.c")
           if p.relative_to(ROOT).parts[0] not in NOT_APP
           and not any(part.startswith(".") for part in p.relative_to(ROOT).parts)}
    problems = [f"  {p.relative_to(ROOT)} is not an app source but application.fam takes it"
                for p in sorted(taken - app)]
    problems += [f"  {p.relative_to(ROOT)} is an app source no pattern takes"
                 for p in sorted(app - taken)]
    if problems:
        print("\n".join(problems))
        print("  [FAIL] application.fam's sources are the app's")
        return 1
    print(f"  [PASS] application.fam's sources are the app's ({len(taken)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
