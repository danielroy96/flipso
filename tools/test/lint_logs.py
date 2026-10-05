"""Fail when an app source logs at debug level.

The Apps Catalog sends back submissions that ship development logging ("piles of
debug logging left in from development (e.g. lots of verbose FURI_LOG_D / trace
logs)", AGENTS.md in flipperdevices/flipper-application-catalog). Flipso's
reader once logged every directory entry, and on a rejected shell the shell's
bytes - the card number among them - at debug level. What the app logs now is
what a user's bug report needs: the info, warning and error lines.

To see what a real card produced, save it and replay the file
(tools/test/replay.py); for a read that fails before there is a card to save,
add a FURI_LOG_D locally, or build tools/debug/flipso_dump.c in, and do not
commit it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBUG = re.compile(r"\bFURI_LOG_(D|T)\(")

# Every app source, at any depth; the host tools under tools/ are not the app.
SKIP = ("tools", "build", "dist")
sources = [p for p in sorted(ROOT.rglob("*.[ch]"))
           if p.relative_to(ROOT).parts[0] not in SKIP and not p.relative_to(ROOT).parts[0].startswith(".")]

problems = []
for path in sources:
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if DEBUG.search(line):
            problems.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")

if problems:
    print("Debug-level logging in the app sources - the Apps Catalog rejects it:")
    print("\n".join(problems))
    sys.exit(1)
print(f"no debug logging in {len(sources)} sources")
